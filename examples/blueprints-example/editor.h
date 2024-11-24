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

static bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f)
{
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");
    ImRect bb;
    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
}

namespace Utils
{
    static void DrawEachLine(const std::string& text)
    {
        std::stringstream stream(text);
        std::string segment;

        while (std::getline(stream, segment, '\n'))
        {
            ImGui::Text(segment.c_str());
        }
    }

    struct CaptureStdout
    {
        CaptureStdout()
        {
            // Redirect cout.
            oldCoutStreamBuf = std::cout.rdbuf();
            std::cout.rdbuf(strCout.rdbuf());

            // Redirect cerr.
            oldCerrStreamBuf = std::cerr.rdbuf();
            std::cerr.rdbuf(strCout.rdbuf());
        }

        std::string Restore()
        {
            // Restore old cout.
            std::cout.rdbuf(oldCoutStreamBuf);
            std::cerr.rdbuf(oldCerrStreamBuf);

            return strCout.str();
        }

        std::streambuf* oldCoutStreamBuf;
        std::streambuf* oldCerrStreamBuf;
        std::ostringstream strCout;
    };
}


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

    struct TreeNode {
        std::string label;                  // Label of the node
        std::vector<TreeNode> children;     // List of child nodes
        bool isOpen = false;                // Tracks if the node is expanded
    };

    void RenderTreeNode(TreeNode& node, int& selectedItem, int& currentId);
    void ShowExampleTreeView();

    void ShowLeftPane(float paneWidth);

    void OnFrame(float deltaTime) override;

    Script               m_script;
    GraphView            m_graphView;

    ImTextureID          m_HeaderBackground = nullptr;
    ImTextureID          m_SaveIcon = nullptr;
    ImTextureID          m_RestoreIcon = nullptr;
    bool                 m_ShowOrdinals = false;

    IDGenerator          m_IDGenerator;
    NodeRegistry         m_NodeRegistry;

    // TODO: Move somewhere else!
    bool m_isConstFoldingEnabled = true;
    bool m_isRealTimeCompilationEnabled = true;
    std::vector<Value>   m_constFoldingValues;
    std::vector<ed::NodeId>   m_constFoldingIDs;
};
