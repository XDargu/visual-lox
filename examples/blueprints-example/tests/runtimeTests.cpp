#include "runtimeTests.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../runtime/scriptRuntime.h"
#include "../runtime/standardLibrary.h"
#include "../script/script.h"
#include "../validation/scriptValidator.h"

#include <Vm.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void AttachNode(Graph& graph, const NodePtr& node)
{
    NodeUtils::BuildNode(node);
    graph.AddNode(node);
}

bool HasCode(const ValidationReport& report, const char* code)
{
    return std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
        [&](const ValidationDiagnostic& diagnostic) { return diagnostic.code == code; });
}
}

int RunRuntimeTests()
{
    try
    {
        VM& vm = VM::getInstance();
        const bool wasGcAllowed = vm.isGarbageCollectionAllowed();
        vm.allowGarbageCollection(false);

        NodeRegistry registry;
        RegisterStandardLibrary(registry);
        registry.RegisterNatives(vm);

        const CompiledNodeDefPtr addDefinition = registry.FindCompiled("Math::Add");
        const CompiledNodeDefPtr printDefinition = registry.FindCompiled("Debug::Print");
        Require(addDefinition && HasFlag(addDefinition->functionDef->flags, NodeDefinitionFlags::Pure),
                "Math::Add should be declared pure.");
        Require(printDefinition && !HasFlag(printDefinition->functionDef->flags, NodeDefinitionFlags::Pure),
                "Debug::Print should be declared impure.");
        Require(HasFlag(registry.FindNative("Math::Square")->functionDef->flags, NodeDefinitionFlags::Pure),
                "Math::Square should be declared pure.");
        Require(!HasFlag(registry.FindNative("File::FileExists")->functionDef->flags, NodeDefinitionFlags::Pure),
                "File::FileExists depends on external state and must not be pure.");
        Require(!HasFlag(registry.FindNative("Functional::Map")->functionDef->flags, NodeDefinitionFlags::Pure),
                "Higher-order functions cannot be pure without a pure callable contract.");
        IDGenerator invalidIds;
        NodePtr flagSeparationNode = addDefinition->MakeNode(invalidIds);
        flagSeparationNode->InstanceFlags |= NodeInstanceFlags::Error;
        Require(flagSeparationNode->IsPure() &&
                HasFlag(flagSeparationNode->InstanceFlags, NodeInstanceFlags::Error),
                "Instance errors must not change immutable definition capabilities.");

        Script missingBegin;
        missingBegin.ID = invalidIds.GetNextId();
        missingBegin.main = std::make_shared<ScriptFunction>(invalidIds.GetNextId(), "Main");
        const ValidationReport missingBeginReport = ScriptValidator::Validate(missingBegin);
        Require(missingBeginReport.HasErrors() && HasCode(missingBeginReport, "begin-count"),
                "Validation should reject a graph without Begin.");

        IDGenerator cycleIds;
        Script cyclic;
        cyclic.ID = cycleIds.GetNextId();
        cyclic.main = std::make_shared<ScriptFunction>(cycleIds.GetNextId(), "Main");
        AttachNode(cyclic.main->Graph, BuildBeginNode(cycleIds, cyclic.main));
        NodePtr cycleA = addDefinition->MakeNode(cycleIds);
        NodePtr cycleB = addDefinition->MakeNode(cycleIds);
        AttachNode(cyclic.main->Graph, cycleA);
        AttachNode(cyclic.main->Graph, cycleB);
        Link cycleLinkA(cycleIds.GetNextId(), cycleA->Outputs[0].ID, cycleB->Inputs[0].ID);
        Link cycleLinkB(cycleIds.GetNextId(), cycleB->Outputs[0].ID, cycleA->Inputs[0].ID);
        cyclic.main->Graph.AddLink(cycleLinkA);
        cyclic.main->Graph.AddLink(cycleLinkB);
        const ValidationReport cycleReport = ScriptValidator::Validate(cyclic);
        Require(cycleReport.HasErrors() && HasCode(cycleReport, "graph-cycle"),
                "Validation should reject dependency cycles.");

        IDGenerator ids;
        Script script;
        script.ID = ids.GetNextId();
        script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
        NodePtr begin = BuildBeginNode(ids, script.main);
        NodePtr add = addDefinition->MakeNode(ids);
        add->InputValues[0] = Value(2.0);
        add->InputValues[1] = Value(3.0);
        NodePtr print = printDefinition->MakeNode(ids);
        AttachNode(script.main->Graph, begin);
        AttachNode(script.main->Graph, add);
        AttachNode(script.main->Graph, print);
        Link flow(ids.GetNextId(), begin->Outputs[0].ID, print->Inputs[0].ID);
        Link data(ids.GetNextId(), add->Outputs[0].ID, print->Inputs[1].ID);
        script.main->Graph.AddLink(flow);
        script.main->Graph.AddLink(data);

        vm.setExternalMarkingFunc([&]()
        {
            MarkNodeRegistryRoots(registry, vm);
            ScriptUtils::MarkScriptRoots(script);
        });
        const ScriptCompileResult compiled = ScriptRuntime::Compile(vm, script);
        Require(static_cast<bool>(compiled), "A valid script should compile.");
        const auto folded = std::find(compiled.foldedNodeIds.begin(), compiled.foldedNodeIds.end(), add->ID);
        Require(folded != compiled.foldedNodeIds.end(), "A reachable pure Add node should be folded.");
        const size_t foldedIndex = static_cast<size_t>(std::distance(compiled.foldedNodeIds.begin(), folded));
        Require(isNumber(compiled.foldedValues[foldedIndex]) && asNumber(compiled.foldedValues[foldedIndex]) == 5.0,
                "Constant folding should preserve the Add result.");
        Require(ScriptRuntime::Execute(vm, compiled.function) == InterpretResult::INTERPRET_OK,
                "The folded script should execute successfully.");

        NodePtr replacementAdd = addDefinition->MakeNode(ids);
        NodePtr replacementPrint = printDefinition->MakeNode(ids);
        AttachNode(script.main->Graph, replacementAdd);
        AttachNode(script.main->Graph, replacementPrint);

        Link replacementData(ids.GetNextId(), replacementAdd->Outputs[0].ID, print->Inputs[1].ID);
        Require(script.main->Graph.CanCreateLink(&replacementAdd->Outputs[0], &print->Inputs[1], {}) ==
                    ELinkQueryResult::Possible,
                "An occupied data input should accept a valid replacement connection.");
        script.main->Graph.AddLink(replacementData);
        Require(script.main->Graph.GetLinks().size() == 2,
                "Replacing a data input connection should remove the previous link.");
        Require(GraphUtils::FindConnectedOutput(script.main->Graph, print->Inputs[1])->Node == replacementAdd,
                "The replacement data connection should become active.");

        Link replacementFlow(ids.GetNextId(), begin->Outputs[0].ID, replacementPrint->Inputs[0].ID);
        script.main->Graph.AddLink(replacementFlow);
        Require(script.main->Graph.GetLinks().size() == 2,
                "Replacing a flow output connection should remove the previous link.");
        const std::vector<const Pin*> flowTargets =
            GraphUtils::FindConnectedInputs(script.main->Graph, begin->Outputs[0]);
        Require(flowTargets.size() == 1 && flowTargets[0]->Node == replacementPrint,
                "The replacement flow connection should become active.");

        vm.setExternalMarkingFunc([]() {});
        vm.allowGarbageCollection(wasGcAllowed);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Runtime test failure: " << exception.what() << '\n';
        VM::getInstance().setExternalMarkingFunc([]() {});
        return 1;
    }
}
