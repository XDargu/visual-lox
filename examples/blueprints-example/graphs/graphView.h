
# pragma once
#define IMGUI_DEFINE_MATH_OPERATORS

#include "node.h"
#include "link.h"
#include "graph.h"
#include "idgeneration.h"

#include "../utilities/builders.h"
#include "../utilities/widgets.h"
#include "../utilities/drawing.h"

#include "../script/function.h"

#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <map>

class NodeRegistry;
struct Value;
struct Script;

static inline ImRect ImGui_GetItemRect()
{
    return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

static inline ImRect ImRect_Expanded(const ImRect& rect, float x, float y)
{
    ImRect result = rect;
    result.Min.x -= x;
    result.Min.y -= y;
    result.Max.x += x;
    result.Max.y += y;
    return result;
}

namespace ed = ax::NodeEditor;

struct GraphView
{
    int GetNextId();

    void Init(ImFont* largeNodeFont);

    void TouchNode(ed::NodeId id);
    float GetTouchProgress(ed::NodeId id);
    void UpdateTouch();

    void DrawPinInput(const Pin& input, int inputIdx);
    void DrawPinIcon(const Pin& pin, bool connected, int alpha);;

    void BuildNode(const NodePtr& node);
    NodePtr SpawnNode(const NodePtr& node);

    void setIDGenerator(IDGenerator& generator);
    void setNodeRegistry(NodeRegistry& nodeRegistry);
    void SetGraph(Script* pTargetScript, ScriptFunction* pScriptFunction, Graph* pTargetGraph);

    void Destroy();

    void OnFrame(float deltaTime);

    void DrawNodeEditor(ImTextureID& headerBackground, int headerWidth, int headerHeight);
    void DrawContextMenu();

    ed::EditorContext* m_Editor = nullptr;
    Graph* m_pGraph = nullptr;
    ScriptFunction* m_pScriptFunction = nullptr;
    Script* m_pScript = nullptr;

    // Drawing
    ImFont* m_largeNodeFont = nullptr;

    // Touch control
    std::map<ed::NodeId, float, NodeIdLess> m_NodeTouchTime;
    const float          m_TouchTime = 1.0f;
    const int            m_PinIconSize = 24;

    // Drawing context info
    ed::NodeId contextNodeId = 0;
    ed::LinkId contextLinkId = 0;
    ed::PinId  contextPinId = 0;
    bool createNewNode = false;
    Pin* newNodeLinkPin = nullptr;
    Pin* newLinkPin = nullptr;

    // TODO: Move somewhere else
    std::vector<NodePtr> processedNodes;
    
    // ID genration
    NodeRegistry* m_pNodeRegistry = nullptr;
    IDGenerator* m_pIDGenerator = nullptr;
};

struct GraphViewUtils
{
    static void DrawTypeInputImpl(const PinType pinType, Value& inputValue);
    static void DrawTypeInput(const PinType pinType, Value& inputValue);
    static void DrawTypeSelection(Value& inputValue);
};