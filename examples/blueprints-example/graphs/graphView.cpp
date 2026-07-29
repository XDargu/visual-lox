#pragma once

#include "graphView.h"
#include "graphLayout.h"
#include "IconsFontAwesome6.h"

#include "nodeRegistry.h"

#include "../native/nodes/begin.h"
#include "../native/nodes/commentBox.h"
#include "../native/nodes/return.h"
#include "../utilities/utils.h"

#include "../native/nodes/variable.h"
#include "../native/nodes/function.h"
#include "../native/nodes/object.h"

#include "../script/script.h"
#include "../operations/documentOperations.h"

#include <Compiler.h>

#include <misc/imgui_stdlib.h>
#include <imgui_node_editor_internal.h>

#include <string_view>
#include <stack>
#include <algorithm>
#include <limits>
#include <fstream>
#include <cmath>

namespace Utils
{
    bool FilterString(std::string_view target, std::string_view filter)
    {
        if (filter.empty()) return true;

        // Fuzzy subsequence matching keeps palette searches useful when users
        // type abbreviated intents such as "gvar" for "Get Variable".
        size_t cursor = 0;
        for (const char character : target)
        {
            if (cursor < filter.size() && character == filter[cursor])
                ++cursor;
        }
        return cursor == filter.size();
    }
}

// Graph View
int GraphView::GetNextId()
{
    return m_pIDGenerator->GetNextId();
}

void GraphView::Init(ImFont* largeNodeFont)
{
    m_largeNodeFont = largeNodeFont;
    std::ifstream preferences("VisualLoxPalette.ini");
    std::string line;
    while (std::getline(preferences, line))
    {
        if (line.rfind("favorite=", 0) == 0)
            favoriteNodeTypes.insert(line.substr(9));
        else if (line.rfind("recent=", 0) == 0 && recentNodeTypes.size() < 8)
            recentNodeTypes.push_back(line.substr(7));
    }
}

void GraphView::setNavigationHandlers(
    std::function<void(int)> goToOrigin,
    std::function<void(const NodePtr&)> findReferences)
{
    onGoToOrigin = std::move(goToOrigin);
    onFindReferences = std::move(findReferences);
}

void GraphView::FocusNodeOnNextFrame(int nodeId)
{
    focusNodeIdOnNextFrame = nodeId;
    m_NavigateToContentOnNextFrame = false;
}

void GraphView::TouchNode(ed::NodeId id)
{
    m_NodeTouchTime[id] = m_TouchTime;
}

float GraphView::GetTouchProgress(ed::NodeId id)
{
    auto it = m_NodeTouchTime.find(id);
    if (it != m_NodeTouchTime.end() && it->second > 0.0f)
        return (m_TouchTime - it->second) / m_TouchTime;
    else
        return 0.0f;
}

void GraphView::UpdateTouch()
{
    const float deltaTime = ImGui::GetIO().DeltaTime;
    for (auto& entry : m_NodeTouchTime)
    {
        if (entry.second > 0.0f)
            entry.second -= deltaTime;
    }
}

void GraphView::DrawPinInput(const Pin& input, int inputIdx)
{
    const NodePtr& node = input.Node;
    Value inputValue = node->InputValues[inputIdx];
    const bool changed = GraphViewUtils::DrawTypeInput(input.Type, inputValue);
    if (ImGui::IsItemActivated() && m_pOperations && !m_pOperations->IsTransactionActive())
        ReportOperation(m_pOperations->BeginTransaction("Edit node input"));
    if (changed && m_pOperations && m_pScriptFunction)
    {
        if (!m_pOperations->IsTransactionActive())
            ReportOperation(m_pOperations->BeginTransaction("Edit node input"));
        OperationResult operation = m_pOperations->ChangeNodeInputValue(
            m_pScriptFunction->ID.id, node->ID, inputIdx, inputValue);
        ReportOperation(operation);
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && m_pOperations && m_pOperations->IsTransactionActive())
        ReportOperation(m_pOperations->CommitTransaction());
    ImGui::Spring(0);
}

void GraphView::DrawPinIcon(const Pin& pin, bool connected, int alpha)
{
    const ax::Drawing::IconType iconType = GetPinIcon(pin.Type);
    ImColor color = GetIconColor(pin.Type);
    color.Value.w = alpha / 255.0f;

    // Just a test
    if (pin.Type == PinType::Any && connected)
    {
        // Figure out to which type is it connected
        if (pin.Kind == PinKind::Input)
        {
            if (const Pin* input = GraphUtils::FindConnectedOutput(*m_pGraph, pin))
            {
                color = GetIconColor(input->Type);
            }
        }
    }

    ax::Widgets::Icon(ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)), iconType, connected, color, ImColor(32, 32, 32, alpha));
}

NodePtr GraphView::SpawnNode(const NodePtr& node)
{
    if (!m_pOperations || !m_pScriptFunction)
        return nullptr;
    OperationResult result = m_pOperations->AddNode(m_pScriptFunction->ID.id, node);
    ReportOperation(result);
    return result ? node : nullptr;
}

void GraphView::ReportOperation(const OperationResult& result)
{
    if (result)
    {
        operationError.clear();
        operationErrorTime = 0.0f;
    }
    else
    {
        operationError = result.error;
        operationErrorTime = 4.0f;
    }
}

void GraphView::setIDGenerator(IDGenerator& generator)
{
    m_pIDGenerator = &generator;
}

void GraphView::setNodeRegistry(NodeRegistry& nodeRegistry)
{
    m_pNodeRegistry = &nodeRegistry;
}

namespace
{
bool DrawNodeDiagnosticBox(ed::NodeId nodeId,
                           const std::vector<const ValidationDiagnostic*>& diagnostics,
                           bool hasError)
{
    if (diagnostics.empty()) return false;

    const ValidationDiagnostic* primary = diagnostics.front();
    if (hasError)
    {
        const auto error = std::find_if(diagnostics.begin(), diagnostics.end(),
            [](const ValidationDiagnostic* diagnostic)
            {
                return diagnostic->severity == DiagnosticSeverity::Error;
            });
        if (error != diagnostics.end()) primary = *error;
    }
    const std::string& message = primary->message;
    const float previousNodeWidth = ed::GetNodeSize(nodeId).x;
    const float width = previousNodeWidth > 40.0f
        ? std::max(80.0f, previousNodeWidth - 16.0f)
        : 160.0f;
    const ImVec2 size(width, ImGui::GetTextLineHeight() + 8.0f);

    ImGui::PushID("node-diagnostic");
    ImGui::InvisibleButton("##message", size);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 fill = hasError ? IM_COL32(125, 35, 35, 245) : IM_COL32(120, 85, 20, 245);
    const ImU32 border = hasError ? IM_COL32(255, 90, 90, 255) : IM_COL32(255, 195, 60, 255);
    drawList->AddRectFilled(min, max, fill, 3.0f);
    drawList->AddRect(min, max, border, 3.0f);
    const ImVec2 textMin = min + ImVec2(5.0f, 4.0f);
    const ImVec2 textMax = max - ImVec2(5.0f, 2.0f);
    ImGui::RenderTextEllipsis(drawList, textMin, textMax, textMax.x, textMax.x,
                              message.c_str(), message.c_str() + message.size(), nullptr);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    return hovered;
}

void DrawPinTooltip(const Pin& pin)
{
    ImGui::BeginTooltip();
    if (!pin.Name.empty())
        ImGui::TextUnformatted(pin.Name.c_str());
    ImGui::TextDisabled("Type: %s", pin.Type.ToString().c_str());
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(pin.Description.empty()
        ? "No pin description has been provided"
        : pin.Description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void DrawNodeTooltip(const Node& node)
{
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(node.Name.c_str());
    if (node.IsPure())
        ImGui::TextDisabled("Pure");
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(node.Description.empty()
        ? "No node description has been provided"
        : node.Description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
}

void GraphView::setDocumentOperations(DocumentOperations& operations)
{
    m_pOperations = &operations;
}

void GraphView::SetGraph(Script* pTargetScript, const ScriptFunctionPtr& pScriptFunction,
                         Graph* pTargetGraph, bool navigateToContent)
{
    const bool preserveStyle = m_Editor != nullptr;
    ed::Style preservedStyle;
    if (preserveStyle)
    {
        ed::SetCurrentEditor(m_Editor);
        preservedStyle = ed::GetStyle();
    }

    // Destroying the old context flushes its latest node positions through the
    // SaveNodeSettings callback while m_pGraph still points at the old graph.
    Destroy();

    m_pGraph = pTargetGraph;
    m_pScript = pTargetScript;
    m_pScriptFunction = pScriptFunction;
    hasCanvasMousePosition = false;
    editingCommentBoxId = -1;
    commentBoxEditText.clear();
    focusCommentBoxEditor = false;
    autoLayoutRequested = false;

    ed::Config config;

    config.SettingsFile = "Blueprints.json";
    config.CanvasSizeMode = ed::CanvasSizeMode::CenterOnly;
    config.GridSpacing = nodeGridSpacing;

    config.UserPointer = this;

    config.LoadNodeSettings = [](ed::NodeId nodeId, char* data, void* userPointer) -> size_t
    {
        GraphView* self = static_cast<GraphView*>(userPointer);

        NodePtr node = self->m_pGraph->FindNode(nodeId);
        if (!node)
            return 0;

        if (data != nullptr)
            memcpy(data, node->State.data(), node->State.size());
        return node->State.size();
    };

    config.SaveNodeSettings = [](ed::NodeId nodeId, const char* data, size_t size, ed::SaveReasonFlags reason, void* userPointer) -> bool
    {
        GraphView* self = static_cast<GraphView*>(userPointer);

        NodePtr node = self->m_pGraph->FindNode(nodeId);
        if (!node)
            return false;

        const std::string state(data, size);
        if (self->m_pOperations && self->m_pScriptFunction)
        {
            const bool isPositionChange =
                (reason & ed::SaveReasonFlags::Position) != ed::SaveReasonFlags::None;
            const bool isSizeChange =
                (reason & ed::SaveReasonFlags::Size) != ed::SaveReasonFlags::None;
            const bool isUserChange =
                (reason & ed::SaveReasonFlags::User) != ed::SaveReasonFlags::None;
            const bool dragging = isPositionChange && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            const int rawNodeId = static_cast<int>(nodeId.Get());
            const bool amendCreation = isPositionChange &&
                self->amendNextNodePosition.erase(rawNodeId) != 0;
            // The node editor reports AddNode and automatic Size changes while
            // laying out a graph for the first time. Those are initialization,
            // not document edits. Persist explicit positions and user resizes.
            if (isPositionChange || (isSizeChange && isUserChange))
            {
                const OperationResult operation = self->m_pOperations->SetNodeState(
                    self->m_pScriptFunction->ID.id, nodeId, state,
                    self->recordNodeStateHistory && isPositionChange,
                    dragging && self->nodePositionDragActive,
                    amendCreation);
                if (!operation)
                    self->ReportOperation(operation);
                if (isPositionChange && self->autoLayoutTransactionActive)
                {
                    self->autoLayoutFailed |= !operation;
                    self->pendingAutoLayoutNodeStates.erase(rawNodeId);
                }
            }
            self->nodePositionDragActive = dragging;
        }
        else
            node->State = state;

        self->TouchNode(nodeId);

        return true;
    };

    m_Editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(m_Editor);
    if (preserveStyle)
        ed::GetStyle() = preservedStyle;
    if (!navigateToContent && m_HasPreservedView)
    {
        auto* internalEditor =
            reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(m_Editor);
        internalEditor->SetView(m_PreservedViewOrigin, m_PreservedViewScale);
    }

    // We should add the nodes here in a better wya
    // TODO: Improve this. This calls IMGUI!
    // I might need to manually expose this
    for (auto& node : m_pGraph->GetNodes())
        RegisterNode(node);

    // Restoration is applied when nodes are drawn on the next frame. Frame the
    // content afterwards, once the restored bounds are available.
    m_NavigateToContentOnNextFrame = navigateToContent;
}

void GraphView::RegisterNode(const NodePtr& node)
{
    if (!node)
        return;
    // Creating a node can initialize settings from Blueprints.json. Keep the
    // graph-owned state authoritative so loading or pasting cannot be
    // overwritten by defaults before RestoreNodeState runs.
    const std::string persistedState = node->State;
    ed::BeginNode(node->ID);
    ed::EndNode();
    node->State = persistedState;
    if (!node->State.empty())
        ed::RestoreNodeState(node->ID);
}

void GraphView::Destroy()
{
    std::ofstream preferences("VisualLoxPalette.ini", std::ios::trunc);
    if (preferences)
    {
        for (const std::string& favorite : favoriteNodeTypes)
            preferences << "favorite=" << favorite << '\n';
        for (const std::string& recent : recentNodeTypes)
            preferences << "recent=" << recent << '\n';
    }

    if (m_Editor)
    {
        auto* internalEditor =
            reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(m_Editor);
        const ImGuiEx::CanvasView view = internalEditor->GetView();
        m_PreservedViewOrigin = view.Origin;
        m_PreservedViewScale = view.Scale;
        m_HasPreservedView = true;
        recordNodeStateHistory = false;
        ed::DestroyEditor(m_Editor);
        FinishAutoLayout(true);
        recordNodeStateHistory = true;
        m_Editor = nullptr;
        m_pGraph = nullptr;
    }
}

void GraphView::OnFrame(float deltaTime)
{
    UpdateTouch();

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        nodePositionDragActive = false;
    if (operationErrorTime > 0.0f)
    {
        operationErrorTime -= deltaTime;
        if (operationErrorTime <= 0.0f)
            operationError.clear();
    }

    ed::SetCurrentEditor(m_Editor);
}

void GraphView::DrawNodeEditor(ImTextureID& headerBackground, int headerWidth, int headerHeight)
{
    BeginAutoLayout();

    if (!operationError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", operationError.c_str());

    std::vector<const ValidationDiagnostic*> hoveredDiagnostics;
    const Pin* hoveredPin = nullptr;
    ed::Begin("Node editor");

    {
        ImVec2 cursorTopLeft = ImGui::GetCursorScreenPos();

        ax::NodeEditor::Utilities::BlueprintNodeBuilder builder(headerBackground, headerWidth, headerHeight);

        // Simple nodes
        for (const NodePtr& node : m_pGraph->GetNodes())
        {
            if (node->Type != NodeType::Blueprint && node->Type != NodeType::SimpleGet && node->Type != NodeType::SimpleLargeBody)
                continue;

            const bool isSimpleGet = node->Type == NodeType::SimpleGet;
            const bool isSimpleLarge = node->Type == NodeType::SimpleLargeBody;
            const std::vector<const ValidationDiagnostic*> nodeDiagnostics =
                validationReport && m_pScriptFunction
                    ? validationReport->ForNode(m_pScriptFunction->ID, node->ID)
                    : std::vector<const ValidationDiagnostic*>();
            const bool hasDiagnosticError = std::any_of(nodeDiagnostics.begin(), nodeDiagnostics.end(),
                [](const ValidationDiagnostic* diagnostic)
                {
                    return diagnostic->severity == DiagnosticSeverity::Error;
                });

            const bool isDisconnected = nodeDiagnostics.empty() &&
                std::find_if(processedNodes.begin(), processedNodes.end(),
                    [&](const ProcessedNode& pnode) { return pnode.node->ID == node->ID; }) == processedNodes.end();

            const float alpha = ImGui::GetStyle().Alpha;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha * (isDisconnected ? 0.4f : 1.0f));
            if (!nodeDiagnostics.empty())
                ed::PushStyleColor(ed::StyleColor_NodeBorder,
                    hasDiagnosticError ? ImColor(255, 55, 55, 255) : ImColor(255, 190, 40, 255));

            builder.Begin(node->ID);
            if (!(isSimpleGet || isSimpleLarge))
            {
                builder.Header(node->Color);
                ImGui::Spring(0);
                ImGui::TextUnformatted(node->Name.c_str());
                if (!nodeDiagnostics.empty())
                    ImGui::TextColored(hasDiagnosticError ? ImVec4(1.0f, 0.25f, 0.25f, 1.0f)
                                                           : ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "!");
                ImGui::Spring(1);
                builder.EndHeader();
            }

            const bool usesImplicitReceiver =
                m_pScript && m_pScriptFunction &&
                GraphUtils::UsesImplicitReceiver(
                    *m_pScript, m_pScriptFunction->ID, *m_pGraph, *node);
            const int receiverInputIndex = node->GetReceiverInputIndex();
            const bool hasDynamicInputs = HasFlag(node->DefinitionFlags, NodeDefinitionFlags::DynamicInputs);
            const bool canAddDynamicInput = hasDynamicInputs && node->CanAddInput();
            int idx = 0;
            for (const Pin& input : node->Inputs)
            {
                const bool linked = m_pGraph->IsPinLinked(input.ID);
                const bool isReceiver = idx == receiverInputIndex;
                float alpha = ImGui::GetStyle().Alpha;
                //if (newLinkPin && m_pGraph->CanCreateLink(newLinkPin, &input, processedNodes) != ELinkQueryResult::Possible && &input != newLinkPin)
                //    alpha = alpha * (48.0f / 255.0f);

                builder.Input(input.ID);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                DrawPinIcon(input, linked, (int)(alpha * 255));
                ImGui::Spring(0);
                if (node->ShowInputPinNames && !input.Name.empty())
                {
                    ImGui::TextUnformatted(input.Name.c_str());
                    ImGui::Spring(0);
                }
                if (isReceiver && !linked)
                {
                    if (usesImplicitReceiver)
                        ImGui::TextDisabled("self");
                    else
                        ImGui::TextColored(
                            ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "required");
                    ImGui::Spring(0);
                }

                if (!linked)
                    DrawPinInput(input, idx);

                ImGui::PopStyleVar();
                builder.EndInput();
                if (ImGui::IsItemHovered())
                    hoveredPin = &input;
                ++idx;
            }

            if (canAddDynamicInput && !isSimpleLarge)
            {
                builder.BeginInputControl();
                if (ImGui::Button("Add Pin"))
                {
                    OperationResult operation = m_pOperations->AddDynamicInput(m_pScriptFunction->ID.id, node->ID);
                    ReportOperation(operation);
                }
                builder.EndInputControl();
            }

            if (isSimpleGet || isSimpleLarge)
            {
                builder.Middle();

                if (isSimpleLarge)
                    ImGui::PushFont(m_largeNodeFont);

                ImGui::Spring(1, 0);
                ImGui::TextUnformatted(node->Name.c_str());
                ImGui::Spring(1, 0);

                if (isSimpleLarge)
                    ImGui::PopFont();
            }

            if (isSimpleLarge && hasDynamicInputs)
                builder.TopAlignOutputs();

            for (const Pin& output : node->Outputs)
            {
                float alpha = ImGui::GetStyle().Alpha;
                //if (newLinkPin && m_pGraph->CanCreateLink(newLinkPin, &output, processedNodes) != ELinkQueryResult::Possible && &output != newLinkPin)
                //    alpha = alpha * (48.0f / 255.0f);

                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                builder.Output(output.ID);
                /*if (output.Type == PinType::String)
                {
                    static char buffer[128] = "Edit Me\nMultiline!";
                    static bool wasActive = false;

                    ImGui::PushItemWidth(100.0f);
                    ImGui::InputText("##edit", buffer, 127);
                    ImGui::PopItemWidth();
                    if (ImGui::IsItemActive() && !wasActive)
                    {
                        ed::EnableShortcuts(false);
                        wasActive = true;
                    }
                    else if (!ImGui::IsItemActive() && wasActive)
                    {
                        ed::EnableShortcuts(true);
                        wasActive = false;
                    }
                    ImGui::Spring(0);
                }*/
                if (node->ShowOutputPinNames && !output.Name.empty())
                {
                    ImGui::Spring(0);
                    ImGui::TextUnformatted(output.Name.c_str());
                }
                ImGui::Spring(0);
                DrawPinIcon(output, m_pGraph->IsPinLinked(output.ID), (int)(alpha * 255));
                ImGui::PopStyleVar();
                builder.EndOutput();
                if (ImGui::IsItemHovered())
                    hoveredPin = &output;
            }

            if (isSimpleLarge && canAddDynamicInput)
            {
                builder.BeginOutputControl();
                ImGui::Spring(1);
                if (ImGui::Button("Add Pin"))
                {
                    OperationResult operation = m_pOperations->AddDynamicInput(m_pScriptFunction->ID.id, node->ID);
                    ReportOperation(operation);
                }
                builder.EndOutputControl();
            }

            if (!nodeDiagnostics.empty())
            {
                builder.Footer();
                if (DrawNodeDiagnosticBox(node->ID, nodeDiagnostics, hasDiagnosticError))
                    hoveredDiagnostics = nodeDiagnostics;
            }

            builder.End();

            if (!nodeDiagnostics.empty())
                ed::PopStyleColor();

            ImGui::PopStyleVar();
        }

        // Comment boxes
        for (const NodePtr& node : m_pGraph->GetNodes())
        {
            if (node->Type != NodeType::CommentBox)
                continue;

            const float commentBoxAlpha = 0.75f;
            const auto withAlpha = [](ImColor color, int alpha)
            {
                color.Value.w = alpha / 255.0f;
                return color;
            };
            bool commitEdit = false;
            bool cancelEdit = false;

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentBoxAlpha);
            ed::PushStyleColor(ed::StyleColor_NodeBg, withAlpha(node->Color, 64));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, withAlpha(node->Color, 128));
            ed::BeginNode(node->ID);
            ImGui::PushID(node->ID.AsPointer());
            ImGui::BeginVertical("content");
            ImGui::BeginHorizontal("horizontal");
            ImGui::Spring(1);
            if (editingCommentBoxId == node->ID.Get())
            {
                ed::EnableShortcuts(false);
                if (focusCommentBoxEditor)
                {
                    ImGui::SetKeyboardFocusHere();
                    focusCommentBoxEditor = false;
                }
                ImGui::SetNextItemWidth((std::max)(140.0f, node->Size.x - 32.0f));
                const bool submitted = ImGui::InputText("##comment-box-label", &commentBoxEditText,
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                cancelEdit = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape), false);
                commitEdit = !cancelEdit && (submitted || ImGui::IsItemDeactivated());
            }
            else
            {
                ImGui::TextUnformatted(node->Name.c_str());
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    editingCommentBoxId = static_cast<int>(node->ID.Get());
                    commentBoxEditText = node->Name;
                    focusCommentBoxEditor = true;
                }
            }
            ImGui::Spring(1);
            ImGui::EndHorizontal();
            ed::Group(node->Size);
            ImGui::EndVertical();
            ImGui::PopID();
            ed::EndNode();
            ed::PopStyleColor(2);
            ImGui::PopStyleVar();

            if (cancelEdit || commitEdit)
            {
                if (commitEdit && !commentBoxEditText.empty())
                    ReportOperation(m_pOperations->ChangeCommentBoxText(m_pScriptFunction->ID.id, node->ID, commentBoxEditText));
                ed::EnableShortcuts(true);
                editingCommentBoxId = -1;
                commentBoxEditText.clear();
                focusCommentBoxEditor = false;
            }

            if (ed::BeginGroupHint(node->ID))
            {
                auto bgAlpha = static_cast<int>(ImGui::GetStyle().Alpha * 255);

                auto min = ed::GetGroupMin();

                ImGui::SetCursorScreenPos(min - ImVec2(-8, ImGui::GetTextLineHeightWithSpacing() + 4));
                ImGui::BeginGroup();
                ImGui::TextUnformatted(node->Name.c_str());
                ImGui::EndGroup();

                auto drawList = ed::GetHintBackgroundDrawList();

                auto hintBounds = ImGui_GetItemRect();
                auto hintFrameBounds = ImRect_Expanded(hintBounds, 8, 4);

                drawList->AddRectFilled(
                    hintFrameBounds.GetTL(),
                    hintFrameBounds.GetBR(),
                    withAlpha(node->Color, 64 * bgAlpha / 255), 4.0f);

                drawList->AddRect(
                    hintFrameBounds.GetTL(),
                    hintFrameBounds.GetBR(),
                    withAlpha(node->Color, 128 * bgAlpha / 255), 4.0f);
            }
            ed::EndGroupHint();
        }

        // Links
        for (const Link& link : m_pGraph->GetLinks())
            ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);

        if (!createNewNode)
        {
            if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f))
            {
                auto showLabel = [](const char* label, ImColor color)
                {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
                    auto size = ImGui::CalcTextSize(label);

                    auto padding = ImGui::GetStyle().FramePadding;
                    auto spacing = ImGui::GetStyle().ItemSpacing;

                    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

                    auto rectMin = ImGui::GetCursorScreenPos() - padding;
                    auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

                    auto drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                    ImGui::TextUnformatted(label);
                };

                ed::PinId startPinId = 0, endPinId = 0;
                if (ed::QueryNewLink(&startPinId, &endPinId))
                {
                    auto startPin = m_pGraph->FindPin(startPinId);
                    auto endPin = m_pGraph->FindPin(endPinId);

                    newLinkPin = startPin ? startPin : endPin;

                    if (startPin && startPin->Kind == PinKind::Input)
                    {
                        std::swap(startPin, endPin);
                        std::swap(startPinId, endPinId);
                    }

                    if (startPin && endPin)
                    {
                        const ELinkQueryResult result = m_pGraph->CanCreateLink(startPin, endPin, processedNodes);
                        if (result == ELinkQueryResult::Possible)
                        {
                            const bool replacesLink = !m_pGraph->CollectLinksToReplace(startPin, endPin).empty();
                            showLabel(replacesLink ? "~ Replace Link" : "+ Create Link",
                                      ImColor(32, 45, 32, 180));
                            if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                            {
                                OperationResult operation = m_pOperations->Connect(
                                    m_pScriptFunction->ID.id, startPinId, endPinId, processedNodes);
                                ReportOperation(operation);
                            }
                        }
                        else if (endPin == startPin)
                        {
                            // No message while hovering over the source pin
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else
                        {
                            showLabel((std::string("x ") + LinkQueryResultToString(result)).c_str(), ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        
                    }
                }

                ed::PinId pinId = 0;
                if (ed::QueryNewNode(&pinId))
                {
                    newLinkPin = m_pGraph->FindPin(pinId);
                    if (newLinkPin)
                        showLabel("+ Create Node", ImColor(32, 45, 32, 180));

                    if (ed::AcceptNewItem())
                    {
                        createNewNode = true;
                        newNodeLinkPin = m_pGraph->FindPin(pinId);
                        paletteScriptItem = {};
                        newLinkPin = nullptr;
                        ed::Suspend();
                        ImGui::OpenPopup("Create New Node");
                        ed::Resume();
                    }
                }
            }
            else
                newLinkPin = nullptr;

            ed::EndCreate();

            if (ed::BeginDelete())
            {
                ed::NodeId nodeId = 0;
                while (ed::QueryDeletedNode(&nodeId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        OperationResult operation = m_pOperations->RemoveNode(m_pScriptFunction->ID.id, nodeId);
                        ReportOperation(operation);
                    }
                }

                ed::LinkId linkId = 0;
                while (ed::QueryDeletedLink(&linkId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        OperationResult operation = m_pOperations->Disconnect(m_pScriptFunction->ID.id, linkId);
                        ReportOperation(operation);
                    }
                }
            }
            ed::EndDelete();
        }

        ImGui::SetCursorScreenPos(cursorTopLeft);
    }

    DrawContextMenu();
    if (const NodePtr doubleClicked =
            m_pGraph->FindNode(ed::GetDoubleClickedNode());
        doubleClicked && doubleClicked->refId.IsValid() && onGoToOrigin)
    {
        onGoToOrigin(doubleClicked->refId.id);
    }
    if (ed::BeginShortcut())
    {
        // Clipboard handling is centralized by the editor. Accept the node
        // editor's shortcut action so it cannot remain active and block input.
        ed::AcceptCopy();
        ed::AcceptPaste();
        ed::AcceptCut();
        ed::AcceptDuplicate();
        ed::AcceptCreateNode();
        ed::EndShortcut();
    }
    if (focusNodeIdOnNextFrame >= 0)
    {
        if (m_pGraph->FindNode(ed::NodeId(focusNodeIdOnNextFrame)))
        {
            ed::ClearSelection();
            ed::SelectNode(ed::NodeId(focusNodeIdOnNextFrame));
            ed::NavigateToSelection(false, 0.0f);
        }
        focusNodeIdOnNextFrame = -1;
        m_NavigateToContentOnNextFrame = false;
    }
    else if (m_NavigateToContentOnNextFrame)
    {
        ed::NavigateToContent(0.0f);
        m_NavigateToContentOnNextFrame = false;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        // The node editor canvas has already transformed ImGui's input into
        // local canvas space at this point.
        lastCanvasMousePosition = ImGui::GetMousePos();
        hasCanvasMousePosition = true;
    }
    ed::End();
    FinishAutoLayout();
    const NodePtr hoveredNode = m_pGraph->FindNode(ed::GetHoveredNode());

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(Editor::ScriptItemDragPayloadType))
        {
            if (payload->DataSize == sizeof(Editor::TreeNodeDragPayload))
            {
                pendingScriptItemDrop =
                    *static_cast<const Editor::TreeNodeDragPayload*>(payload->Data);
                pendingScriptItemDropPosition = hasCanvasMousePosition
                    ? lastCanvasMousePosition
                    : ed::ScreenToCanvas(ImGui::GetMousePos());
                openPaletteForScriptItem = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Render after the node editor has restored normal screen coordinates.
    // Suspending while a node is being built disrupts the builder's draw-list
    // channels and can corrupt the rest of that node's layout.
    // Link creation draws its own contextual label next to the cursor.
    if (!newLinkPin && !hoveredDiagnostics.empty())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        for (const ValidationDiagnostic* diagnostic : hoveredDiagnostics)
            ImGui::TextUnformatted(diagnostic->message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    else if (!newLinkPin && hoveredPin)
        DrawPinTooltip(*hoveredPin);
    else if (!newLinkPin && hoveredNode)
        DrawNodeTooltip(*hoveredNode);
}

void GraphView::DrawContextMenu()
{
    static std::string searchFilter = "";
    static std::string searchFilterLower = "";

    static bool addNodePopupOpened = false;
    static bool focusPaletteSearch = false;
    static ImVec2 openPopupPosition;

    if (!addNodePopupOpened && !openPaletteForScriptItem)
        // DrawContextMenu runs inside the active canvas transform, so this is
        // already the editor-space position expected by SetNodePosition.
        openPopupPosition = ImGui::GetMousePos();

    const bool openPaletteFromKeyboard =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Space), false);

    ed::Suspend();
    if (openPaletteForScriptItem)
    {
        addNodePopupOpened = true;
        focusPaletteSearch = true;
        paletteSelection = 0;
        openPopupPosition = pendingScriptItemDropPosition;
        paletteScriptItem = pendingScriptItemDrop;
        openPaletteForScriptItem = false;
        newNodeLinkPin = nullptr;
        searchFilter = "";
        searchFilterLower = "";
        ImGui::OpenPopup("Create New Node");
    }
    else if (ed::ShowNodeContextMenu(&contextNodeId))
    {
        ImGui::OpenPopup("Node Context Menu");
    }
    else if (ed::ShowPinContextMenu(&contextPinId))
    {
        ImGui::OpenPopup("Pin Context Menu");
    }
    else if (ed::ShowLinkContextMenu(&contextLinkId))
    {
        ImGui::OpenPopup("Link Context Menu");
    }
    else if (ed::ShowBackgroundContextMenu() || openPaletteFromKeyboard)
    {
        addNodePopupOpened = true;
        focusPaletteSearch = true;
        paletteSelection = 0;
        ImGui::OpenPopup("Create New Node");
        newNodeLinkPin = nullptr;
        paletteScriptItem = {};
        searchFilter = "";
        searchFilterLower = "";
    }
    ed::Resume();

    ed::Suspend();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    if (ImGui::BeginPopup("Node Context Menu"))
    {
        NodePtr node = m_pGraph->FindNode(contextNodeId);

        const bool hasOrigin = node && node->refId.IsValid();
        const bool canFindReferences = node && node->Type != NodeType::CommentBox &&
            (hasOrigin || !node->SerializationType.empty());

        if (ImGui::MenuItem( ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "  Go to Origin", nullptr, false, hasOrigin) && onGoToOrigin)
            onGoToOrigin(node->refId.id);

        if (ImGui::MenuItem(ICON_FA_MAGNIFYING_GLASS "  Find References", nullptr, false, canFindReferences) && onFindReferences)
            onFindReferences(node);

        if (node)
            ImGui::Separator();

        if (node && ImGui::MenuItem(ICON_FA_COPY "  Copy", "Ctrl+C"))
        {
            std::vector<ed::NodeId> selected(ed::GetSelectedObjectCount());
            int count = ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()));
            const bool contextIsSelected = std::find(selected.begin(), selected.begin() + count,
                                                     contextNodeId) != selected.begin() + count;
            std::vector<int> ids;
            if (contextIsSelected)
            {
                for (int i = 0; i < count; ++i)
                    ids.push_back(static_cast<int>(selected[i].Get()));
            }
            else
            {
                ids.push_back(static_cast<int>(contextNodeId.Get()));
            }
            ReportOperation(m_pOperations->CopyNodes(m_pScriptFunction->ID.id, ids));
        }
        if (node && ImGui::MenuItem(ICON_FA_CLONE "  Duplicate", "Ctrl+D"))
        {
            ReportOperation(m_pOperations->CopyNodes(
                m_pScriptFunction->ID.id, { static_cast<int>(contextNodeId.Get()) }));
            std::vector<int> pasted;
            ReportOperation(m_pOperations->PasteNodes(m_pScriptFunction->ID.id, pasted));
            bool append = false;
            for (const int id : pasted)
            {
                ed::SelectNode(ed::NodeId(id), append);
                append = true;
            }
        }
        const bool canDelete = node &&
            !HasFlag(node->DefinitionFlags, NodeDefinitionFlags::Protected);
        if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete", "Delete", false, canDelete))
            ed::DeleteNode(contextNodeId);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Pin Context Menu"))
    {
        Pin* pin = m_pGraph->FindPin(contextPinId);

        if (pin && pin->Type == PinType::Any)
        {
            ImGui::TextDisabled(ICON_FA_WAND_MAGIC_SPARKLES "  Convert type");
            const int inputIdx = GraphUtils::FindNodeInputIdx(*pin);
            Value inputValue = pin->Node->InputValues[inputIdx];

            GraphViewUtils::DrawTypeSelection(inputValue, [&](PinType newType)
            {
                switch (newType)
                {
                case PinType::Bool: inputValue = Value(false); break;
                case PinType::Float: inputValue = Value(0.0); break;
                case PinType::String: inputValue = Value(takeString("", 0)); break;
                case PinType::List: inputValue = Value(newList()); break;
                case PinType::Range: inputValue = Value(newRange(0.0, 1.0)); break;
                case PinType::Object: inputValue = Value(); break;
                case PinType::Function: inputValue = Value(newFunction()); break;
                case PinType::Any: inputValue = Value(); break;
                }
                OperationResult operation = m_pOperations->ChangeNodeInputValue(
                    m_pScriptFunction->ID.id, pin->Node->ID, inputIdx, inputValue);
                ReportOperation(operation);
            });
        }

        bool hasConnections = false;
        if (pin)
        {
            for (const Link& link : m_pGraph->GetLinks())
            {
                if (link.StartPinID == pin->ID || link.EndPinID == pin->ID)
                {
                    hasConnections = true;
                    break;
                }
            }
        }
        if (ImGui::MenuItem(ICON_FA_LINK_SLASH "  Disconnect", nullptr, false, hasConnections))
        {
            std::vector<ed::LinkId> links;
            for (const Link& link : m_pGraph->GetLinks())
                if (link.StartPinID == contextPinId || link.EndPinID == contextPinId)
                    links.push_back(link.ID);
            for (const ed::LinkId linkId : links)
                ReportOperation(m_pOperations->Disconnect(m_pScriptFunction->ID.id, linkId));
        }

        if (pin && pin->Node &&
            HasFlag(pin->Node->DefinitionFlags, NodeDefinitionFlags::DynamicInputs) &&
            pin->Kind == PinKind::Input)
        {
            if (pin->Node->CanRemoveInput(pin->ID))
            {
                if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Remove pin"))
                {
                    OperationResult operation = m_pOperations->RemoveDynamicInput(
                        m_pScriptFunction->ID.id, pin->Node->ID, pin->ID);
                    ReportOperation(operation);
                }
            }
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Link Context Menu"))
    {
        Link* link = m_pGraph->FindLink(contextLinkId);

        if (ImGui::MenuItem(ICON_FA_LINK_SLASH "  Disconnect", "Delete", false, link != nullptr))
            ed::DeleteLink(contextLinkId);
        ImGui::EndPopup();
    }

    const ImVec2 paletteSize(520.0f, 560.0f);
    ImGui::SetNextWindowSize(paletteSize, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(paletteSize, paletteSize);
    if (ImGui::BeginPopup("Create New Node",
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        addNodePopupOpened = true;
        const bool hasScriptItemContext =
            paletteScriptItem.kind != Editor::TreeNodeKind::None;
        const bool pureGraph = m_pScriptFunction &&
            HasFlag(m_pScriptFunction->functionDef->flags,
                    NodeDefinitionFlags::Pure);
        ImGui::TextDisabled(ICON_FA_BOLT "  Add node");
        ImGui::SameLine();
        if (hasScriptItemContext)
            ImGui::TextColored(ImVec4(0.78f, 0.62f, 1.0f, 1.0f),
                               ICON_FA_FILTER "  Related to dragged item");
        else if (newNodeLinkPin)
            ImGui::TextColored(ImVec4(0.42f, 0.72f, 1.0f, 1.0f),
                               ICON_FA_FILTER "  Compatible results");
        else
            ImGui::TextDisabled("Type to search all nodes");

        const bool requestSearchFocus = focusPaletteSearch || ImGui::IsWindowAppearing();
        const bool expandPaletteOnOpen = ImGui::IsWindowAppearing();
        if (requestSearchFocus)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##search",
                                     ICON_FA_MAGNIFYING_GLASS " Search nodes...",
                                     &searchFilter))
        {
            searchFilterLower = Utils::to_lower(searchFilter);
            paletteSelection = 0;
        }
        if (requestSearchFocus)
        {
            // SetKeyboardFocusHere() is occasionally overridden by the popup's
            // own appearing-frame navigation setup. Reassert focus on the item
            // that was just submitted so typing works immediately.
            ImGui::SetItemDefaultFocus();
            ImGui::SetKeyboardFocusHere(-1);
            focusPaletteSearch = false;
            paletteSelection = 0;
        }

        static bool contextualSearch = true;
        static bool favoritesOnly = false;
        if (newNodeLinkPin)
        {
            ImGui::Checkbox("Only show compatible nodes", &contextualSearch);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rank and filter results for the pin that opened this palette");
            ImGui::SameLine();
        }
        if (!hasScriptItemContext)
        {
            if (ImGui::Checkbox(ICON_FA_STAR " Favorites only", &favoritesOnly))
                paletteSelection = 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Only search nodes marked as favorites");
        }
        else
        {
            ImGui::TextDisabled("Showing only graph nodes associated with this item");
        }

        const bool isInput = newNodeLinkPin && newNodeLinkPin->Kind == PinKind::Input;
        const bool isOutput = newNodeLinkPin && newNodeLinkPin->Kind == PinKind::Output;
        const bool isFlow = newNodeLinkPin && newNodeLinkPin->Type == PinType::Flow;

        auto FilterContext = [&](const BasicFunctionDefPtr& functionDef)
        {
            if (!contextualSearch) return true;
            if (isFlow) return true;

            if (isInput)
            {
                for (auto& otuput : functionDef->outputs)
                {
                    if (GraphUtils::AreTypesCompatible(otuput.type, newNodeLinkPin->Type))
                        return true;
                }

                return false;
            }
            if (isOutput)
            {
                for (auto& input : functionDef->inputs)
                {
                    if (GraphUtils::AreTypesCompatible(newNodeLinkPin->Type, input.type))
                        return true;
                }

                return false;
            }

            return true;
        };

        auto FilterContextVar = [&](const ScriptPropertyPtr& propertyDef)
        {
            if (!contextualSearch) return true;
            if (isFlow) return true;

            if (newNodeLinkPin)
                return isInput
                    ? GraphUtils::AreTypesCompatible(propertyDef->type, newNodeLinkPin->Type)
                    : GraphUtils::AreTypesCompatible(newNodeLinkPin->Type, propertyDef->type);

            return true;
        };

        auto FilterContextFuncGet = [&](const BasicFunctionDefPtr& functionDef)
        {
            if (!contextualSearch) return true;
            if (isFlow) return true;

            if (newNodeLinkPin)
            {
                std::vector<TypeRef> inputs;
                std::vector<TypeRef> outputs;
                for (const auto& input : functionDef->inputs) inputs.push_back(input.type);
                for (const auto& output : functionDef->outputs) outputs.push_back(output.type);
                const TypeRef functionType =
                    TypeRef::Function(std::move(inputs), std::move(outputs));
                return isInput
                    ? GraphUtils::AreTypesCompatible(functionType, newNodeLinkPin->Type)
                    : GraphUtils::AreTypesCompatible(newNodeLinkPin->Type, functionType);
            }

            return true;
        };

        auto FilterContextMethodGet =
            [&](const BasicFunctionDefPtr& functionDef,
                const TypeRef& instanceType)
        {
            if (!contextualSearch) return true;
            if (isFlow) return true;
            if (!newNodeLinkPin) return true;

            if (isOutput)
                return GraphUtils::AreTypesCompatible(
                    newNodeLinkPin->Type, instanceType);

            std::vector<TypeRef> inputs;
            std::vector<TypeRef> outputs;
            for (const auto& input : functionDef->inputs)
                inputs.push_back(input.type);
            for (const auto& output : functionDef->outputs)
                outputs.push_back(output.type);
            return GraphUtils::AreTypesCompatible(
                TypeRef::Function(std::move(inputs), std::move(outputs)),
                newNodeLinkPin->Type);
        };

        struct Data
        {
            std::string name;
            std::string fullName;
            std::function<NodePtr(IDGenerator&)> creationFun;
            BasicFunctionDefPtr definition;
            std::map<std::string, Data> children;
            int depth;
        };

        Data root;
        root.name = "Nodes";
        root.fullName = "Nodes";
        root.depth = 0;

        auto AddEntry = [&](const std::string& fullName,
                            std::function<NodePtr(IDGenerator&)> creation,
                            BasicFunctionDefPtr definition = {})
        {
            if (!Utils::FilterString(Utils::to_lower(fullName), searchFilterLower))
                return;
            Data* current = &root;
            int depth = 1;
            const std::vector<std::string> tokens = Utils::split(fullName, "::");
            for (const std::string& token : tokens)
            {
                const std::string key = current == &root && token == "Misc" ? "~Misc" : token;
                Data& child = current->children[key];
                child.name = token;
                child.depth = depth++;
                child.fullName = token;
                if (token == tokens.back())
                {
                    child.fullName = fullName;
                    child.creationFun = creation;
                    child.definition = std::move(definition);
                }
                current = &child;
            }
        };

        if (!hasScriptItemContext && !newNodeLinkPin)
            AddEntry("Misc::Comment Box", [](IDGenerator& ids) { return BuildCommentBoxNode(ids); });

        for (auto& def : m_pNodeRegistry->nativeDefinitions)
        {
            // Call
            {
                const std::string getFuncName = "Function::" + def.functionDef->name;

                if (!hasScriptItemContext &&
                    (!pureGraph || HasFlag(def.functionDef->flags,
                                           NodeDefinitionFlags::Pure)) &&
                    Utils::FilterString(Utils::to_lower(def.functionDef->name), searchFilterLower) &&
                    FilterContext(def.functionDef))
                {
                    Data* current = &root;
                    int depth = 1;
                    const std::vector<std::string> tokens = Utils::split(getFuncName, "::");

                    for (const std::string& token : tokens)
                    {
                        Data& child = current->children[token];

                        child.name = token;
                        child.depth = depth;
                        child.fullName = token;

                        if (token == tokens.back())
                        {
                            // Last element!
                            child.fullName = def.functionDef->name;
                            child.creationFun = [=](IDGenerator& idGenerator) { return def.functionDef->MakeNode(idGenerator, ScriptElementID::Invalid); };
                            child.definition = def.functionDef;
                        }

                        current = &child;
                        depth++;
                    }
                }
            }

            // Get
            {
                const std::string getFuncName = "Get::" + def.functionDef->name;

                if (!hasScriptItemContext &&
                    (!pureGraph || HasFlag(def.functionDef->flags,
                                           NodeDefinitionFlags::Pure)) &&
                    Utils::FilterString(Utils::to_lower(getFuncName), searchFilterLower) &&
                    FilterContextFuncGet(def.functionDef))
                {
                    Data* current = &root;
                    int depth = 1;
                    const std::vector<std::string> tokens = Utils::split(getFuncName, "::");

                    for (const std::string& token : tokens)
                    {
                        Data& child = current->children[token];

                        child.name = token;
                        child.depth = depth;
                        child.fullName = token;

                        if (token == tokens.back())
                        {
                            // Last element!
                            child.fullName = getFuncName;
                            const BasicFunctionDefPtr functionDef = def.functionDef;
                            child.creationFun = [functionDef](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildGetFunctionNode(
                                    IDGenerator, functionDef,
                                    ScriptElementID::Invalid);
                            };
                            child.definition = def.functionDef;
                        }

                        current = &child;
                        depth++;
                    }
                }
            }
        }

        for (auto& def : m_pNodeRegistry->compiledDefinitions)
        {
            // Call
            const bool isFlow = def->name.find("Flow") != std::string::npos;

            const std::string getFuncName = (!isFlow ? "Function::" : "") + def->name;

            if (!hasScriptItemContext &&
                (!pureGraph || HasFlag(def->functionDef->flags,
                                       NodeDefinitionFlags::Pure)) &&
                Utils::FilterString(Utils::to_lower(def->name), searchFilterLower) &&
                FilterContext(def->functionDef))
            {
                Data* current = &root;
                int depth = 1;
                const std::vector<std::string> tokens = Utils::split(getFuncName, "::");

                for (const std::string& token : tokens)
                {
                    Data& child = current->children[token];

                    child.name = token;
                    child.depth = depth;
                    child.fullName = token;

                    if (token == tokens.back())
                    {
                        // Last element!
                        const CompiledNodeDefPtr compiledDefinition = def;
                        child.creationFun = [compiledDefinition](IDGenerator& IDGenerator) -> NodePtr
                        {
                            return compiledDefinition->MakeNode(IDGenerator);
                        };
                        child.definition = def->functionDef;
                        child.fullName = def->name;
                    }

                    current = &child;
                    depth++;
                }
            }
        }

        const auto addVariableDefinitions = [&](const ScriptPropertyPtr& def, bool local)
        {
            const ScriptPropertyPtr capturedVariable = def;
            const ScriptElementID functionId = m_pScriptFunction ? m_pScriptFunction->ID : ScriptElementID::Invalid;
            const std::string category = local ? "Local Variables" : "Global Variables";
            const bool draggedVariableMatches = !hasScriptItemContext ||
                (paletteScriptItem.kind == Editor::TreeNodeKind::Variable &&
                 paletteScriptItem.id == def->ID.id &&
                 (!local || paletteScriptItem.ownerId == functionId.id));
            // Get
            {
                const std::string getVar = category + "::Get::" + def->Name;

                if (draggedVariableMatches &&
                    Utils::FilterString(Utils::to_lower(getVar), searchFilterLower) &&
                    FilterContextVar(def))
                {
                    Data* current = &root;
                    int depth = 1;
                    const std::vector<std::string> tokens = Utils::split(getVar, "::");

                    for (const std::string& token : tokens)
                    {
                        Data& child = current->children[token];

                        child.name = token;
                        child.depth = depth;
                        child.fullName = token;

                        if (token == tokens.back())
                        {
                            // Last element!
                            child.fullName = getVar;
                            child.creationFun = [capturedVariable, functionId](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildGetVariableNode(IDGenerator, capturedVariable, ScriptElementID::Invalid, functionId);
                            };
                        }

                        current = &child;
                        depth++;
                    }
                }
            }

            // Set
            {
                const std::string setVar = category + "::Set::" + def->Name;

                if ((!pureGraph || local) && draggedVariableMatches &&
                    Utils::FilterString(Utils::to_lower(setVar), searchFilterLower) &&
                    FilterContextVar(def))
                {
                    Data* current = &root;
                    int depth = 1;
                    const std::vector<std::string> tokens = Utils::split(setVar, "::");

                    for (const std::string& token : tokens)
                    {
                        Data& child = current->children[token];

                        child.name = token;
                        child.depth = depth;
                        child.fullName = token;

                        if (token == tokens.back())
                        {
                            // Last element!
                            child.fullName = setVar;
                            child.creationFun = [capturedVariable, functionId](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildSetVariableNode(IDGenerator, capturedVariable, ScriptElementID::Invalid, functionId);
                            };
                        }

                        current = &child;
                        depth++;
                    }
                }
            }
        };

        if (m_pScriptFunction)
            for (const ScriptPropertyPtr& variable : m_pScriptFunction->variables)
                addVariableDefinitions(variable, true);
        for (const ScriptPropertyPtr& variable : m_pScript->variables)
            addVariableDefinitions(variable, false);

        for (auto& def : m_pScript->functions)
        {
            const ScriptFunctionPtr capturedFunction = def;
            // Call
            {
                const std::string fullFuncName = "Functions::" + def->functionDef->name;

                if ((!hasScriptItemContext ||
                     (paletteScriptItem.kind == Editor::TreeNodeKind::Function &&
                      paletteScriptItem.id == def->ID.id)) &&
                    (!pureGraph || HasFlag(def->functionDef->flags,
                                           NodeDefinitionFlags::Pure)) &&
                    Utils::FilterString(Utils::to_lower(fullFuncName), searchFilterLower) &&
                    FilterContext(def->functionDef))
                {
                    Data* current = &root;
                    int depth = 1;
                    const std::vector<std::string> tokens = Utils::split(fullFuncName, "::");

                    for (const std::string& token : tokens)
                    {
                        Data& child = current->children[token];

                        child.name = token;
                        child.depth = depth;
                        child.fullName = token;

                        if (token == tokens.back())
                        {
                            // Last element!
                            child.fullName = fullFuncName;
                            child.creationFun = [capturedFunction](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return capturedFunction->functionDef->MakeNode(
                                    IDGenerator, capturedFunction->ID);
                            };
                            child.definition = capturedFunction->functionDef;
                        }

                        current = &child;
                        depth++;
                    }
                }
            }

            // Get
            {
                const std::string getFuncName = "Functions::Get::" + def->functionDef->name;

                if ((!hasScriptItemContext ||
                     (paletteScriptItem.kind == Editor::TreeNodeKind::Function &&
                      paletteScriptItem.id == def->ID.id)) &&
                    (!pureGraph || HasFlag(def->functionDef->flags,
                                           NodeDefinitionFlags::Pure)) &&
                    Utils::FilterString(Utils::to_lower(getFuncName), searchFilterLower) &&
                    FilterContextFuncGet(def->functionDef))
                {
                    Data* current = &root;
                    int depth = 1;
                    const std::vector<std::string> tokens = Utils::split(getFuncName, "::");

                    for (const std::string& token : tokens)
                    {
                        Data& child = current->children[token];

                        child.name = token;
                        child.depth = depth;
                        child.fullName = token;

                        if (token == tokens.back())
                        {
                            // Last element!
                            child.fullName = getFuncName;
                            child.creationFun = [capturedFunction](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildGetFunctionNode(
                                    IDGenerator, capturedFunction->functionDef,
                                    capturedFunction->ID);
                            };
                            child.definition = capturedFunction->functionDef;
                        }

                        current = &child;
                        depth++;
                    }
                }
            }
        }

        // Class-specific nodes must be added before the palette tree is rendered.
        // These nodes work both outside a class and inside methods/constructors;
        // inside a class, connect them to the This node for the current instance.
        for (const ScriptClassPtr& scriptClass : m_pScript->classes)
        {
            const ScriptClassPtr capturedClass = scriptClass;
            const bool wholeClassContext =
                hasScriptItemContext &&
                paletteScriptItem.kind == Editor::TreeNodeKind::Class &&
                paletteScriptItem.id == scriptClass->ID.id;
            const bool constructorContext =
                hasScriptItemContext &&
                paletteScriptItem.kind == Editor::TreeNodeKind::Constructor &&
                paletteScriptItem.ownerId == scriptClass->ID.id;
            if (!pureGraph &&
                (!hasScriptItemContext || wholeClassContext || constructorContext))
                AddEntry("Classes::" + scriptClass->Name + "::Construct",
                    [capturedClass](IDGenerator& ids)
                    {
                        return BuildConstructObjectNode(ids, capturedClass);
                    });
            for (const ScriptPropertyPtr& property : scriptClass->properties)
            {
                const ScriptPropertyPtr capturedProperty = property;
                const bool propertyContext =
                    hasScriptItemContext &&
                    paletteScriptItem.kind == Editor::TreeNodeKind::ClassProperty &&
                    paletteScriptItem.id == property->ID.id;
                if (!hasScriptItemContext || wholeClassContext || propertyContext)
                {
                    AddEntry("Classes::" + scriptClass->Name + "::Properties::Get " + property->Name,
                        [capturedProperty, capturedClass](IDGenerator& ids)
                        {
                            return BuildGetPropertyNode(
                                ids, capturedProperty, ScriptElementID::Invalid,
                                TypeRef::Object(capturedClass->ID.id, capturedClass->Name));
                        });
                    if (!pureGraph)
                        AddEntry("Classes::" + scriptClass->Name + "::Properties::Set " + property->Name,
                            [capturedProperty, capturedClass](IDGenerator& ids)
                            {
                                return BuildSetPropertyNode(
                                    ids, capturedProperty, ScriptElementID::Invalid,
                                    TypeRef::Object(capturedClass->ID.id, capturedClass->Name));
                            });
                }
            }
            for (const ScriptFunctionPtr& method : scriptClass->methods)
            {
                const ScriptFunctionPtr capturedMethod = method;
                const TypeRef instanceType =
                    TypeRef::Object(scriptClass->ID.id, scriptClass->Name);
                const bool methodContext =
                    hasScriptItemContext &&
                    paletteScriptItem.kind == Editor::TreeNodeKind::ClassMethod &&
                    paletteScriptItem.id == method->ID.id;
                const bool relatedContext =
                    !hasScriptItemContext || wholeClassContext || methodContext;
                if (relatedContext &&
                    FilterContextMethodGet(
                        method->functionDef, instanceType))
                    AddEntry("Classes::" + scriptClass->Name +
                        "::Methods::Get " + method->functionDef->name,
                        [capturedMethod, capturedClass](IDGenerator& ids)
                        {
                            return BuildGetMethodNode(
                                ids, capturedMethod, ScriptElementID::Invalid,
                                TypeRef::Object(
                                    capturedClass->ID.id,
                                    capturedClass->Name));
                        }, capturedMethod->functionDef);
                if ((!pureGraph ||
                     HasFlag(method->functionDef->flags,
                             NodeDefinitionFlags::Pure)) &&
                    relatedContext)
                    AddEntry("Classes::" + scriptClass->Name + "::Methods::Call " +
                        method->functionDef->name,
                        [capturedMethod, capturedClass](IDGenerator& ids)
                        {
                            return BuildMethodCallNode(
                                ids, capturedMethod, ScriptElementID::Invalid,
                                TypeRef::Object(capturedClass->ID.id, capturedClass->Name));
                        }, capturedMethod->functionDef);
            }
        }
        if (!hasScriptItemContext &&
            ScriptUtils::FindOwningClass(*m_pScript, m_pScriptFunction->ID.id))
        {
            const ScriptClassPtr ownerClass = m_pScriptFunction
                ? ScriptUtils::FindOwningClass(*m_pScript, m_pScriptFunction->ID.id)
                : nullptr;
            AddEntry("Classes::This", [ownerClass](IDGenerator& ids)
            {
                return BuildThisNode(ids, ownerClass
                    ? TypeRef::Object(ownerClass->ID.id, ownerClass->Name)
                    : TypeRef(PinType::Object));
            });
        }

        // TODO: Only show return if we can return!
        {
            {
                const std::string fullFuncName = "Flow::Return";

                if (!hasScriptItemContext &&
                    Utils::FilterString(Utils::to_lower(fullFuncName), searchFilterLower))
                {
                    Data* current = &root;
                    int depth = 1;
                    const std::vector<std::string> tokens = Utils::split(fullFuncName, "::");

                    for (const std::string& token : tokens)
                    {
                        Data& child = current->children[token];

                        child.name = token;
                        child.depth = depth;
                        child.fullName = token;

                        if (token == tokens.back())
                        {
                            // Last element!
                            child.fullName = fullFuncName;
                            const ScriptFunctionPtr function = m_pScriptFunction;
                            child.creationFun = [function](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildReturnNode(IDGenerator, *function);
                            };
                            child.definition = m_pScriptFunction->functionDef;
                        }

                        current = &child;
                        depth++;
                    }
                }
            }
        }

        NodePtr node = nullptr;
        std::string createdNodeKey;
        bool creationTransactionStarted = false;
        auto newNodePostion = openPopupPosition;

        std::vector<const Data*> results;
        const bool applyFavoritesOnly = favoritesOnly && !hasScriptItemContext;
        std::function<void(const Data&)> collectResults = [&](const Data& entry)
        {
            if (entry.creationFun &&
                (!applyFavoritesOnly || favoriteNodeTypes.count(entry.fullName) != 0))
                results.push_back(&entry);
            for (const auto& [_, child] : entry.children)
                collectResults(child);
        };
        collectResults(root);

        auto recentRank = [&](const std::string& key)
        {
            const auto it = std::find(recentNodeTypes.begin(), recentNodeTypes.end(), key);
            return it == recentNodeTypes.end()
                ? std::numeric_limits<int>::max()
                : static_cast<int>(std::distance(recentNodeTypes.begin(), it));
        };

        auto palettePresentation = [](const std::string& fullName)
        {
            std::pair<const char*, const char*> presentation = {
                ICON_FA_CUBE, "Create a graph node"
            };
            if (fullName == "Misc::Comment Box")
                presentation = { ICON_FA_NOTE_STICKY, "Add a resizable annotation around related nodes" };
            else if (fullName.rfind("Flow::", 0) == 0)
                presentation = { ICON_FA_CODE_BRANCH, "Control the order in which the graph executes" };
            else if (fullName.rfind("Variables::", 0) == 0)
                presentation = { ICON_FA_DATABASE, "Read or update script-level data" };
            else if (fullName.rfind("Functions::", 0) == 0 ||
                     fullName.rfind("Get::", 0) == 0)
                presentation = { ICON_FA_CODE, "Call a function or store it as a value" };
            else if (fullName.rfind("Classes::", 0) == 0)
                presentation = { ICON_FA_CUBES, "Construct an object or access one of its members" };
            return presentation;
        };

        if (results.empty())
        {
            paletteSelection = 0;
            ImGui::Spacing();
            ImGui::TextDisabled("No matching nodes");
            ImGui::TextDisabled("Try fewer characters or disable compatibility filtering");
        }
        else
        {
            paletteSelection = ImClamp(paletteSelection, 0, static_cast<int>(results.size()) - 1);
            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow), false))
                paletteSelection = ImMin(paletteSelection + 1, static_cast<int>(results.size()) - 1);
            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow), false))
                paletteSelection = ImMax(paletteSelection - 1, 0);

            const bool createSelected =
                ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false);
            const Data* selected = results[paletteSelection];
            const BasicFunctionDefPtr selectedDefinition =
                selected->definition;
            static std::string documentedNodeKey;
            static NodePtr documentedNode;
            if (expandPaletteOnOpen ||
                documentedNodeKey != selected->fullName)
            {
                IDGenerator previewIds;
                documentedNode = selected->creationFun(previewIds);
                NodeUtils::NormalizeDocumentation(documentedNode);
                documentedNodeKey = selected->fullName;
            }
            constexpr float detailLines = 8.0f;
            const float descriptionHeight =
                ImGui::GetTextLineHeightWithSpacing() * detailLines +
                ImGui::GetStyle().ItemSpacing.y * 2.0f;
            ImGui::BeginChild("##paletteTree",
                              ImVec2(0, -descriptionHeight), false,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);

            std::function<void(const Data&, int)> drawTree =
                [&](const Data& parent, int depth)
            {
                for (const auto& [_, child] : parent.children)
                {
                    std::function<bool(const Data&)> containsVisibleNode =
                        [&](const Data& candidate)
                    {
                        if (candidate.creationFun &&
                            (!applyFavoritesOnly ||
                             favoriteNodeTypes.count(candidate.fullName) != 0))
                            return true;
                        for (const auto& [ignoredName, descendant] : candidate.children)
                        {
                            (void)ignoredName;
                            if (containsVisibleNode(descendant))
                                return true;
                        }
                        return false;
                    };
                    if (!containsVisibleNode(child))
                        continue;

                    const bool hasChildren = !child.children.empty();
                    if (hasChildren)
                    {
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (expandPaletteOnOpen || !searchFilter.empty())
                            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                        else if (depth == 0)
                            flags |= ImGuiTreeNodeFlags_DefaultOpen;
                        const std::string groupLabel =
                            std::string(ICON_FA_FOLDER) + "  " + child.name +
                            "##group-" + child.fullName + "-" + std::to_string(depth);
                        const bool open = ImGui::TreeNodeEx(groupLabel.c_str(), flags);
                        if (open)
                        {
                            drawTree(child, depth + 1);
                            ImGui::TreePop();
                        }
                        continue;
                    }
                    if (!child.creationFun)
                        continue;

                    const auto resultIt = std::find(results.begin(), results.end(), &child);
                    if (resultIt == results.end())
                        continue;
                    const int index = static_cast<int>(std::distance(results.begin(), resultIt));
                    const bool favorite = favoriteNodeTypes.count(child.fullName) != 0;
                    const bool recent =
                        recentRank(child.fullName) != std::numeric_limits<int>::max();
                    const auto [icon, description] = palettePresentation(child.fullName);
                    (void)description;

                    ImGui::PushID(child.fullName.c_str());
                    ImGui::PushStyleColor(ImGuiCol_Text, favorite
                        ? ImVec4(0.96f, 0.72f, 0.24f, 1.0f)
                        : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::SmallButton(ICON_FA_STAR "##favorite"))
                    {
                        if (favorite)
                            favoriteNodeTypes.erase(child.fullName);
                        else
                            favoriteNodeTypes.insert(child.fullName);
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(favorite
                            ? "Remove from favorites" : "Add to favorites");
                    ImGui::SameLine();

                    const std::string label = std::string(icon) + "  " + child.name +
                        (recent ? "   " ICON_FA_CLOCK_ROTATE_LEFT : "") +
                        "##node-" + child.fullName;
                    const bool activated = ImGui::Selectable(
                        label.c_str(), paletteSelection == index,
                        ImGuiSelectableFlags_AllowDoubleClick);
                    if (ImGui::IsItemHovered() || activated)
                        paletteSelection = index;

                    if (activated || (createSelected && paletteSelection == index))
                    {
                        OperationResult begun = m_pOperations->BeginTransaction("Create node");
                        ReportOperation(begun);
                        if (begun)
                        {
                            creationTransactionStarted = true;
                            node = SpawnNode(child.creationFun(*m_pIDGenerator));
                            createdNodeKey = child.fullName;
                        }
                    }
                    ImGui::PopID();
                }
            };
            drawTree(root, 0);
            ImGui::EndChild();

            const auto [selectedIcon, selectedDescription] = palettePresentation(selected->fullName);
            ImGui::Separator();
            ImGui::BeginChild("##paletteDetails", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            ImGui::Text("%s  %s", selectedIcon, selected->fullName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("Enter to create");
            ImGui::TextWrapped("%s", documentedNode &&
                                      !documentedNode->Description.empty()
                ? documentedNode->Description.c_str()
                : selectedDescription);

            const auto drawPins = [](const char* heading, const std::vector<Pin>& pins, const bool shouldExcludeFlow)
            {
                if (pins.empty()) return;
                ImGui::Spacing();
                ImGui::TextDisabled("%s", heading);
                for (const Pin& pin : pins)
                {
                    if (shouldExcludeFlow && pin.Type == PinType::Flow)
                        continue;

                    const std::string label = (pin.Name.empty() ? "Flow" : pin.Name) + ": " + pin.Description.c_str() + " (" + pin.Type.ToString() + ")";
                    ImGui::BulletText("%s", label.c_str());
                }
            };
            if (documentedNode)
            {
                drawPins("INPUTS", documentedNode->Inputs, true);
                if (selectedDefinition && HasFlag(selectedDefinition->flags, NodeDefinitionFlags::DynamicInputs))
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("ADDITIONAL INPUTS");

                    const std::string label = selectedDefinition->dynamicInputProps.description + " (" + selectedDefinition->dynamicInputProps.type.ToString() + ")";

                    ImGui::BulletText("%s", label.c_str());
                }
                drawPins("OUTPUTS", documentedNode->Outputs, documentedNode->Category != NodeCategory::Flow);
            }
            ImGui::EndChild();
        }

        if (node)
        {
            recentNodeTypes.erase(
                std::remove(recentNodeTypes.begin(), recentNodeTypes.end(), createdNodeKey),
                recentNodeTypes.end());
            recentNodeTypes.insert(recentNodeTypes.begin(), createdNodeKey);
            if (recentNodeTypes.size() > 8)
                recentNodeTypes.resize(8);

            NodeUtils::BuildNode(node);

            createNewNode = false;

            amendNextNodePosition.insert(static_cast<int>(node->ID.Get()));
            ed::SetNodePosition(node->ID, newNodePostion);

            if (auto startPin = newNodeLinkPin)
            {
                auto& pins = startPin->Kind == PinKind::Input ? node->Outputs : node->Inputs;

                for (auto& pin : pins)
                {
                    if (m_pGraph->CanCreateLink(startPin, &pin, processedNodes) == ELinkQueryResult::Possible)
                    {
                        auto endPin = &pin;
                        if (startPin->Kind == PinKind::Input)
                            std::swap(startPin, endPin);

                        OperationResult operation = m_pOperations->Connect(
                            m_pScriptFunction->ID.id, startPin->ID, endPin->ID, processedNodes);
                        ReportOperation(operation);
                        break;
                    }
                }
            }

            if (m_pOperations->IsTransactionActive())
                ReportOperation(m_pOperations->CommitTransaction());

            ImGui::CloseCurrentPopup();
        }

        if (creationTransactionStarted && m_pOperations->IsTransactionActive())
        {
            ReportOperation(m_pOperations->CommitTransaction());
        }

        ImGui::EndPopup();
    }
    else
    {
        createNewNode = false;
        addNodePopupOpened = false;
        paletteScriptItem = {};
    }
    ImGui::PopStyleVar();
    ed::Resume();
}

static void ForceMinWidth(const char* text, float minWidth, float padding = 20.0f)
{
    const float contentWidth = ImGui::CalcTextSize(text).x + padding;
    const float widthToUse = contentWidth > minWidth ? contentWidth : minWidth;

    ImGui::SetNextItemWidth(widthToUse);
}

static void ForceMinWidth(double value, float minWidth, float padding = 20.0f)
{
    // Get the size of the text based on the content of the input
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.15g", value);

    ForceMinWidth(buffer, minWidth);
}

/* static */ bool GraphViewUtils::DrawTypeInputImpl(const PinType pinType, Value& inputValue)
{
    if (pinType == PinType::Bool)
    {
        bool& value = inputValue.as.boolean;
        return ImGui::Checkbox("", &value);
    }
    else if (pinType == PinType::String)
    {
        ObjString* a = asString(inputValue);

        std::string temp = a->chars;

        ForceMinWidth(temp.c_str(), 30.0f);
        if (ImGui::InputText("##edit", &temp))
        {
            inputValue = Value(copyString(temp.c_str(), temp.size()));
            return true;
        }
    }
    else if (pinType == PinType::Float)
    {
        double& value = inputValue.as.number;

        ForceMinWidth(value, 30.0f);
        return ImGui::InputDouble("##edit", &value, 0, 0, "%.15g");
    }
    else if (pinType == PinType::Range)
    {
        ObjRange* range = asRange(inputValue);
        double min = range->min;
        double max = range->max;
        bool changed = false;
        ImGui::PushID("range-min");
        ForceMinWidth(min, 30.0f);
        changed |= ImGui::InputDouble("##edit", &min, 0, 0, "%.15g");
        ImGui::PopID();
        ImGui::TextUnformatted("..");
        ImGui::PushID("range-max");
        ForceMinWidth(max, 30.0f);
        changed |= ImGui::InputDouble("##edit", &max, 0, 0, "%.15g");
        ImGui::PopID();
        if (changed)
            inputValue = Value(newRange(min, max));
        return changed;
    }

    return false;
}

/* static */  bool GraphViewUtils::DrawTypeInput(const PinType pinType, Value& inputValue)
{
    if (pinType == PinType::Bool || pinType == PinType::String || pinType == PinType::Float ||
        pinType == PinType::Range)
    {
        return DrawTypeInputImpl(pinType, inputValue);
    }
    else if (pinType == PinType::Any)
    {
        PinType currentType = PinType::Any;
        if (isBoolean(inputValue))
        {
            currentType = PinType::Bool;
        }
        else if (isNumber(inputValue))
        {
            currentType = PinType::Float;
        }
        else if (isString(inputValue))
        {
            currentType = PinType::String;
        }
        else if (isRange(inputValue))
        {
            currentType = PinType::Range;
        }

        return DrawTypeInputImpl(currentType, inputValue);
    }

    return false;
}

void GraphViewUtils::DrawTypeSelection(Value& inputValue, std::function<void(PinType type)> onChange)
{
    int currentIdx = 0;

    if (isBoolean(inputValue))
        currentIdx = 0;
    else if (isNumber(inputValue))
        currentIdx = 1;
    else if (isString(inputValue))
        currentIdx = 2;
    else if (isList(inputValue))
        currentIdx = 3;
    else if (isFunction(inputValue))
        currentIdx = 4;
    else if (isRange(inputValue))
        currentIdx = 5;
    else
        currentIdx = 6;

    ImGui::PushItemWidth(80.0f);
    if (ImGui::Combo("Type", &currentIdx, "Bool\0Number\0String\0List\0Function\0Range\0Any\0"))
    {
        if (currentIdx == 0)
            onChange(PinType::Bool);
        else if (currentIdx == 1)
            onChange(PinType::Float);
        else if (currentIdx == 2)
            onChange(PinType::String);
        else if (currentIdx == 3)
            onChange(PinType::List);
        else if (currentIdx == 4)
            onChange(PinType::Function);
        else if (currentIdx == 5)
            onChange(PinType::Range);
        else
            onChange(PinType::Any);
    }
    ImGui::PopItemWidth();
}

namespace
{
void DrawTypeRefEditor(const Script& script, TypeRef& type, bool& changed,
                       int depth)
{
    if (depth > 8)
    {
        ImGui::TextDisabled("Maximum type nesting reached");
        return;
    }

    std::vector<TypeRef> choices = {
        TypeRef(PinType::Any),
        TypeRef(PinType::Bool),
        TypeRef(PinType::Float),
        TypeRef(PinType::String),
        TypeRef(PinType::Range),
        TypeRef::List(PinType::Any),
        TypeRef::Function({}, {}),
    };
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        const TypeRef classType =
            TypeRef::Object(scriptClass->ID.id, scriptClass->Name);
        choices.push_back(classType);
    }

    const std::string preview = type.ToString();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##type-kind", preview.c_str()))
    {
        for (const TypeRef& choice : choices)
        {
            const std::string label = choice.ToString();
            const bool selected = type.kind == choice.kind &&
                (choice.kind != PinType::Object ||
                 type.classId == choice.classId);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                type = choice;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (type.kind == PinType::List)
    {
        if (type.parameters.size() != 1)
        {
            type = TypeRef::List(PinType::Any);
            changed = true;
        }
        ImGui::Indent();
        ImGui::TextDisabled("Element type");
        ImGui::PushID("list-element");
        DrawTypeRefEditor(script, type.parameters[0], changed, depth + 1);
        ImGui::PopID();
        ImGui::Unindent();
    }
    else if (type.kind == PinType::Function)
    {
        int inputCount = type.functionInputCount;
        if (inputCount < 0 || static_cast<size_t>(inputCount) > type.parameters.size())
        {
            inputCount = 0;
            type.functionInputCount = 0;
            changed = true;
        }

        ImGui::Indent();
        ImGui::TextDisabled("Signature inputs");
        for (int i = 0; i < inputCount;)
        {
            ImGui::PushID(i);
            ImGui::TextDisabled("%d", i + 1);
            ImGui::SameLine();
            ImGui::BeginGroup();
            DrawTypeRefEditor(script, type.parameters[static_cast<size_t>(i)],
                              changed, depth + 1);
            ImGui::EndGroup();
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                type.parameters.erase(type.parameters.begin() + i);
                --type.functionInputCount;
                --inputCount;
                changed = true;
                ImGui::PopID();
                continue;
            }
            ImGui::PopID();
            ++i;
        }
        if (ImGui::SmallButton("+ Input"))
        {
            type.parameters.insert(
                type.parameters.begin() + inputCount, TypeRef(PinType::Any));
            ++type.functionInputCount;
            ++inputCount;
            changed = true;
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Signature outputs");
        for (size_t i = static_cast<size_t>(inputCount);
             i < type.parameters.size();)
        {
            ImGui::PushID(static_cast<int>(i) + 1000);
            ImGui::TextDisabled("%zu", i - static_cast<size_t>(inputCount) + 1);
            ImGui::SameLine();
            ImGui::BeginGroup();
            DrawTypeRefEditor(script, type.parameters[i], changed, depth + 1);
            ImGui::EndGroup();
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                type.parameters.erase(type.parameters.begin() + i);
                changed = true;
                ImGui::PopID();
                continue;
            }
            ImGui::PopID();
            ++i;
        }
        if (ImGui::SmallButton("+ Output"))
        {
            type.parameters.push_back(TypeRef(PinType::Any));
            changed = true;
        }
        ImGui::Unindent();
    }
}
}

void GraphViewUtils::DrawDeclaredTypeSelection(
    const Script& script, const TypeRef& current,
    std::function<void(TypeRef type)> onChange, const char* label,
    bool notifyWhenReselected)
{
    ImGui::TextDisabled("%s", label);
    TypeRef edited = current;
    bool changed = false;
    DrawTypeRefEditor(script, edited, changed, 0);
    if (changed && (notifyWhenReselected || edited != current))
        onChange(std::move(edited));
}

void GraphView::RequestAutoLayout()
{
    if (CanAutoLayout())
        autoLayoutRequested = true;
}

bool GraphView::CanAutoLayout() const
{
    if (!m_pGraph || !m_pOperations || !m_pScriptFunction || autoLayoutRequested || autoLayoutTransactionActive)
        return false;

    return std::count_if(m_pGraph->GetNodes().begin(), m_pGraph->GetNodes().end(), [](const NodePtr& node)
    {
        return node && node->Type != NodeType::CommentBox;
    }) >= 2;
}

void GraphView::BeginAutoLayout()
{
    if (!autoLayoutRequested || autoLayoutTransactionActive || !m_pGraph || !m_pOperations || !m_pScriptFunction)
        return;

    std::vector<GraphLayout::Node> layoutNodes;
    layoutNodes.reserve(m_pGraph->GetNodes().size());
    for (const NodePtr& node : m_pGraph->GetNodes())
    {
        if (!node || node->Type == NodeType::CommentBox)
            continue;

        const ImVec2 position = ed::GetNodePosition(node->ID);
        const ImVec2 size = ed::GetNodeSize(node->ID);
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || size.x <= 0.0f || size.y <= 0.0f)
            return;
        layoutNodes.push_back({
            static_cast<int>(node->ID.Get()),
            position,
            size,
            node->Category == NodeCategory::Begin,
            !GraphUtils::IsNodeImplicit(node),
        });
    }

    if (layoutNodes.size() < 2)
    {
        autoLayoutRequested = false;
        return;
    }

    std::vector<GraphLayout::Edge> layoutEdges;
    layoutEdges.reserve(m_pGraph->GetLinks().size());
    auto* internalEditor = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(m_Editor);
    const auto pinOffsetY = [&](const Pin& pin)
    {
        const ax::NodeEditor::Detail::Pin* editorPin = internalEditor ? internalEditor->FindPin(pin.ID) : nullptr;
        if (!editorPin || editorPin->m_Bounds.GetHeight() <= 0.0f)
            return std::pair<bool, float>(false, 0.0f);
        return std::pair<bool, float>(true, editorPin->m_Pivot.GetCenter().y - ed::GetNodePosition(pin.Node->ID).y);
    };
    for (const Link& link : m_pGraph->GetLinks())
    {
        const Pin* output = m_pGraph->FindPin(link.StartPinID);
        const Pin* input = m_pGraph->FindPin(link.EndPinID);
        if (!output || !input || !output->Node || !input->Node || output->Node->Type == NodeType::CommentBox || input->Node->Type == NodeType::CommentBox)
            continue;
        const auto [hasOutputOffset, outputOffsetY] = pinOffsetY(*output);
        const auto [hasInputOffset, inputOffsetY] = pinOffsetY(*input);
        layoutEdges.push_back({
            static_cast<int>(output->Node->ID.Get()),
            static_cast<int>(input->Node->ID.Get()),
            GraphUtils::FindNodeOutputIdx(*output),
            GraphUtils::FindNodeInputIdx(*input),
            output->Type == PinType::Flow,
            hasOutputOffset && hasInputOffset,
            outputOffsetY,
            inputOffsetY,
        });
    }

    const std::vector<GraphLayout::Position> positions = GraphLayout::Calculate(layoutNodes, layoutEdges);
    const float gridSpacing = nodeGridSpacing;
    const auto alignToGrid = [gridSpacing](float value)
    {
        return gridSpacing > 0.0f ? std::floor(value / gridSpacing + 0.5f) * gridSpacing : value;
    };

    std::vector<GraphLayout::Position> changedPositions;
    for (const GraphLayout::Position& position : positions)
    {
        const ImVec2 aligned(alignToGrid(position.value.x), alignToGrid(position.value.y));
        const ImVec2 current = ed::GetNodePosition(ed::NodeId(position.id));
        if (current.x != aligned.x || current.y != aligned.y)
            changedPositions.push_back({ position.id, aligned });
    }
    autoLayoutRequested = false;
    if (changedPositions.empty())
    {
        m_NavigateToContentOnNextFrame = true;
        return;
    }

    const OperationResult begun = m_pOperations->BeginTransaction("Auto layout");
    ReportOperation(begun);
    if (!begun)
        return;

    autoLayoutTransactionActive = true;
    autoLayoutFailed = false;
    pendingAutoLayoutNodeStates.clear();
    for (const GraphLayout::Position& position : changedPositions)
        pendingAutoLayoutNodeStates.insert(position.id);
    for (const GraphLayout::Position& position : changedPositions)
        ed::SetNodePosition(ed::NodeId(position.id), position.value);
}

void GraphView::FinishAutoLayout(bool cancelIfIncomplete)
{
    if (!autoLayoutTransactionActive || (!pendingAutoLayoutNodeStates.empty() && !cancelIfIncomplete))
        return;

    const bool complete = pendingAutoLayoutNodeStates.empty() && !autoLayoutFailed;
    const OperationResult result = complete ? m_pOperations->CommitTransaction() : m_pOperations->CancelTransaction();
    if (complete || !result)
        ReportOperation(result);
    autoLayoutTransactionActive = false;
    autoLayoutFailed = false;
    pendingAutoLayoutNodeStates.clear();
    if (complete && result)
        m_NavigateToContentOnNextFrame = true;
}
