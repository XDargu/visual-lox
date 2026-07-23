
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
#include "../validation/scriptValidator.h"

#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <map>
#include <set>
#include <functional>
#include <string>
#include <vector>

class NodeRegistry;
class DocumentOperations;
struct OperationResult;
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
    void setDocumentOperations(DocumentOperations& operations);
    void SetGraph(Script* pTargetScript, const ScriptFunctionPtr& pScriptFunction,
                  Graph* pTargetGraph, bool navigateToContent = true);
    void RegisterNode(const NodePtr& node);

    void Destroy();

    void OnFrame(float deltaTime);

    void DrawNodeEditor(ImTextureID& headerBackground, int headerWidth, int headerHeight);
    void DrawContextMenu();
    void ReportOperation(const OperationResult& result);

    ed::EditorContext* m_Editor = nullptr;
    bool m_NavigateToContentOnNextFrame = false;
    ImVec2 m_PreservedViewOrigin = ImVec2(0, 0);
    float m_PreservedViewScale = 1.0f;
    bool m_HasPreservedView = false;
    Graph* m_pGraph = nullptr;
    ScriptFunctionPtr m_pScriptFunction = nullptr;
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
    std::vector<ProcessedNode> processedNodes;
    const ValidationReport* validationReport = nullptr;
    
    // ID genration
    NodeRegistry* m_pNodeRegistry = nullptr;
    IDGenerator* m_pIDGenerator = nullptr;
    DocumentOperations* m_pOperations = nullptr;
    std::string operationError;
    float operationErrorTime = 0.0f;
    bool recordNodeStateHistory = true;
    bool nodePositionDragActive = false;
    std::set<int> amendNextNodePosition;
    ImVec2 lastCanvasMousePosition = ImVec2(0, 0);
    bool hasCanvasMousePosition = false;
    std::vector<std::string> recentNodeTypes;
    std::set<std::string> favoriteNodeTypes;
    int paletteSelection = 0;
};

struct GraphViewUtils
{
    static bool DrawTypeInputImpl(const PinType pinType, Value& inputValue);
    static bool DrawTypeInput(const PinType pinType, Value& inputValue);
    static void DrawTypeSelection(Value& inputValue, std::function<void(PinType type)> onChange);
};
