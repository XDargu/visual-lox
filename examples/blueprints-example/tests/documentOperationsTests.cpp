#include "documentOperationsTests.h"

#include "testFramework.h"
#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/variable.h"
#include "../operations/documentOperations.h"
#include "../runtime/standardLibrary.h"

#include <Vm.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
using Tests::Require;

void RequireSuccess(const OperationResult& result, const char* message)
{
    if (!result)
        throw std::runtime_error(std::string(message) + " " + result.error);
}

void Attach(Graph& graph, const NodePtr& node)
{
    NodeUtils::BuildNode(node);
    graph.AddNode(node);
}

struct OperationsFixture
{
    OperationsFixture()
        : vm(VM::getInstance())
        , wasGcAllowed(vm.isGarbageCollectionAllowed())
    {
        vm.allowGarbageCollection(false);
        RegisterStandardLibrary(registry);
        registry.RegisterNatives(vm);

        script.ID = ids.GetNextId();
        script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
        begin = BuildBeginNode(ids, script.main);
        Attach(script.main->Graph, begin);
        operations = std::make_unique<DocumentOperations>(script, ids, registry);
    }

    ~OperationsFixture()
    {
        vm.allowGarbageCollection(wasGcAllowed);
    }

    ScriptFunctionPtr AddWorker()
    {
        const int functionId = ids.GetNextId();
        RequireSuccess(operations->AddFunction(functionId, "Worker"), "Adding a function failed.");
        return ScriptUtils::FindFunctionById(script, functionId);
    }

    VM& vm;
    bool wasGcAllowed;
    NodeRegistry registry;
    IDGenerator ids;
    Script script;
    NodePtr begin;
    std::unique_ptr<DocumentOperations> operations;
};

void RequiredBeginCannotBeDeleted()
{
    OperationsFixture fixture;
    Require(!fixture.operations->RemoveNode(fixture.script.main->ID.id, fixture.begin->ID),
            "The required Begin node must be protected from deletion.");
    Require(fixture.script.main->Graph.GetNodes().size() == 1,
            "Deleting a protected node changed the graph.");
}

void MainSignatureCannotBeEdited()
{
    OperationsFixture fixture;
    fixture.script.main->functionDef->inputs.push_back(
        { "Arguments", Value(newList()), fixture.ids.GetNextId() });
    const int inputId = fixture.script.main->functionDef->inputs.front().id;
    Require(!fixture.operations->AddFunctionInput(
                fixture.script.main->ID.id, fixture.ids.GetNextId(), "Other"),
            "Main accepted an additional input.");
    Require(!fixture.operations->RemoveFunctionInput(
                fixture.script.main->ID.id, inputId),
            "Main allowed its Arguments input to be removed.");
    Require(!fixture.operations->ChangeFunctionInputValue(
                fixture.script.main->ID.id, inputId, Value(false)),
            "Main allowed its Arguments input type to be changed.");
    Require(!fixture.operations->AddFunctionOutput(
                fixture.script.main->ID.id, fixture.ids.GetNextId(), "Result"),
            "Main accepted a configurable output.");
}

void NodeStateCanBeUndoneAndRedone()
{
    OperationsFixture fixture;
    RequireSuccess(fixture.operations->SetNodeState(fixture.script.main->ID.id, fixture.begin->ID,
                    "{\"location\": [10, 20]}", true),
            "Recording a node position failed.");
    RequireSuccess(fixture.operations->Undo(), "Undoing a node position failed.");
    fixture.begin = fixture.script.main->Graph.GetNodes().front();
    Require(fixture.begin->State.empty(), "Undo did not restore the previous node position.");
    RequireSuccess(fixture.operations->Redo(), "Redoing a node position failed.");
    fixture.begin = fixture.script.main->Graph.GetNodes().front();
    Require(!fixture.begin->State.empty(), "Redo did not restore the node position.");
}

void CompiledDynamicInputsCanBeUndoneAndRedone()
{
    OperationsFixture fixture;
    NodePtr add = fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    const ed::NodeId addId = add->ID;
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, add), "Adding Math::Add failed.");
    RequireSuccess(fixture.operations->AddDynamicInput(fixture.script.main->ID.id, addId), "Adding a Math::Add input failed.");

    add = fixture.script.main->Graph.FindNode(addId);
    Require(add && add->Inputs.size() == 3 && add->Inputs[2].Name == "C" && add->Inputs[2].Type == PinType::Float,
            "The additional Math::Add input has the wrong layout.");
    RequireSuccess(fixture.operations->Undo(), "Undoing a dynamic input failed.");
    add = fixture.script.main->Graph.FindNode(addId);
    Require(add && add->Inputs.size() == 2, "Undo did not remove the dynamic input.");
    RequireSuccess(fixture.operations->Redo(), "Redoing a dynamic input failed.");
    add = fixture.script.main->Graph.FindNode(addId);
    Require(add && add->Inputs.size() == 3, "Redo did not restore the dynamic input.");

    const int variableId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddVariable(variableId, "Operand", Value(7.0)), "Adding the operand variable failed.");
    ScriptPropertyPtr variable = ScriptUtils::FindVariableById(fixture.script, variableId);
    NodePtr source = BuildGetVariableNode(fixture.ids, variable);
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, source), "Adding the operand node failed.");
    RequireSuccess(fixture.operations->Connect(fixture.script.main->ID.id, source->Outputs[0].ID, add->Inputs[2].ID),
            "Connecting the dynamic input failed.");
    const ed::PinId removedPin = add->Inputs[2].ID;
    RequireSuccess(fixture.operations->RemoveDynamicInput(fixture.script.main->ID.id, addId, removedPin), "Removing the dynamic input failed.");
    Require(fixture.script.main->Graph.FindNode(addId)->Inputs.size() == 2 && fixture.script.main->Graph.GetLinks().empty(),
            "Removing a dynamic input should also remove its link.");
    RequireSuccess(fixture.operations->Undo(), "Undoing dynamic input removal failed.");
    add = fixture.script.main->Graph.FindNode(addId);
    Require(add && add->Inputs.size() == 3 && fixture.script.main->Graph.GetLinks().size() == 1,
            "Undo did not restore the dynamic input and its link.");
}

void MakeListTypeOverrideCanBeUndoneAndRedone()
{
    OperationsFixture fixture;
    const int variableId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddVariable(variableId, "Count", Value(42.0)),
            "Adding a numeric variable failed.");
    ScriptPropertyPtr variable = ScriptUtils::FindVariableById(fixture.script, variableId);
    NodePtr source = BuildGetVariableNode(fixture.ids, variable);
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, source),
            "Adding the numeric source failed.");
    const int textVariableId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddVariable(
            textVariableId, "Label", Value(copyString("mixed", 5))),
            "Adding a text variable failed.");
    ScriptPropertyPtr textVariable =
        ScriptUtils::FindVariableById(fixture.script, textVariableId);
    NodePtr textSource = BuildGetVariableNode(fixture.ids, textVariable);
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, textSource),
            "Adding the text source failed.");

    NodePtr makeList =
        fixture.registry.FindNative("List::MakeList")->functionDef->MakeNode(
            fixture.ids, ScriptElementID::Invalid);
    makeList->AddInput(fixture.ids);
    makeList->AddInput(fixture.ids);
    const int makeListId = static_cast<int>(makeList->ID.Get());
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, makeList),
            "Adding MakeList failed.");
    RequireSuccess(fixture.operations->Connect(
            fixture.script.main->ID.id, source->Outputs[0].ID, makeList->Inputs[0].ID),
            "Connecting MakeList failed.");
    Require(makeList->Outputs[0].Type == TypeRef::List(PinType::Float),
            "MakeList did not infer Number from its input.");
    Require(makeList->ResolvedTypeVariables.at("T") == PinType::Float,
            "The inferred generic type was not exposed as node state.");

    RequireSuccess(fixture.operations->ChangeNodeTypeOverride(
            fixture.script.main->ID.id, makeList->ID, "T", PinType::Any),
            "Changing MakeList to List<Any> failed.");
    Require(makeList->Inputs[0].Type == PinType::Any &&
            makeList->Inputs[1].Type == PinType::Any &&
            makeList->Outputs[0].Type == TypeRef::List(PinType::Any),
            "The explicit Any type did not replace MakeList inference.");
    Require(makeList->ResolvedTypeVariables.at("T") == PinType::Any,
            "The explicit generic type was not exposed as node state.");
    Require(fixture.script.main->Graph.CanCreateLink(
                &textSource->Outputs[0], &makeList->Inputs[1], {}) ==
                ELinkQueryResult::Possible,
            "An explicit List<Any> did not accept a differently typed input.");

    RequireSuccess(fixture.operations->Undo(), "Undoing the MakeList type failed.");
    makeList = fixture.script.main->Graph.FindNode(ed::NodeId(makeListId));
    Require(makeList && makeList->TypeOverrides.empty() &&
            makeList->Outputs[0].Type == TypeRef::List(PinType::Float),
            "Undo did not restore MakeList inference.");

    RequireSuccess(fixture.operations->Redo(), "Redoing the MakeList type failed.");
    makeList = fixture.script.main->Graph.FindNode(ed::NodeId(makeListId));
    Require(makeList && makeList->TypeOverrides.at("T") == PinType::Any &&
            makeList->Outputs[0].Type == TypeRef::List(PinType::Any),
            "Redo did not restore the explicit List<Any> type.");
}

void FunctionChangesCanBeUndoneAndRedone()
{
    OperationsFixture fixture;
    const int functionId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddFunction(functionId, "Worker"), "Adding a function failed.");
    Require(fixture.script.functions.size() == 1, "The function was not added.");
    RequireSuccess(fixture.operations->Undo(), "Undoing a script-data operation failed.");
    Require(fixture.script.functions.empty(), "Undo did not restore the function list.");
    RequireSuccess(fixture.operations->Redo(), "Redoing a script-data operation failed.");
    Require(fixture.script.functions.size() == 1, "Redo did not restore the function.");

    ScriptFunctionPtr worker = ScriptUtils::FindFunctionById(fixture.script, functionId);
    NodePtr recursiveCall = worker->functionDef->MakeNode(fixture.ids, worker->ID);
    RequireSuccess(fixture.operations->AddNode(functionId, recursiveCall),
            "Adding a recursive call node failed.");
}

void MarkingFunctionPureMakesCallsImplicit()
{
    OperationsFixture fixture;
    ScriptFunctionPtr worker = fixture.AddWorker();
    NodePtr workerBegin = worker->Graph.FindNodeIf(
        [](const NodePtr& node)
        {
            return node->Category == NodeCategory::Begin;
        });
    NodePtr call = worker->functionDef->MakeNode(fixture.ids, worker->ID);
    RequireSuccess(
        fixture.operations->AddNode(fixture.script.main->ID.id, call),
        "Adding a function call failed.");
    RequireSuccess(
        fixture.operations->Connect(
            fixture.script.main->ID.id, fixture.begin->Outputs[0].ID,
            call->Inputs[0].ID),
        "Connecting the function call failed.");

    RequireSuccess(
        fixture.operations->ChangeFunctionPurity(worker->ID.id, true),
        "Marking the function pure failed.");
    Require(workerBegin && !workerBegin->Outputs.empty() &&
            workerBegin->Outputs[0].Type == PinType::Flow,
            "Marking a function pure removed execution flow from its body.");
    Require(GraphUtils::IsNodeImplicit(call),
            "A call to a pure script function retained execution-flow pins.");
    Require(fixture.script.main->Graph.GetLinks().empty(),
            "Links attached to removed execution-flow pins were not cleaned up.");

    RequireSuccess(
        fixture.operations->ChangeFunctionPurity(worker->ID.id, false),
        "Marking the function impure failed.");
    Require(workerBegin && !workerBegin->Outputs.empty() &&
            workerBegin->Outputs[0].Type == PinType::Flow,
            "Clearing purity changed execution flow inside the function body.");
    Require(!GraphUtils::IsNodeImplicit(call),
            "The caller's execution-flow pins were not restored after clearing purity.");
}

void ToStringShortcutCreatesCompleteMethod()
{
    OperationsFixture fixture;
    const int classId = fixture.ids.GetNextId();
    const int methodId = fixture.ids.GetNextId();
    const int outputId = fixture.ids.GetNextId();
    RequireSuccess(
        fixture.operations->AddClass(classId, "Display"),
        "Adding the class failed.");
    RequireSuccess(
        fixture.operations->AddClassToStringMethod(
            classId, methodId, outputId),
        "Adding the toString method failed.");

    ScriptClassPtr scriptClass =
        ScriptUtils::FindClassById(fixture.script, classId);
    Require(scriptClass && scriptClass->methods.size() == 1,
            "The shortcut did not add exactly one class method.");
    ScriptFunctionPtr method = scriptClass->methods.front();
    Require(method->ID == methodId &&
            method->functionDef->name == "toString",
            "The shortcut created the wrong method.");
    Require(HasFlag(method->functionDef->flags, NodeDefinitionFlags::Pure),
            "The generated toString method should be pure.");
    Require(method->functionDef->outputs.size() == 1 &&
            method->functionDef->outputs.front().id == outputId &&
            method->functionDef->outputs.front().name == "Text" &&
            method->functionDef->outputs.front().type == PinType::String &&
            isString(method->functionDef->outputs.front().value) &&
            asString(method->functionDef->outputs.front().value)->chars ==
                "Display instance",
            "The generated toString method should return a String.");

    NodePtr begin = method->Graph.FindNodeIf(
        [](const NodePtr& node)
        {
            return node->Category == NodeCategory::Begin;
        });
    NodePtr returnNode = method->Graph.FindNodeIf(
        [](const NodePtr& node)
        {
            return node->Category == NodeCategory::Return;
        });
    Require(begin && returnNode && method->Graph.GetLinks().size() == 1,
            "The generated toString graph should contain a connected Begin and Return.");
    const Link& flowLink = method->Graph.GetLinks().front();
    Require(flowLink.StartPinID == begin->Outputs[0].ID &&
            flowLink.EndPinID == returnNode->Inputs[0].ID &&
            returnNode->InputValues.size() > 1 &&
            isString(returnNode->InputValues[1]) &&
            asString(returnNode->InputValues[1])->chars == "Display instance",
            "The generated Return node should provide the default instance text.");

    Require(!fixture.operations->AddClassToStringMethod(
                classId, fixture.ids.GetNextId(), fixture.ids.GetNextId()),
            "A class accepted a duplicate toString shortcut.");
    RequireSuccess(fixture.operations->Undo(),
                   "Undoing toString creation failed.");
    scriptClass = ScriptUtils::FindClassById(fixture.script, classId);
    Require(scriptClass && scriptClass->methods.empty(),
            "Undo did not remove the generated toString method.");
    RequireSuccess(fixture.operations->Redo(),
                   "Redoing toString creation failed.");
    scriptClass = ScriptUtils::FindClassById(fixture.script, classId);
    Require(scriptClass && scriptClass->methods.size() == 1 &&
            scriptClass->methods.front()->functionDef->name == "toString",
            "Redo did not restore the generated toString method.");
}

void NodeFragmentsPreserveLinksAndUseFreshIds()
{
    OperationsFixture fixture;
    const CompiledNodeDefPtr addDefinition = fixture.registry.FindCompiled("Math::Add");
    Require(static_cast<bool>(addDefinition), "Math::Add is not registered.");
    NodePtr first = addDefinition->MakeNode(fixture.ids);
    NodePtr second = addDefinition->MakeNode(fixture.ids);
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, first),
            "Adding the first node failed.");
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, second),
            "Adding the second node failed.");
    RequireSuccess(fixture.operations->Connect(fixture.script.main->ID.id,
                    first->Outputs[0].ID, second->Inputs[0].ID),
            "Connecting copied nodes failed.");

    const size_t nodesBeforePaste = fixture.script.main->Graph.GetNodes().size();
    const size_t linksBeforePaste = fixture.script.main->Graph.GetLinks().size();
    RequireSuccess(fixture.operations->CopyNodes(fixture.script.main->ID.id,
                    { static_cast<int>(first->ID.Get()), static_cast<int>(second->ID.Get()) }),
            "Copying nodes failed.");
    std::vector<int> pastedNodes;
    RequireSuccess(fixture.operations->PasteNodes(fixture.script.main->ID.id, pastedNodes),
            "Pasting nodes failed.");
    Require(pastedNodes.size() == 2, "Paste did not recreate both selected nodes.");
    Require(fixture.script.main->Graph.GetNodes().size() == nodesBeforePaste + 2,
            "Pasted nodes were not added to the destination graph.");
    Require(fixture.script.main->Graph.GetLinks().size() == linksBeforePaste + 1,
            "The internal link between pasted nodes was not recreated.");

    std::set<int> uniqueNodeIds;
    for (const NodePtr& node : fixture.script.main->Graph.GetNodes())
        Require(uniqueNodeIds.insert(static_cast<int>(node->ID.Get())).second,
                "Paste duplicated a node ID.");

    RequireSuccess(fixture.operations->Undo(), "Undoing node paste failed.");
    Require(fixture.script.main->Graph.GetNodes().size() == nodesBeforePaste,
            "Undo did not remove the pasted node fragment.");
    RequireSuccess(fixture.operations->Redo(), "Redoing node paste failed.");
    Require(fixture.script.main->Graph.GetNodes().size() == nodesBeforePaste + 2,
            "Redo did not restore the pasted node fragment.");
}

void PastedFunctionsRemapInternalReferences()
{
    OperationsFixture fixture;
    ScriptFunctionPtr worker = fixture.AddWorker();
    NodePtr recursiveCall = worker->functionDef->MakeNode(fixture.ids, worker->ID);
    RequireSuccess(fixture.operations->AddNode(worker->ID.id, recursiveCall),
            "Adding a recursive call node failed.");

    RequireSuccess(fixture.operations->CopyScriptElement(worker->ID.id), "Copying a function failed.");
    int pastedFunctionId = 0;
    RequireSuccess(fixture.operations->PasteScriptElement(fixture.script.main->ID.id, pastedFunctionId),
            "Pasting a function failed.");
    ScriptFunctionPtr pastedFunction = ScriptUtils::FindFunctionById(fixture.script, pastedFunctionId);
    Require(pastedFunction && pastedFunction->Graph.GetNodes().size() == 2,
            "Pasted function did not retain its graph nodes.");
    Require(pastedFunctionId != worker->ID.id, "Pasted function reused its source ID.");
    NodePtr pastedRecursiveCall = pastedFunction->Graph.FindNodeIf([](const NodePtr& node)
    {
        return node->SerializationType == "function.call";
    });
    Require(pastedRecursiveCall && pastedRecursiveCall->refId == pastedFunctionId,
            "An internal function reference was not remapped to the pasted function.");
}

void PastedVariablesUseFreshIds()
{
    OperationsFixture fixture;
    const int variableId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddVariable(variableId, "Count", Value(42.0)),
            "Adding a variable failed.");
    RequireSuccess(fixture.operations->CopyScriptElement(variableId), "Copying a variable failed.");
    int pastedVariableId = 0;
    RequireSuccess(fixture.operations->PasteScriptElement(fixture.script.main->ID.id, pastedVariableId),
            "Pasting a variable failed.");
    Require(pastedVariableId != variableId &&
            ScriptUtils::FindVariableById(fixture.script, pastedVariableId),
            "Pasted variable did not receive a fresh ID.");
}

void FunctionInputsCanBePastedToAnotherFunction()
{
    OperationsFixture fixture;
    ScriptFunctionPtr source = fixture.AddWorker();
    const int targetId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddFunction(targetId, "Target"), "Adding target function failed.");

    const int inputId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddFunctionInput(source->ID.id, inputId, "Value", Value(1.0)),
            "Adding a function input failed.");
    RequireSuccess(fixture.operations->CopyScriptElement(inputId), "Copying a function input failed.");
    int pastedInputId = 0;
    RequireSuccess(fixture.operations->PasteScriptElement(targetId, pastedInputId),
            "Pasting a function input failed.");
    ScriptFunctionPtr target = ScriptUtils::FindFunctionById(fixture.script, targetId);
    Require(target->functionDef->FindInputByID(pastedInputId) != nullptr,
            "Pasted function input was not attached to the target function.");
}

void FunctionOutputsCanBePastedToAnotherFunction()
{
    OperationsFixture fixture;
    ScriptFunctionPtr source = fixture.AddWorker();
    const int targetId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddFunction(targetId, "Target"), "Adding target function failed.");

    const int outputId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddFunctionOutput(source->ID.id, outputId, "Result", Value(2.0)),
            "Adding a function output failed.");
    RequireSuccess(fixture.operations->CopyScriptElement(outputId), "Copying a function output failed.");
    int pastedOutputId = 0;
    RequireSuccess(fixture.operations->PasteScriptElement(targetId, pastedOutputId),
            "Pasting a function output failed.");
    ScriptFunctionPtr target = ScriptUtils::FindFunctionById(fixture.script, targetId);
    Require(target->functionDef->FindOutputByID(pastedOutputId) != nullptr,
            "Pasted function output was not attached to the target function.");
}

void TransactionIsOneUndoStep()
{
    OperationsFixture fixture;
    const CompiledNodeDefPtr addDefinition = fixture.registry.FindCompiled("Math::Add");
    NodePtr source = addDefinition->MakeNode(fixture.ids);
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, source),
            "Adding transaction source node failed.");
    const size_t nodeCount = fixture.script.main->Graph.GetNodes().size();
    const size_t linkCount = fixture.script.main->Graph.GetLinks().size();

    RequireSuccess(fixture.operations->BeginTransaction("Create and connect node"),
            "Starting a grouped operation failed.");
    NodePtr groupedNode = addDefinition->MakeNode(fixture.ids);
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, groupedNode),
            "Adding a node inside a transaction failed.");
    RequireSuccess(fixture.operations->SetNodeState(fixture.script.main->ID.id, groupedNode->ID,
                    "{\"location\": [200, 100]}", true),
            "Positioning a node inside a transaction failed.");
    RequireSuccess(fixture.operations->Connect(fixture.script.main->ID.id, source->Outputs[0].ID,
                    groupedNode->Inputs[0].ID),
            "Connecting inside a transaction failed.");
    RequireSuccess(fixture.operations->CommitTransaction(), "Committing a grouped operation failed.");
    RequireSuccess(fixture.operations->Undo(), "Undoing a grouped operation failed.");
    Require(fixture.script.main->Graph.GetNodes().size() == nodeCount &&
            fixture.script.main->Graph.GetLinks().size() == linkCount,
            "One undo did not revert the complete grouped operation.");
    RequireSuccess(fixture.operations->Redo(), "Redoing a grouped operation failed.");
}

void DanglingVariableReferencesRecover()
{
    OperationsFixture fixture;
    const int variableId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddVariable(variableId, "Count", Value(42.0)),
            "Adding a variable failed.");
    ScriptPropertyPtr variable = ScriptUtils::FindVariableById(fixture.script, variableId);
    NodePtr reference = BuildGetVariableNode(fixture.ids, variable);
    const int referenceId = static_cast<int>(reference->ID.Get());
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, reference),
            "Adding a variable reference failed.");
    RequireSuccess(fixture.operations->RemoveVariable(variableId),
            "Deleting a referenced variable should be allowed.");
    NodePtr missing = fixture.script.main->Graph.FindNode(ed::NodeId(referenceId));
    Require(missing && HasFlag(missing->InstanceFlags, NodeInstanceFlags::Error),
            "A deleted variable did not leave an error on its reference node.");
    RequireSuccess(fixture.operations->Undo(), "Undoing referenced-variable deletion failed.");
    RequireSuccess(fixture.operations->Redo(), "Redoing referenced-variable deletion failed.");
    missing = fixture.script.main->Graph.FindNode(ed::NodeId(referenceId));
    Require(missing && std::dynamic_pointer_cast<GetVariableNode>(missing) &&
            HasFlag(missing->InstanceFlags, NodeInstanceFlags::Error),
            "Redo did not restore a typed dangling variable reference.");

    fixture.script.variables.push_back(variable);
    missing->Refresh(fixture.script, fixture.ids);
    Require(!HasFlag(missing->InstanceFlags, NodeInstanceFlags::Error),
            "GetVariableNode did not recover when its definition became available.");
}

void FunctionVariablesSupportHistoryClipboardAndScope()
{
    OperationsFixture fixture;
    ScriptFunctionPtr worker = fixture.AddWorker();
    const int functionId = worker->ID.id;
    const int variableId = fixture.ids.GetNextId();
    RequireSuccess(fixture.operations->AddFunctionVariable(functionId, variableId, "Counter", Value(3.0)),
            "Adding a local variable failed.");
    RequireSuccess(fixture.operations->RenameFunctionVariable(functionId, variableId, "Local Counter"),
            "Renaming a local variable failed.");
    RequireSuccess(fixture.operations->RemoveFunctionVariable(functionId, variableId),
            "Removing a local variable failed.");
    RequireSuccess(fixture.operations->Undo(), "Undoing local-variable removal failed.");

    worker = ScriptUtils::FindFunctionById(fixture.script, functionId);
    ScriptPropertyPtr variable = ScriptUtils::FindFunctionVariableById(worker, variableId);
    Require(variable && variable->Name == "Local Counter",
            "Undo did not restore the local variable to its owning function.");
    Require(!ScriptUtils::FindVariableById(fixture.script, variableId),
            "A function local leaked into the global variable collection.");

    NodePtr reference = BuildGetVariableNode(fixture.ids, variable, ScriptElementID::Invalid, worker->ID);
    RequireSuccess(fixture.operations->AddNode(functionId, reference),
            "Adding a local-variable reference failed.");

    RequireSuccess(fixture.operations->CopyScriptElement(variableId),
            "Copying a local variable failed.");
    int pastedVariableId = 0;
    RequireSuccess(fixture.operations->PasteScriptElement(functionId, pastedVariableId),
            "Pasting a local variable failed.");
    Require(pastedVariableId != variableId &&
            ScriptUtils::FindFunctionVariableById(worker, pastedVariableId),
            "A pasted local variable did not receive a fresh ID in the target function.");

    RequireSuccess(fixture.operations->CopyScriptElement(functionId),
            "Copying a function with locals failed.");
    int pastedFunctionId = 0;
    RequireSuccess(fixture.operations->PasteScriptElement(fixture.script.main->ID.id, pastedFunctionId),
            "Pasting a function with locals failed.");
    ScriptFunctionPtr pastedFunction = ScriptUtils::FindFunctionById(fixture.script, pastedFunctionId);
    Require(pastedFunction && !pastedFunction->variables.empty(),
            "Function cloning omitted local variables.");
    NodePtr pastedReference = pastedFunction->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->SerializationType == "variable.get"; });
    Require(pastedReference && pastedReference->refId != variableId &&
            ScriptUtils::FindFunctionVariableById(pastedFunction, pastedReference->refId.id) &&
            !HasFlag(pastedReference->InstanceFlags, NodeInstanceFlags::Error),
            "Function cloning did not remap and resolve its local-variable references.");

    RequireSuccess(fixture.operations->CopyNodes(functionId, { static_cast<int>(reference->ID.Get()) }),
            "Copying a local-variable node failed.");
    std::vector<int> pastedNodes;
    Require(!fixture.operations->PasteNodes(fixture.script.main->ID.id, pastedNodes),
            "A local-variable node was pasted outside its owning function.");
}

void DanglingFunctionReferencesRecover()
{
    OperationsFixture fixture;
    ScriptFunctionPtr worker = fixture.AddWorker();
    NodePtr reference = worker->functionDef->MakeNode(fixture.ids, worker->ID);
    const int referenceId = static_cast<int>(reference->ID.Get());
    RequireSuccess(fixture.operations->AddNode(fixture.script.main->ID.id, reference),
            "Adding a function reference failed.");
    RequireSuccess(fixture.operations->RemoveFunction(worker->ID.id),
            "Deleting a referenced function should be allowed.");
    NodePtr missing = fixture.script.main->Graph.FindNode(ed::NodeId(referenceId));
    Require(missing && HasFlag(missing->InstanceFlags, NodeInstanceFlags::Error),
            "A deleted function did not leave an error on its reference node.");
    RequireSuccess(fixture.operations->Undo(), "Undoing referenced-function deletion failed.");
    RequireSuccess(fixture.operations->Redo(), "Redoing referenced-function deletion failed.");
    missing = fixture.script.main->Graph.FindNode(ed::NodeId(referenceId));
    Require(missing && HasFlag(missing->InstanceFlags, NodeInstanceFlags::Error),
            "Redo could not deserialize a dangling function reference.");

    fixture.script.functions.push_back(worker);
    missing->Refresh(fixture.script, fixture.ids);
    Require(!HasFlag(missing->InstanceFlags, NodeInstanceFlags::Error),
            "The function-call node did not recover when its definition became available.");
}
}

void AddDocumentOperationsTests(Tests::Runner& runner)
{
    runner.Group("Document operations / history", [&]()
    {
        runner.Test("required Begin node cannot be deleted", RequiredBeginCannotBeDeleted);
        runner.Test("Main signature cannot be edited", MainSignatureCannotBeEdited);
        runner.Test("node state can be undone and redone", NodeStateCanBeUndoneAndRedone);
        runner.Test("compiled dynamic inputs can be undone and redone", CompiledDynamicInputsCanBeUndoneAndRedone);
        runner.Test("MakeList type overrides can be undone and redone",
            MakeListTypeOverrideCanBeUndoneAndRedone);
        runner.Test("function changes can be undone and redone", FunctionChangesCanBeUndoneAndRedone);
        runner.Test("marking a function pure makes calls implicit",
            MarkingFunctionPureMakesCallsImplicit);
        runner.Test("toString shortcut creates a complete method",
            ToStringShortcutCreatesCompleteMethod);
        runner.Test("a transaction is one undo step", TransactionIsOneUndoStep);
    });
    runner.Group("Document operations / clipboard", [&]()
    {
        runner.Test("node fragments preserve links and use fresh IDs", NodeFragmentsPreserveLinksAndUseFreshIds);
        runner.Test("pasted functions remap internal references", PastedFunctionsRemapInternalReferences);
        runner.Test("pasted variables use fresh IDs", PastedVariablesUseFreshIds);
        runner.Test("function inputs can be pasted to another function", FunctionInputsCanBePastedToAnotherFunction);
        runner.Test("function outputs can be pasted to another function", FunctionOutputsCanBePastedToAnotherFunction);
        runner.Test("function variables preserve history, clipboard, and scope",
            FunctionVariablesSupportHistoryClipboardAndScope);
    });
    runner.Group("Document operations / dangling references", [&]()
    {
        runner.Test("dangling variable references recover", DanglingVariableReferencesRecover);
        runner.Test("dangling function references recover", DanglingFunctionReferencesRecover);
    });
}
