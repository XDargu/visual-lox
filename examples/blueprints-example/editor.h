#pragma once

#include <application.h>
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

    void ChangeGraph(ScriptFunction& scriptFunction);

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

    void AddFunction();

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

    IDGenerator          m_IDGenerator;
    NodeRegistry         m_NodeRegistry;

    // Script treeview
    TreeNode             m_scriptTreeView;

    // TODO: Move somewhere else!
    bool m_isConstFoldingEnabled = true;
    bool m_isRealTimeCompilationEnabled = true;
    std::vector<Value>   m_constFoldingValues;
    std::vector<ed::NodeId>   m_constFoldingIDs;
};

}