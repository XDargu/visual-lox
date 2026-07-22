#include "documentOperations.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../script/scriptSerializer.h"
#include "../utilities/utils.h"

#include <algorithm>

namespace
{
OperationResult Missing(const char* type, int id)
{
    return OperationResult::Fail(std::string(type) + " " + std::to_string(id) + " was not found.");
}

template<class Range, class Name>
std::string UniqueName(const std::string& requested, const Range& range, Name name)
{
    std::string candidate = requested;
    int suffix = 1;
    const auto exists = [&](const std::string& value)
    {
        return std::any_of(range.begin(), range.end(), [&](const auto& item) { return name(item) == value; });
    };
    while (exists(candidate))
        candidate = requested + std::to_string(suffix++);
    return candidate;
}
}

DocumentOperations::DocumentOperations(Script& script, IDGenerator& ids, const NodeRegistry& registry)
    : m_script(script), m_ids(ids), m_registry(registry)
{
}

ScriptFunctionPtr DocumentOperations::FindFunction(int id) const
{
    if (m_script.main && m_script.main->ID == id)
        return m_script.main;
    return ScriptUtils::FindFunctionById(m_script, id);
}

OperationResult DocumentOperations::Apply(const std::string& label,
                                           const std::function<OperationResult()>& mutation)
{
    if (IsTransactionActive())
    {
        OperationResult result = mutation();
        if (!result)
            CancelTransaction();
        else
            ++m_revision;
        return result;
    }

    std::string before;
    SerializationResult serialized = ScriptSerializer::SerializeToString(m_script, before);
    if (!serialized)
        return OperationResult::Fail("Could not start '" + label + "': " + serialized.error);

    OperationResult result = mutation();
    if (!result)
    {
        std::string current;
        const SerializationResult currentResult = ScriptSerializer::SerializeToString(m_script, current);
        if (!currentResult || current != before)
            Restore(before);
        return result;
    }

    std::string after;
    serialized = ScriptSerializer::SerializeToString(m_script, after);
    if (!serialized)
    {
        const std::string failure = serialized.error;
        Restore(before);
        return OperationResult::Fail("Could not finish '" + label + "': " + failure);
    }

    if (before == after)
        return OperationResult::Ok();

    m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_historyCursor), m_history.end());
    m_history.push_back({ label, std::move(before), std::move(after) });
    m_historyCursor = m_history.size();
    ++m_revision;
    return OperationResult::Ok();
}

OperationResult DocumentOperations::BeginTransaction(const std::string& label)
{
    if (m_transactionDepth++ > 0)
        return OperationResult::Ok();
    m_transactionLabel = label;
    SerializationResult result = ScriptSerializer::SerializeToString(m_script, m_transactionBefore);
    if (!result)
    {
        m_transactionDepth = 0;
        m_transactionLabel.clear();
        return OperationResult::Fail(result.error);
    }
    return OperationResult::Ok();
}

OperationResult DocumentOperations::CommitTransaction()
{
    if (!IsTransactionActive())
        return OperationResult::Fail("There is no active transaction.");
    if (--m_transactionDepth > 0)
        return OperationResult::Ok();

    std::string after;
    SerializationResult result = ScriptSerializer::SerializeToString(m_script, after);
    if (!result)
    {
        const std::string error = result.error;
        Restore(m_transactionBefore);
        m_transactionBefore.clear();
        m_transactionLabel.clear();
        return OperationResult::Fail(error);
    }
    if (after != m_transactionBefore)
    {
        m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_historyCursor), m_history.end());
        m_history.push_back({ m_transactionLabel, std::move(m_transactionBefore), std::move(after) });
        m_historyCursor = m_history.size();
        ++m_revision;
    }
    m_transactionBefore.clear();
    m_transactionLabel.clear();
    return OperationResult::Ok();
}

OperationResult DocumentOperations::CancelTransaction()
{
    if (!IsTransactionActive())
        return OperationResult::Fail("There is no active transaction.");
    const std::string before = std::move(m_transactionBefore);
    m_transactionDepth = 0;
    m_transactionLabel.clear();
    return Restore(before);
}

OperationResult DocumentOperations::Restore(const std::string& snapshot)
{
    SerializationResult result = ScriptSerializer::DeserializeFromString(snapshot, m_registry, m_script, m_ids);
    if (!result)
        return OperationResult::Fail(result.error);
    ++m_revision;
    return OperationResult::Ok();
}

OperationResult DocumentOperations::AddNode(int functionId, const NodePtr& node)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    if (!node) return OperationResult::Fail("Cannot add an empty node.");
    if (function->Graph.FindNode(node->ID))
        return OperationResult::Fail("A node with this ID already exists.");
    return Apply("Add node", [&]
    {
        NodeUtils::BuildNode(node);
        function->Graph.AddNode(node);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RemoveNode(int functionId, ed::NodeId nodeId)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    NodePtr node = function->Graph.FindNode(nodeId);
    if (!node) return Missing("Node", nodeId.Get());
    if (HasFlag(node->DefinitionFlags, NodeDefinitionFlags::Protected))
        return OperationResult::Fail("This node is required by its function and cannot be deleted.");
    return Apply("Delete node", [&]
    {
        return function->Graph.DeleteNode(nodeId)
            ? OperationResult::Ok()
            : OperationResult::Fail("The node could not be deleted.");
    });
}

OperationResult DocumentOperations::Connect(int functionId, ed::PinId first, ed::PinId second,
                                             const std::vector<ProcessedNode>& processedNodes)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    Pin* a = function->Graph.FindPin(first);
    Pin* b = function->Graph.FindPin(second);
    const ELinkQueryResult query = function->Graph.CanCreateLink(a, b, processedNodes);
    if (query != ELinkQueryResult::Possible)
        return OperationResult::Fail(LinkQueryResultToString(query));
    return Apply("Connect pins", [&]
    {
        Pin* input = a->Kind == PinKind::Input ? a : b;
        Pin* output = a->Kind == PinKind::Output ? a : b;
        Link link{ ed::LinkId(m_ids.GetNextId()), output->ID, input->ID };
        link.Color = GetIconColor(output->Type);
        function->Graph.AddLink(link);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::Disconnect(int functionId, ed::LinkId linkId)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    // Node deletion removes attached links as part of the same operation. The
    // node editor may still report those links in its deletion queue.
    if (!function->Graph.FindLink(linkId)) return OperationResult::Ok();
    return Apply("Disconnect pins", [&]
    {
        function->Graph.DeleteLink(linkId);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::ChangeNodeInputValue(int functionId, ed::NodeId nodeId,
                                                          int inputIndex, const Value& value)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    NodePtr node = function ? function->Graph.FindNode(nodeId) : nullptr;
    if (!node) return Missing("Node", nodeId.Get());
    if (inputIndex < 0 || inputIndex >= static_cast<int>(node->InputValues.size()))
        return OperationResult::Fail("The node input index is invalid.");
    return Apply("Change node input", [&]
    {
        node->InputValues[inputIndex] = value;
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::AddDynamicInput(int functionId, ed::NodeId nodeId)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    NodePtr node = function ? function->Graph.FindNode(nodeId) : nullptr;
    if (!node) return Missing("Node", nodeId.Get());
    if (!node->CanAddInput()) return OperationResult::Fail("This node cannot add another input.");
    return Apply("Add node input", [&]
    {
        node->AddInput(m_ids);
        NodeUtils::BuildNode(node);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RemoveDynamicInput(int functionId, ed::NodeId nodeId, ed::PinId pinId)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    NodePtr node = function ? function->Graph.FindNode(nodeId) : nullptr;
    if (!node) return Missing("Node", nodeId.Get());
    if (!node->CanRemoveInput(pinId)) return OperationResult::Fail("This node input cannot be removed.");
    return Apply("Delete node input", [&]
    {
        std::vector<ed::LinkId> links;
        for (const Link& link : function->Graph.GetLinks())
            if (link.StartPinID == pinId || link.EndPinID == pinId) links.push_back(link.ID);
        for (ed::LinkId link : links) function->Graph.DeleteLink(link);
        node->RemoveInput(pinId);
        NodeUtils::BuildNode(node);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::SetNodeState(int functionId, ed::NodeId nodeId,
                                                  const std::string& state, bool recordHistory,
                                                  bool coalesceHistory,
                                                  bool amendPreviousTransaction)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    NodePtr node = function ? function->Graph.FindNode(nodeId) : nullptr;
    if (!node) return Missing("Node", nodeId.Get());
    if (node->State == state)
        return OperationResult::Ok();
    if (IsTransactionActive())
    {
        node->State = state;
        ++m_revision;
        return OperationResult::Ok();
    }
    if (!recordHistory)
    {
        node->State = state;
        ++m_revision;
        return OperationResult::Ok();
    }

    std::string before;
    SerializationResult serialized = ScriptSerializer::SerializeToString(m_script, before);
    if (!serialized) return OperationResult::Fail(serialized.error);
    node->State = state;

    std::string after;
    serialized = ScriptSerializer::SerializeToString(m_script, after);
    if (!serialized)
    {
        Restore(before);
        return OperationResult::Fail(serialized.error);
    }

    // Node-editor reports position continuously while dragging. Merge adjacent
    // position callbacks into one transaction until another operation occurs.
    static const std::string label = "Move nodes";
    if (amendPreviousTransaction && m_historyCursor == m_history.size() &&
        !m_history.empty() && m_history.back().label == "Create node")
    {
        m_history.back().after = std::move(after);
    }
    else if (coalesceHistory && m_historyCursor == m_history.size() &&
        !m_history.empty() && m_history.back().label == label)
    {
        m_history.back().after = std::move(after);
    }
    else
    {
        m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_historyCursor), m_history.end());
        m_history.push_back({ label, std::move(before), std::move(after) });
        m_historyCursor = m_history.size();
    }
    ++m_revision;
    return OperationResult::Ok();
}

OperationResult DocumentOperations::CopyNodes(int functionId, const std::vector<int>& nodeIds)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    std::vector<int> copyable;
    for (int id : nodeIds)
    {
        NodePtr node = function->Graph.FindNode(ed::NodeId(id));
        if (node && !HasFlag(node->DefinitionFlags, NodeDefinitionFlags::Protected))
            copyable.push_back(id);
    }
    if (copyable.empty())
        return OperationResult::Fail("The selection contains no copyable nodes.");
    std::string document;
    SerializationResult result = ScriptSerializer::SerializeToString(m_script, document);
    if (!result) return OperationResult::Fail(result.error);
    m_clipboard = { ClipboardKind::Nodes, std::move(document), functionId, std::move(copyable) };
    return OperationResult::Ok();
}

OperationResult DocumentOperations::PasteNodes(int functionId, std::vector<int>& pastedNodeIds)
{
    if (m_clipboard.kind != ClipboardKind::Nodes)
        return OperationResult::Fail("The clipboard does not contain nodes.");
    if (!FindFunction(functionId)) return Missing("Function", functionId);
    Script source;
    IDGenerator sourceIds;
    SerializationResult loaded = ScriptSerializer::DeserializeFromString(
        m_clipboard.document, m_registry, source, sourceIds);
    if (!loaded) return OperationResult::Fail("Clipboard data is invalid: " + loaded.error);
    return Apply("Paste nodes", [&]
    {
        SerializationResult cloned = ScriptSerializer::CloneNodes(
            source, m_clipboard.ownerId, m_clipboard.elementIds, m_registry,
            m_script, functionId, m_ids, pastedNodeIds);
        return cloned ? OperationResult::Ok() : OperationResult::Fail(cloned.error);
    });
}

OperationResult DocumentOperations::CopyScriptElement(int elementId)
{
    ClipboardKind kind = ClipboardKind::None;
    int ownerId = 0;
    if (ScriptUtils::FindFunctionById(m_script, elementId))
        kind = ClipboardKind::Function;
    else if (ScriptUtils::FindVariableById(m_script, elementId))
        kind = ClipboardKind::Variable;
    else
    {
        std::vector<ScriptFunctionPtr> functions = m_script.functions;
        if (m_script.main) functions.push_back(m_script.main);
        for (const ScriptFunctionPtr& function : functions)
        {
            if (function->functionDef->FindInputByID(elementId))
            {
                kind = ClipboardKind::FunctionInput;
                ownerId = function->ID.id;
                break;
            }
            if (function->functionDef->FindOutputByID(elementId))
            {
                kind = ClipboardKind::FunctionOutput;
                ownerId = function->ID.id;
                break;
            }
        }
    }
    if (kind == ClipboardKind::None)
        return OperationResult::Fail("The selected script item cannot be copied.");
    std::string document;
    SerializationResult result = ScriptSerializer::SerializeToString(m_script, document);
    if (!result) return OperationResult::Fail(result.error);
    m_clipboard = { kind, std::move(document), ownerId, { elementId } };
    return OperationResult::Ok();
}

OperationResult DocumentOperations::PasteScriptElement(int targetFunctionId, int& pastedElementId)
{
    if (m_clipboard.kind == ClipboardKind::None || m_clipboard.kind == ClipboardKind::Nodes)
        return OperationResult::Fail("The clipboard does not contain script data.");
    Script source;
    IDGenerator sourceIds;
    SerializationResult loaded = ScriptSerializer::DeserializeFromString(
        m_clipboard.document, m_registry, source, sourceIds);
    if (!loaded) return OperationResult::Fail("Clipboard data is invalid: " + loaded.error);

    return Apply("Paste script data", [&]
    {
        SerializationResult cloned;
        switch (m_clipboard.kind)
        {
        case ClipboardKind::Function:
            cloned = ScriptSerializer::CloneFunction(source, m_clipboard.elementIds.front(),
                                                       m_registry, m_script, m_ids, pastedElementId);
            if (cloned)
            {
                ScriptFunctionPtr function = ScriptUtils::FindFunctionById(m_script, pastedElementId);
                std::vector<ScriptFunctionPtr> others;
                for (const ScriptFunctionPtr& item : m_script.functions)
                    if (item->ID != pastedElementId) others.push_back(item);
                function->functionDef->name = UniqueName(function->functionDef->name, others,
                    [](const ScriptFunctionPtr& item) { return item->functionDef->name; });
            }
            break;
        case ClipboardKind::Variable:
            cloned = ScriptSerializer::CloneVariable(source, m_clipboard.elementIds.front(),
                                                       m_script, m_ids, pastedElementId);
            if (cloned)
            {
                ScriptPropertyPtr variable = ScriptUtils::FindVariableById(m_script, pastedElementId);
                std::vector<ScriptPropertyPtr> others;
                for (const ScriptPropertyPtr& item : m_script.variables)
                    if (item->ID != pastedElementId) others.push_back(item);
                variable->Name = UniqueName(variable->Name, others,
                    [](const ScriptPropertyPtr& item) { return item->Name; });
            }
            break;
        case ClipboardKind::FunctionInput:
        case ClipboardKind::FunctionOutput:
        {
            ScriptFunctionPtr target = FindFunction(targetFunctionId);
            if (!target) return Missing("Function", targetFunctionId);
            const bool output = m_clipboard.kind == ClipboardKind::FunctionOutput;
            cloned = ScriptSerializer::CloneFunctionPort(source, m_clipboard.ownerId,
                m_clipboard.elementIds.front(), output, m_script, targetFunctionId, m_ids, pastedElementId);
            if (cloned)
            {
                auto& ports = output ? target->functionDef->outputs : target->functionDef->inputs;
                BasicFunctionDef::Input* port = output
                    ? target->functionDef->FindOutputByID(pastedElementId)
                    : target->functionDef->FindInputByID(pastedElementId);
                std::vector<BasicFunctionDef::Input> others;
                for (const auto& item : ports) if (item.id != pastedElementId) others.push_back(item);
                port->name = UniqueName(port->name, others,
                    [](const BasicFunctionDef::Input& item) { return item.name; });
                ScriptUtils::RefreshFunctionRefs(m_script, targetFunctionId, m_ids);
            }
            break;
        }
        default: return OperationResult::Fail("Unsupported clipboard data.");
        }
        return cloned ? OperationResult::Ok() : OperationResult::Fail(cloned.error);
    });
}

OperationResult DocumentOperations::AddFunction(int id, const std::string& name)
{
    if (FindFunction(id)) return OperationResult::Fail("A function with this ID already exists.");
    return Apply("Add function", [&]
    {
        ScriptFunctionPtr function = std::make_shared<ScriptFunction>(id, name.c_str());
        NodePtr begin = BuildBeginNode(m_ids, function);
        NodeUtils::BuildNode(begin);
        function->Graph.AddNode(begin);
        m_script.functions.push_back(function);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RemoveFunction(int id)
{
    ScriptFunctionPtr function = ScriptUtils::FindFunctionById(m_script, id);
    if (!function) return Missing("Function", id);
    return Apply("Delete function", [&]
    {
        stl::erase_if(m_script.functions, [&](const ScriptFunctionPtr& item) { return item->ID == id; });
        ScriptUtils::RefreshFunctionRefs(m_script, id, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RenameFunction(int id, const std::string& name)
{
    ScriptFunctionPtr function = FindFunction(id);
    if (!function) return Missing("Function", id);
    return Apply("Rename function", [&]
    {
        function->functionDef->name = name;
        ScriptUtils::RefreshFunctionRefs(m_script, id, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::AddVariable(int id, const std::string& name, const Value& value)
{
    if (ScriptUtils::FindVariableById(m_script, id))
        return OperationResult::Fail("A variable with this ID already exists.");
    return Apply("Add variable", [&]
    {
        ScriptPropertyPtr variable = std::make_shared<ScriptProperty>(id, name.c_str());
        variable->defaultValue = value;
        m_script.variables.push_back(variable);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RemoveVariable(int id)
{
    if (!ScriptUtils::FindVariableById(m_script, id)) return Missing("Variable", id);
    return Apply("Delete variable", [&]
    {
        stl::erase_if(m_script.variables, [&](const ScriptPropertyPtr& item) { return item->ID == id; });
        ScriptUtils::RefreshVariableRefs(m_script, id, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RenameVariable(int id, const std::string& name)
{
    ScriptPropertyPtr variable = ScriptUtils::FindVariableById(m_script, id);
    if (!variable) return Missing("Variable", id);
    return Apply("Rename variable", [&]
    {
        variable->Name = name;
        ScriptUtils::RefreshVariableRefs(m_script, id, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::ChangeVariableValue(int id, const Value& value)
{
    ScriptPropertyPtr variable = ScriptUtils::FindVariableById(m_script, id);
    if (!variable) return Missing("Variable", id);
    return Apply("Change variable value", [&]
    {
        variable->defaultValue = value;
        ScriptUtils::RefreshVariableRefs(m_script, id, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::AddFunctionInput(int functionId, int inputId, const std::string& name,
                                                      const Value& value)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    return Apply("Add function input", [&]
    {
        function->functionDef->inputs.push_back({ name, value, inputId });
        ScriptUtils::RefreshFunctionRefs(m_script, functionId, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RemoveFunctionInput(int functionId, int inputId)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    if (!function->functionDef->FindInputByID(inputId)) return Missing("Function input", inputId);
    return Apply("Delete function input", [&]
    {
        stl::erase_if(function->functionDef->inputs, [&](const BasicFunctionDef::Input& port) { return port.id == inputId; });
        ScriptUtils::RefreshFunctionRefs(m_script, functionId, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RenameFunctionInput(int functionId, int inputId, const std::string& name)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    BasicFunctionDef::Input* port = function ? function->functionDef->FindInputByID(inputId) : nullptr;
    if (!port) return Missing("Function input", inputId);
    return Apply("Rename function input", [&]
    {
        port->name = name;
        ScriptUtils::RefreshFunctionRefs(m_script, functionId, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::ChangeFunctionInputValue(int functionId, int inputId, const Value& value)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    BasicFunctionDef::Input* port = function ? function->functionDef->FindInputByID(inputId) : nullptr;
    if (!port) return Missing("Function input", inputId);
    return Apply("Change function input", [&]
    {
        port->value = value;
        ScriptUtils::RefreshFunctionRefs(m_script, functionId, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::AddFunctionOutput(int functionId, int outputId, const std::string& name,
                                                       const Value& value)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    return Apply("Add function output", [&]
    {
        function->functionDef->outputs.push_back({ name, value, outputId });
        ScriptUtils::RefreshFunctionRefs(m_script, functionId, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RemoveFunctionOutput(int functionId, int outputId)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    if (!function) return Missing("Function", functionId);
    if (!function->functionDef->FindOutputByID(outputId)) return Missing("Function output", outputId);
    return Apply("Delete function output", [&]
    {
        stl::erase_if(function->functionDef->outputs, [&](const BasicFunctionDef::Input& port) { return port.id == outputId; });
        ScriptUtils::RefreshFunctionRefs(m_script, functionId, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::RenameFunctionOutput(int functionId, int outputId, const std::string& name)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    BasicFunctionDef::Input* port = function ? function->functionDef->FindOutputByID(outputId) : nullptr;
    if (!port) return Missing("Function output", outputId);
    return Apply("Rename function output", [&]
    {
        port->name = name;
        ScriptUtils::RefreshFunctionRefs(m_script, functionId, m_ids);
        return OperationResult::Ok();
    });
}

OperationResult DocumentOperations::ChangeFunctionOutputValue(int functionId, int outputId, const Value& value)
{
    ScriptFunctionPtr function = FindFunction(functionId);
    BasicFunctionDef::Input* port = function ? function->functionDef->FindOutputByID(outputId) : nullptr;
    if (!port) return Missing("Function output", outputId);
    return Apply("Change function output", [&]
    {
        port->value = value;
        ScriptUtils::RefreshFunctionRefs(m_script, functionId, m_ids);
        return OperationResult::Ok();
    });
}

bool DocumentOperations::CanUndo() const { return m_historyCursor > 0; }
bool DocumentOperations::CanRedo() const { return m_historyCursor < m_history.size(); }

OperationResult DocumentOperations::Undo()
{
    if (IsTransactionActive())
    {
        OperationResult committed = CommitTransaction();
        if (!committed) return committed;
    }
    if (!CanUndo()) return OperationResult::Fail("There is nothing to undo.");
    OperationResult result = Restore(m_history[m_historyCursor - 1].before);
    if (result) --m_historyCursor;
    return result;
}

OperationResult DocumentOperations::Redo()
{
    if (IsTransactionActive())
    {
        OperationResult committed = CommitTransaction();
        if (!committed) return committed;
    }
    if (!CanRedo()) return OperationResult::Fail("There is nothing to redo.");
    OperationResult result = Restore(m_history[m_historyCursor].after);
    if (result) ++m_historyCursor;
    return result;
}

void DocumentOperations::ResetHistory()
{
    m_history.clear();
    m_historyCursor = 0;
    m_transactionDepth = 0;
    m_transactionLabel.clear();
    m_transactionBefore.clear();
    ++m_revision;
}
