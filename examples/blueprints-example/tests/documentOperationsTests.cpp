#include "documentOperationsTests.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/variable.h"
#include "../operations/documentOperations.h"
#include "../runtime/standardLibrary.h"

#include <Vm.h>

#include <set>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Require(const OperationResult& result, const char* message)
{
    if (!result) throw std::runtime_error(std::string(message) + " " + result.error);
}

void Attach(Graph& graph, const NodePtr& node)
{
    NodeUtils::BuildNode(node);
    graph.AddNode(node);
}
}

int RunDocumentOperationsTests()
{
    try
    {
        VM& vm = VM::getInstance();
        const bool wasGcAllowed = vm.isGarbageCollectionAllowed();
        vm.allowGarbageCollection(false);

        NodeRegistry registry;
        RegisterStandardLibrary(registry);
        registry.RegisterNatives(vm);

        IDGenerator ids;
        Script script;
        script.ID = ids.GetNextId();
        script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
        NodePtr begin = BuildBeginNode(ids, script.main);
        Attach(script.main->Graph, begin);

        DocumentOperations operations(script, ids, registry);
        Require(!operations.RemoveNode(script.main->ID.id, begin->ID),
                "The required Begin node must be protected from deletion.");
        Require(script.main->Graph.GetNodes().size() == 1,
                "Deleting a protected node changed the graph.");
        Require(operations.SetNodeState(script.main->ID.id, begin->ID,
                                        "{\"location\": [10, 20]}", true),
                "Recording a node position failed.");
        Require(operations.Undo(), "Undoing a node position failed.");
        begin = script.main->Graph.GetNodes().front();
        Require(begin->State.empty(), "Undo did not restore the previous node position.");
        Require(operations.Redo(), "Redoing a node position failed.");
        begin = script.main->Graph.GetNodes().front();
        Require(!begin->State.empty(), "Redo did not restore the node position.");

        const int functionId = ids.GetNextId();
        Require(operations.AddFunction(functionId, "Worker"), "Adding a function failed.");
        Require(script.functions.size() == 1, "The function was not added.");
        Require(operations.Undo(), "Undoing a script-data operation failed.");
        Require(script.functions.empty(), "Undo did not restore the function list.");
        Require(operations.Redo(), "Redoing a script-data operation failed.");
        Require(script.functions.size() == 1, "Redo did not restore the function.");
        ScriptFunctionPtr worker = ScriptUtils::FindFunctionById(script, functionId);
        NodePtr recursiveCall = worker->functionDef->MakeNode(ids, worker->ID);
        Require(operations.AddNode(functionId, recursiveCall), "Adding a recursive call node failed.");

        const CompiledNodeDefPtr addDefinition = registry.FindCompiled("Math::Add");
        Require(static_cast<bool>(addDefinition), "Math::Add is not registered.");
        NodePtr first = addDefinition->MakeNode(ids);
        NodePtr second = addDefinition->MakeNode(ids);
        Require(operations.AddNode(script.main->ID.id, first), "Adding the first node failed.");
        Require(operations.AddNode(script.main->ID.id, second), "Adding the second node failed.");
        Require(operations.Connect(script.main->ID.id, first->Outputs[0].ID, second->Inputs[0].ID),
                "Connecting copied nodes failed.");

        const size_t nodesBeforePaste = script.main->Graph.GetNodes().size();
        const size_t linksBeforePaste = script.main->Graph.GetLinks().size();
        Require(operations.CopyNodes(script.main->ID.id,
                { static_cast<int>(first->ID.Get()), static_cast<int>(second->ID.Get()) }),
                "Copying nodes failed.");
        std::vector<int> pastedNodes;
        Require(operations.PasteNodes(script.main->ID.id, pastedNodes), "Pasting nodes failed.");
        Require(pastedNodes.size() == 2, "Paste did not recreate both selected nodes.");
        Require(script.main->Graph.GetNodes().size() == nodesBeforePaste + 2,
                "Pasted nodes were not added to the destination graph.");
        Require(script.main->Graph.GetLinks().size() == linksBeforePaste + 1,
                "The internal link between pasted nodes was not recreated.");
        std::set<int> uniqueNodeIds;
        for (const NodePtr& node : script.main->Graph.GetNodes())
            Require(uniqueNodeIds.insert(node->ID.Get()).second, "Paste duplicated a node ID.");

        Require(operations.Undo(), "Undoing node paste failed.");
        Require(script.main->Graph.GetNodes().size() == nodesBeforePaste,
                "Undo did not remove the pasted node fragment.");
        Require(operations.Redo(), "Redoing node paste failed.");
        Require(script.main->Graph.GetNodes().size() == nodesBeforePaste + 2,
                "Redo did not restore the pasted node fragment.");

        Require(operations.CopyScriptElement(functionId), "Copying a function failed.");
        int pastedFunctionId = 0;
        Require(operations.PasteScriptElement(script.main->ID.id, pastedFunctionId),
                "Pasting a function failed.");
        ScriptFunctionPtr pastedFunction = ScriptUtils::FindFunctionById(script, pastedFunctionId);
        Require(pastedFunction && pastedFunction->Graph.GetNodes().size() == 2,
                "Pasted function did not retain its graph nodes.");
        Require(pastedFunctionId != functionId, "Pasted function reused its source ID.");
        NodePtr pastedRecursiveCall = pastedFunction->Graph.FindNodeIf([&](const NodePtr& node)
        {
            return node->SerializationType == "function.call";
        });
        Require(pastedRecursiveCall && pastedRecursiveCall->refId == pastedFunctionId,
                "An internal function reference was not remapped to the pasted function.");

        const int variableId = ids.GetNextId();
        Require(operations.AddVariable(variableId, "Count", Value(42.0)), "Adding a variable failed.");
        Require(operations.CopyScriptElement(variableId), "Copying a variable failed.");
        int pastedVariableId = 0;
        Require(operations.PasteScriptElement(script.main->ID.id, pastedVariableId),
                "Pasting a variable failed.");
        Require(pastedVariableId != variableId && ScriptUtils::FindVariableById(script, pastedVariableId),
                "Pasted variable did not receive a fresh ID.");

        const int inputId = ids.GetNextId();
        Require(operations.AddFunctionInput(functionId, inputId, "Value", Value(1.0)),
                "Adding a function input failed.");
        Require(operations.CopyScriptElement(inputId), "Copying a function input failed.");
        int pastedInputId = 0;
        Require(operations.PasteScriptElement(pastedFunctionId, pastedInputId),
                "Pasting a function input failed.");
        Require(pastedFunction->functionDef->FindInputByID(pastedInputId) != nullptr,
                "Pasted function input was not attached to the target function.");

        const int outputId = ids.GetNextId();
        Require(operations.AddFunctionOutput(functionId, outputId, "Result", Value(2.0)),
                "Adding a function output failed.");
        Require(operations.CopyScriptElement(outputId), "Copying a function output failed.");
        int pastedOutputId = 0;
        Require(operations.PasteScriptElement(pastedFunctionId, pastedOutputId),
                "Pasting a function output failed.");
        Require(pastedFunction->functionDef->FindOutputByID(pastedOutputId) != nullptr,
                "Pasted function output was not attached to the target function.");

        const size_t groupedNodeCount = script.main->Graph.GetNodes().size();
        const size_t groupedLinkCount = script.main->Graph.GetLinks().size();
        Require(operations.BeginTransaction("Create and connect node"),
                "Starting a grouped operation failed.");
        NodePtr groupedNode = addDefinition->MakeNode(ids);
        Require(operations.AddNode(script.main->ID.id, groupedNode),
                "Adding a node inside a transaction failed.");
        Require(operations.SetNodeState(script.main->ID.id, groupedNode->ID,
                                        "{\"location\": [200, 100]}", true),
                "Positioning a node inside a transaction failed.");
        NodePtr groupedSource = script.main->Graph.FindNode(ed::NodeId(pastedNodes.front()));
        Require(groupedSource != nullptr, "The grouped-operation source node is missing.");
        Require(operations.Connect(script.main->ID.id, groupedSource->Outputs[0].ID,
                                   groupedNode->Inputs[0].ID),
                "Connecting inside a transaction failed.");
        Require(operations.CommitTransaction(), "Committing a grouped operation failed.");
        Require(operations.Undo(), "Undoing a grouped operation failed.");
        Require(script.main->Graph.GetNodes().size() == groupedNodeCount &&
                script.main->Graph.GetLinks().size() == groupedLinkCount,
                "One undo did not revert the complete grouped operation.");
        Require(operations.Redo(), "Redoing a grouped operation failed.");

        ScriptPropertyPtr referencedVariable = ScriptUtils::FindVariableById(script, variableId);
        NodePtr variableReference = BuildGetVariableNode(ids, referencedVariable);
        const int variableReferenceId = static_cast<int>(variableReference->ID.Get());
        Require(operations.AddNode(script.main->ID.id, variableReference),
                "Adding a variable reference failed.");
        Require(operations.RemoveVariable(variableId),
                "Deleting a referenced variable should be allowed.");
        NodePtr missingVariable = script.main->Graph.FindNode(ed::NodeId(variableReferenceId));
        Require(missingVariable && HasFlag(missingVariable->InstanceFlags, NodeInstanceFlags::Error),
                "A deleted variable did not leave an error on its reference node.");
        Require(operations.Undo(), "Undoing referenced-variable deletion failed.");
        Require(operations.Redo(), "Redoing referenced-variable deletion failed.");
        missingVariable = script.main->Graph.FindNode(ed::NodeId(variableReferenceId));
        Require(missingVariable && HasFlag(missingVariable->InstanceFlags, NodeInstanceFlags::Error),
                "Redo could not deserialize a dangling variable reference.");
        Require(std::dynamic_pointer_cast<GetVariableNode>(missingVariable) != nullptr,
                "A dangling variable reference was not deserialized as GetVariableNode.");
        script.variables.push_back(referencedVariable);
        missingVariable->Refresh(script, ids);
        Require(!HasFlag(missingVariable->InstanceFlags, NodeInstanceFlags::Error),
                "GetVariableNode did not recover when its definition became available.");
        script.variables.pop_back();
        missingVariable->Refresh(script, ids);

        worker = ScriptUtils::FindFunctionById(script, functionId);
        NodePtr functionReference = worker->functionDef->MakeNode(ids, worker->ID);
        const int functionReferenceId = static_cast<int>(functionReference->ID.Get());
        Require(operations.AddNode(script.main->ID.id, functionReference),
                "Adding a function reference failed.");
        Require(operations.RemoveFunction(functionId),
                "Deleting a referenced function should be allowed.");
        NodePtr missingFunction = script.main->Graph.FindNode(ed::NodeId(functionReferenceId));
        Require(missingFunction && HasFlag(missingFunction->InstanceFlags, NodeInstanceFlags::Error),
                "A deleted function did not leave an error on its reference node.");
        Require(operations.Undo(), "Undoing referenced-function deletion failed.");
        Require(operations.Redo(), "Redoing referenced-function deletion failed.");
        missingFunction = script.main->Graph.FindNode(ed::NodeId(functionReferenceId));
        Require(missingFunction && HasFlag(missingFunction->InstanceFlags, NodeInstanceFlags::Error),
                "Redo could not deserialize a dangling function reference.");
        script.functions.push_back(worker);
        missingFunction->Refresh(script, ids);
        Require(!HasFlag(missingFunction->InstanceFlags, NodeInstanceFlags::Error),
                "The function-call node did not recover when its definition became available.");
        script.functions.pop_back();
        missingFunction->Refresh(script, ids);

        vm.allowGarbageCollection(wasGcAllowed);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Document operations test failed: " << exception.what() << '\n';
        return 1;
    }
}
