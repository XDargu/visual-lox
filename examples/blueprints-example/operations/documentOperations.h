#pragma once

#include "../graphs/graph.h"
#include "../script/script.h"

#include <Value.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class NodeRegistry;
struct IDGenerator;

struct OperationResult
{
    bool success = false;
    std::string error;

    explicit operator bool() const { return success; }
    static OperationResult Ok() { return { true, {} }; }
    static OperationResult Fail(std::string message) { return { false, std::move(message) }; }
};

// The single mutation boundary for a Visual Lox document. Each successful
// operation records a complete, serialization-backed before/after transaction.
// This deliberately favors correctness and extensibility over compact history.
class DocumentOperations
{
public:
    DocumentOperations(Script& script, IDGenerator& ids, const NodeRegistry& registry);

    OperationResult AddNode(int functionId, const NodePtr& node);
    OperationResult RemoveNode(int functionId, ed::NodeId nodeId);
    OperationResult Connect(int functionId, ed::PinId first, ed::PinId second,
                            const std::vector<ProcessedNode>& processedNodes = {});
    OperationResult Disconnect(int functionId, ed::LinkId linkId);
    OperationResult ChangeNodeInputValue(int functionId, ed::NodeId nodeId, int inputIndex,
                                         const Value& value);
    OperationResult AddDynamicInput(int functionId, ed::NodeId nodeId);
    OperationResult RemoveDynamicInput(int functionId, ed::NodeId nodeId, ed::PinId pinId);
    OperationResult SetNodeState(int functionId, ed::NodeId nodeId, const std::string& state,
                                 bool recordHistory = false, bool coalesceHistory = false,
                                 bool amendPreviousTransaction = false);

    OperationResult CopyNodes(int functionId, const std::vector<int>& nodeIds);
    OperationResult PasteNodes(int functionId, std::vector<int>& pastedNodeIds);
    OperationResult CopyScriptElement(int elementId);
    OperationResult PasteScriptElement(int targetFunctionId, int& pastedElementId);
    bool HasClipboard() const { return m_clipboard.kind != ClipboardKind::None; }
    bool ClipboardContainsNodes() const { return m_clipboard.kind == ClipboardKind::Nodes; }

    OperationResult AddFunction(int id, const std::string& name = "Function");
    OperationResult RemoveFunction(int id);
    OperationResult RenameFunction(int id, const std::string& name);

    OperationResult AddClass(int id, const std::string& name = "Class");
    OperationResult RemoveClass(int id);
    OperationResult RenameClass(int id, const std::string& name);
    OperationResult AddClassProperty(int classId, int propertyId,
                                     const std::string& name = "Property",
                                     const Value& value = Value());
    OperationResult RemoveClassProperty(int classId, int propertyId);
    OperationResult RenameClassProperty(int classId, int propertyId, const std::string& name);
    OperationResult ChangeClassPropertyValue(int classId, int propertyId, const Value& value);
    OperationResult AddClassMethod(int classId, int methodId, const std::string& name = "Method");
    OperationResult RemoveClassMethod(int classId, int methodId);
    OperationResult AddClassConstructor(int classId, int constructorId);
    OperationResult RemoveClassConstructor(int classId);

    OperationResult AddVariable(int id, const std::string& name = "Variable", const Value& value = Value());
    OperationResult RemoveVariable(int id);
    OperationResult RenameVariable(int id, const std::string& name);
    OperationResult ChangeVariableValue(int id, const Value& value);

    OperationResult AddFunctionInput(int functionId, int inputId, const std::string& name = "Input",
                                     const Value& value = Value());
    OperationResult RemoveFunctionInput(int functionId, int inputId);
    OperationResult RenameFunctionInput(int functionId, int inputId, const std::string& name);
    OperationResult ChangeFunctionInputValue(int functionId, int inputId, const Value& value);

    OperationResult AddFunctionOutput(int functionId, int outputId, const std::string& name = "Output",
                                      const Value& value = Value());
    OperationResult RemoveFunctionOutput(int functionId, int outputId);
    OperationResult RenameFunctionOutput(int functionId, int outputId, const std::string& name);
    OperationResult ChangeFunctionOutputValue(int functionId, int outputId, const Value& value);

    bool CanUndo() const;
    bool CanRedo() const;
    OperationResult Undo();
    OperationResult Redo();
    void ResetHistory();
    std::uint64_t Revision() const { return m_revision; }
    OperationResult BeginTransaction(const std::string& label);
    OperationResult CommitTransaction();
    OperationResult CancelTransaction();
    bool IsTransactionActive() const { return m_transactionDepth > 0; }

private:
    enum class ClipboardKind { None, Nodes, Function, Variable, FunctionInput, FunctionOutput };
    struct Clipboard
    {
        ClipboardKind kind = ClipboardKind::None;
        std::string document;
        int ownerId = 0;
        std::vector<int> elementIds;
    };
    struct Transaction
    {
        std::string label;
        std::string before;
        std::string after;
    };

    OperationResult Apply(const std::string& label,
                          const std::function<OperationResult()>& mutation);
    OperationResult Restore(const std::string& snapshot);
    ScriptFunctionPtr FindFunction(int id) const;

    Script& m_script;
    IDGenerator& m_ids;
    const NodeRegistry& m_registry;
    std::vector<Transaction> m_history;
    size_t m_historyCursor = 0;
    std::uint64_t m_revision = 0;
    Clipboard m_clipboard;
    int m_transactionDepth = 0;
    std::string m_transactionLabel;
    std::string m_transactionBefore;
};
