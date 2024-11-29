#pragma once

#include <application.h>

#include "editorActions.h"

#include "utilities/builders.h"
#include "utilities/widgets.h"

#include "graphs/link.h"
#include "graphs/node.h"
#include "graphs/graphView.h"
#include "graphs/graphCompiler.h"

#include "script/script.h"

#include "native/nodes/begin.h"
#include "native/nodes/branch.h"
#include "native/nodes/print.h"
#include "native/nodes/for-in.h"
#include "native/nodes/math.h"
#include "native/nodes/list.h"

#include "graphs/nodeRegistry.h"

#include "utilities/treeview.h"

#include <imgui_node_editor.h>
#include <imgui_internal.h>

#include <Compiler.h>
#include <Vm.h>
#include <Debug.h>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include <sstream>
#include <memory>

namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

using namespace ax;

using ax::Widgets::IconType;

static ed::EditorContext* m_Editor = nullptr;

namespace Editor
{

struct Example :
    public Application
{
    using Application::Application;

    void OnStart() override;
    void OnStop() override;

    void ChangeGraph(const ScriptFunctionPtr& scriptFunction);

    void ShowStyleEditor(bool* show = nullptr);
    void ShowNodeSelection(float paneWidth);

    std::vector<NodePtr> GatherProcessedNodes(Graph& graph, Compiler& compiler);

    void GatherConstFoldableNodes(Compiler& compiler, VM& vm);
    InterpretResult CompileConstFolding(VM& vm, const NodePtr& constNode);
    void CompileGraph(const Graph& graph, Compiler& compiler);
    void ShowCompilerInfo(float paneWidth);

    void ShowDebugPanel(float paneWidth);

    void ContextMenu();

    void ShowLeftPane(float paneWidth);

    void OnFrame(float deltaTime) override;

    // Tree Node Handling TODO: Move somewhere else
    TreeNode MakeFunctionNode(int funId, const std::string& name);
    TreeNode MakeVariableNode(int varId, const std::string& name);
    TreeNode MakeInputNode(int funId, int inputId, const std::string& name);
    TreeNode MakeOutputNode(int funId, int outputId, const std::string& name);

    TreeNode* FindNodeByID(int id);
    void EraseNodeByID(int id);

    // Script operations
    void AddFunction(int id);
    void AddFunction(const ScriptFunctionPtr& pExistingFunction);
    void AddVariable(int id);
    void AddVariable(const ScriptPropertyPtr& pVariable);

    void ChangeVariableValue(int id, Value& value);

    void RenameFunction(int funId, const char* name);
    void RenameVariable(int varId, const char* name);

    void AddFunctionInput(int funId, int inputId);
    void AddFunctionInput(int funId, int inputId, const char* name, const Value& value);
    void ChangeFunctionInputValue(int funId, int inputId, Value& value);
    void RenameFunctionInput(int funId, int inputId, const char* name);

    void AddFunctionOutput(int funId, int outputId);
    void AddFunctionOutput(int funId, int outputId, const char* name, const Value& value);
    void ChangeFunctionOutputValue(int funId, int outputId, Value& value);
    void RenameFunctionOutput(int funId, int outputId, const char* name);

    void RemoveFunction(int id);
    void RemoveVariable(int id);

    void RemoveFunctionInput(int funId, int inputId);
    void RemoveFunctionOutput(int funId, int outputId);

    void DoAction(IActionPtr action);
    void UndoLastAction();
    void RedoLastAction();
    bool CanUndo() const;
    bool CanRedo() const;

    Script               m_script;
    GraphView            m_graphView;

    ImTextureID          m_HeaderBackground = nullptr;
    ImTextureID          m_SaveIcon = nullptr;
    ImTextureID          m_RestoreIcon = nullptr;
    bool                 m_ShowOrdinals = false;

    // Icons for types of functions
    ImTextureID          m_ScriptIcon = nullptr;
    ImTextureID          m_ClassIcon = nullptr;
    ImTextureID          m_FunctionIcon = nullptr;
    ImTextureID          m_VariableIcon = nullptr;
    ImTextureID          m_InputIcon = nullptr;
    ImTextureID          m_OutputIcon = nullptr;

    IDGenerator          m_IDGenerator;
    NodeRegistry         m_NodeRegistry;

    // Script treeview
    TreeNode             m_scriptTreeView;
    int                  m_selectedItemId = 0;
    int                  m_editingItemId = 0;

    // TODO: Move somewhere else!
    bool m_isConstFoldingEnabled = true;
    bool m_isRealTimeCompilationEnabled = true;
    std::vector<Value>   m_constFoldingValues;
    std::vector<ed::NodeId>   m_constFoldingIDs;

    std::vector<IActionPtr> pendingActions;

    std::vector<IActionPtr> actionStack;
    int undoDepth = 0;
};

}