#include "runtimeTests.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/object.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"
#include "../runtime/scriptRuntime.h"
#include "../runtime/standardLibrary.h"
#include "../script/script.h"
#include "../script/scriptSerializer.h"
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

        // Ranges, matching, classes, constructor arguments, properties and
        // method invocation all participate in the same graph/runtime pipeline.
        IDGenerator featureIds;
        Script featureScript;
        featureScript.ID = featureIds.GetNextId();
        featureScript.main = std::make_shared<ScriptFunction>(featureIds.GetNextId(), "FeatureMain");
        ScriptPropertyPtr classResult = std::make_shared<ScriptProperty>(featureIds.GetNextId(), "ClassFeatureResult");
        classResult->defaultValue = Value(0.0);
        featureScript.variables.push_back(classResult);
        ScriptPropertyPtr matchResult = std::make_shared<ScriptProperty>(featureIds.GetNextId(), "MatchFeatureResult");
        matchResult->defaultValue = Value(false);
        featureScript.variables.push_back(matchResult);
        ScriptPropertyPtr flowMatchResult = std::make_shared<ScriptProperty>(featureIds.GetNextId(), "FlowMatchFeatureResult");
        flowMatchResult->defaultValue = Value(0.0);
        featureScript.variables.push_back(flowMatchResult);

        ScriptClassPtr counter = std::make_shared<ScriptClass>(featureIds.GetNextId(), "Counter");
        ScriptPropertyPtr counterValue = std::make_shared<ScriptProperty>(featureIds.GetNextId(), "value");
        counterValue->defaultValue = Value(1.0);
        counter->properties.push_back(counterValue);

        counter->constructor = std::make_shared<ScriptFunction>(featureIds.GetNextId(), "init");
        counter->constructor->functionDef->inputs.push_back({ "start", Value(0.0), featureIds.GetNextId() });
        NodePtr constructorBegin = BuildBeginNode(featureIds, counter->constructor);
        NodePtr constructorThis = BuildThisNode(featureIds);
        NodePtr constructorSet = BuildSetPropertyNode(featureIds, counterValue);
        AttachNode(counter->constructor->Graph, constructorBegin);
        AttachNode(counter->constructor->Graph, constructorThis);
        AttachNode(counter->constructor->Graph, constructorSet);
        Link constructorFlow(featureIds.GetNextId(), constructorBegin->Outputs[0].ID, constructorSet->Inputs[0].ID);
        Link constructorObject(featureIds.GetNextId(), constructorThis->Outputs[0].ID, constructorSet->Inputs[1].ID);
        Link constructorValue(featureIds.GetNextId(), constructorBegin->Outputs[1].ID, constructorSet->Inputs[2].ID);
        counter->constructor->Graph.AddLink(constructorFlow);
        counter->constructor->Graph.AddLink(constructorObject);
        counter->constructor->Graph.AddLink(constructorValue);

        ScriptFunctionPtr getValue = std::make_shared<ScriptFunction>(featureIds.GetNextId(), "getValue");
        getValue->functionDef->outputs.push_back({ "Value", Value(0.0), featureIds.GetNextId() });
        NodePtr methodBegin = BuildBeginNode(featureIds, getValue);
        NodePtr methodThis = BuildThisNode(featureIds);
        NodePtr propertyGet = BuildGetPropertyNode(featureIds, counterValue);
        NodePtr methodReturn = BuildReturnNode(featureIds, *getValue);
        AttachNode(getValue->Graph, methodBegin);
        AttachNode(getValue->Graph, methodThis);
        AttachNode(getValue->Graph, propertyGet);
        AttachNode(getValue->Graph, methodReturn);
        Link methodFlow(featureIds.GetNextId(), methodBegin->Outputs[0].ID, methodReturn->Inputs[0].ID);
        Link methodObject(featureIds.GetNextId(), methodThis->Outputs[0].ID, propertyGet->Inputs[0].ID);
        Link methodValue(featureIds.GetNextId(), propertyGet->Outputs[0].ID, methodReturn->Inputs[1].ID);
        getValue->Graph.AddLink(methodFlow);
        getValue->Graph.AddLink(methodObject);
        getValue->Graph.AddLink(methodValue);
        counter->methods.push_back(getValue);
        featureScript.classes.push_back(counter);

        NodePtr featureBegin = BuildBeginNode(featureIds, featureScript.main);
        NodePtr construct = BuildConstructObjectNode(featureIds, counter);
        construct->InputValues[1] = Value(7.0);
        NodePtr callGetValue = BuildMethodCallNode(featureIds, getValue);
        NodePtr storeClassResult = BuildSetVariableNode(featureIds, classResult);
        NodePtr storeMatchResult = BuildSetVariableNode(featureIds, matchResult);
        NodePtr range = registry.FindCompiled("Range::Make")->MakeNode(featureIds);
        range->InputValues[0] = Value(2.0);
        range->InputValues[1] = Value(4.0);
        NodePtr flowMatch = registry.FindCompiled("Flow::Match")->MakeNode(featureIds);
        flowMatch->AddInput(featureIds);
        flowMatch->InputValues[1] = Value(3.0);
        flowMatch->InputValues[2] = Value(1.0);
        NodePtr flowRange = registry.FindCompiled("Range::Make")->MakeNode(featureIds);
        flowRange->InputValues[0] = Value(2.0);
        flowRange->InputValues[1] = Value(4.0);
        NodePtr storeFirstCase = BuildSetVariableNode(featureIds, flowMatchResult);
        storeFirstCase->InputValues[1] = Value(10.0);
        NodePtr storeSecondCase = BuildSetVariableNode(featureIds, flowMatchResult);
        storeSecondCase->InputValues[1] = Value(20.0);
        NodePtr storeDefaultCase = BuildSetVariableNode(featureIds, flowMatchResult);
        storeDefaultCase->InputValues[1] = Value(99.0);
        AttachNode(featureScript.main->Graph, featureBegin);
        AttachNode(featureScript.main->Graph, construct);
        AttachNode(featureScript.main->Graph, callGetValue);
        AttachNode(featureScript.main->Graph, storeClassResult);
        AttachNode(featureScript.main->Graph, storeMatchResult);
        AttachNode(featureScript.main->Graph, range);
        AttachNode(featureScript.main->Graph, flowMatch);
        AttachNode(featureScript.main->Graph, flowRange);
        AttachNode(featureScript.main->Graph, storeFirstCase);
        AttachNode(featureScript.main->Graph, storeSecondCase);
        AttachNode(featureScript.main->Graph, storeDefaultCase);
        Link featureFlow1(featureIds.GetNextId(), featureBegin->Outputs[0].ID, construct->Inputs[0].ID);
        Link featureFlow2(featureIds.GetNextId(), construct->Outputs[0].ID, callGetValue->Inputs[0].ID);
        Link featureInstance(featureIds.GetNextId(), construct->Outputs[1].ID, callGetValue->Inputs[1].ID);
        Link featureFlow3(featureIds.GetNextId(), callGetValue->Outputs[0].ID, storeClassResult->Inputs[0].ID);
        Link featureClassValue(featureIds.GetNextId(), callGetValue->Outputs[1].ID, storeClassResult->Inputs[1].ID);
        Link featureFlow4(featureIds.GetNextId(), storeClassResult->Outputs[0].ID, storeMatchResult->Inputs[0].ID);
        Link featureFlow5(featureIds.GetNextId(), storeMatchResult->Outputs[0].ID, flowMatch->Inputs[0].ID);
        Link featureFlowPattern(featureIds.GetNextId(), flowRange->Outputs[0].ID, flowMatch->Inputs[3].ID);
        Link featureFirstCase(featureIds.GetNextId(), flowMatch->Outputs[0].ID, storeFirstCase->Inputs[0].ID);
        Link featureSecondCase(featureIds.GetNextId(), flowMatch->Outputs[1].ID, storeSecondCase->Inputs[0].ID);
        Link featureDefaultCase(featureIds.GetNextId(), flowMatch->Outputs[2].ID, storeDefaultCase->Inputs[0].ID);
        featureScript.main->Graph.AddLink(featureFlow1);
        featureScript.main->Graph.AddLink(featureFlow2);
        featureScript.main->Graph.AddLink(featureInstance);
        featureScript.main->Graph.AddLink(featureFlow3);
        featureScript.main->Graph.AddLink(featureClassValue);
        featureScript.main->Graph.AddLink(featureFlow4);
        featureScript.main->Graph.AddLink(featureFlow5);
        featureScript.main->Graph.AddLink(featureFlowPattern);
        featureScript.main->Graph.AddLink(featureFirstCase);
        featureScript.main->Graph.AddLink(featureSecondCase);
        featureScript.main->Graph.AddLink(featureDefaultCase);

        std::string featureDocument;
        Require(static_cast<bool>(ScriptSerializer::SerializeToString(featureScript, featureDocument)),
                "Classes and object nodes should serialize.");
        Script restoredFeatures;
        IDGenerator restoredFeatureIds;
        Require(static_cast<bool>(ScriptSerializer::DeserializeFromString(featureDocument, registry,
                    restoredFeatures, restoredFeatureIds)),
                "Classes and object nodes should deserialize.");
        Require(restoredFeatures.classes.size() == 1 && restoredFeatures.classes[0]->constructor &&
                restoredFeatures.classes[0]->methods.size() == 1 &&
                restoredFeatures.classes[0]->properties.size() == 1,
                "Class structure should survive a serialization round trip.");

        vm.setExternalMarkingFunc([&]()
        {
            MarkNodeRegistryRoots(registry, vm);
            ScriptUtils::MarkScriptRoots(restoredFeatures);
        });
        const ScriptCompileResult featureCompilation = ScriptRuntime::Compile(vm, restoredFeatures);
        Require(static_cast<bool>(featureCompilation), "The complete class/range/match script should compile.");
        Require(ScriptRuntime::Execute(vm, featureCompilation.function) == InterpretResult::INTERPRET_OK,
                "The complete class/range/match script should execute.");
        Value observedClassResult;
        Value observedMatchResult;
        Value observedFlowMatchResult;
        Require(vm.globalTable().get(copyString("ClassFeatureResult", 18), &observedClassResult) &&
                isNumber(observedClassResult) && asNumber(observedClassResult) == 7.0,
                "Constructor property assignment and method property access should produce 7.");
        Require(vm.globalTable().get(copyString("MatchFeatureResult", 18), &observedMatchResult) &&
                isBoolean(observedMatchResult) && asBoolean(observedMatchResult),
                "Matching 3 against range 2..4 should produce true.");
        Require(vm.globalTable().get(copyString("FlowMatchFeatureResult", 22), &observedFlowMatchResult) &&
                isNumber(observedFlowMatchResult) && asNumber(observedFlowMatchResult) == 20.0,
                "Flow Match should execute the first matching case and skip Default.");

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
