#pragma once

#include "graphView.h"

#include "nodeRegistry.h"

#include "../native/nodes/begin.h"

#include <Compiler.h>

#include <misc/imgui_stdlib.h>

// Graph View
int GraphView::GetNextId()
{
    return m_pIDGenerator->GetNextId();
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

void GraphView::DrawPinIcon(const Pin& pin, bool connected, int alpha)
{
    const ax::Drawing::IconType iconType = GetPinIcon(pin.Type);
    ImColor color = GetIconColor(pin.Type);
    color.Value.w = alpha / 255.0f;

    ax::Widgets::Icon(ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)), iconType, connected, color, ImColor(32, 32, 32, alpha));
}

void GraphView::BuildNode(const NodePtr& node)
{
    for (Pin& input : node->Inputs)
    {
        input.Node = node;
        input.Kind = PinKind::Input;
    }

    for (Pin& output : node->Outputs)
    {
        output.Node = node;
        output.Kind = PinKind::Output;
    }
}

NodePtr GraphView::SpawnNode(const NodePtr& node)
{
    BuildNode(node);
    return m_pGraph->AddNode(node);
}

void GraphView::setIDGenerator(IDGenerator& generator)
{
    m_pIDGenerator = &generator;
}

void GraphView::setNodeRegistry(NodeRegistry& nodeRegistry)
{
    m_pNodeRegistry = &nodeRegistry;
}

void GraphView::SetGraph(Graph* pTargetGraph)
{
    m_pGraph = pTargetGraph;

    ed::Config config;

    config.SettingsFile = "Blueprints.json";

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

        node->State.assign(data, size);

        self->TouchNode(nodeId);

        return true;
    };

    m_Editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(m_Editor);

    SpawnNode(BuildBeginNode(*m_pIDGenerator));

    ed::NavigateToContent();
}

void GraphView::Destroy()
{
    if (m_Editor)
    {
        ed::DestroyEditor(m_Editor);
        m_Editor = nullptr;
        m_pGraph = nullptr;
    }
}

void GraphView::OnFrame(float deltaTime)
{
    UpdateTouch();

    ed::SetCurrentEditor(m_Editor);
}

void GraphView::DrawNodeEditor(ImTextureID& headerBackground, int headerWidth, int headerHeight)
{
    ed::Begin("Node editor");

    {
        ImVec2 cursorTopLeft = ImGui::GetCursorScreenPos();

        ax::NodeEditor::Utilities::BlueprintNodeBuilder builder(headerBackground, headerWidth, headerHeight);

        // Simple nodes
        for (const NodePtr& node : m_pGraph->GetNodes())
        {
            if (node->Type != NodeType::Blueprint && node->Type != NodeType::Simple)
                continue;

            const bool isSimple = node->Type == NodeType::Simple;

            builder.Begin(node->ID);
            if (!isSimple)
            {
                builder.Header(node->Color);
                ImGui::Spring(0);
                ImGui::TextUnformatted(node->Name.c_str());
                ImGui::Spring(1);
                ImGui::Dummy(ImVec2(0, 28));
                ImGui::Spring(0);
                builder.EndHeader();
            }

            int idx = 0;
            for (const Pin& input : node->Inputs)
            {
                float alpha = ImGui::GetStyle().Alpha;
                if (newLinkPin && !m_pGraph->CanCreateLink(newLinkPin, &input) && &input != newLinkPin)
                    alpha = alpha * (48.0f / 255.0f);

                builder.Input(input.ID);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                DrawPinIcon(input, m_pGraph->IsPinLinked(input.ID), (int)(alpha * 255));
                ImGui::Spring(0);
                if (!input.Name.empty())
                {
                    ImGui::TextUnformatted(input.Name.c_str());
                    ImGui::Spring(0);
                }
                if (input.Type == PinType::Bool)
                {
                    if (!m_pGraph->IsPinLinked(input.ID))
                    {
                        bool& value = node->InputValues[idx].as.boolean;
                        ImGui::Checkbox("", &value);
                        ImGui::Spring(0);
                    }
                }
                else if (input.Type == PinType::String)
                {
                    if (!m_pGraph->IsPinLinked(input.ID))
                    {
                        ObjString* a = asString(node->InputValues[idx]);

                        ImGui::PushItemWidth(100.0f);
                        std::string temp = a->chars;
                        if (ImGui::InputText("##edit", &temp))
                        {
                            node->InputValues[idx] = Value(copyString(temp.c_str(), temp.size()));
                        }
                        ImGui::PopItemWidth();
                        ImGui::Spring(0);

                    }
                }
                else if (input.Type == PinType::Float)
                {
                    if (!m_pGraph->IsPinLinked(input.ID))
                    {
                        double& value = node->InputValues[idx].as.number;

                        ImGui::PushItemWidth(100.0f);
                        ImGui::InputDouble("##edit", &value);
                        ImGui::PopItemWidth();
                        ImGui::Spring(0);

                    }
                }
                ImGui::PopStyleVar();
                builder.EndInput();
                ++idx;
            }

            if (isSimple)
            {
                builder.Middle();

                ImGui::Spring(1, 0);
                ImGui::TextUnformatted(node->Name.c_str());
                ImGui::Spring(1, 0);
            }

            for (const Pin& output : node->Outputs)
            {
                float alpha = ImGui::GetStyle().Alpha;
                if (newLinkPin && !m_pGraph->CanCreateLink(newLinkPin, &output) && &output != newLinkPin)
                    alpha = alpha * (48.0f / 255.0f);

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
                if (!output.Name.empty())
                {
                    ImGui::Spring(0);
                    ImGui::TextUnformatted(output.Name.c_str());
                }
                ImGui::Spring(0);
                DrawPinIcon(output, m_pGraph->IsPinLinked(output.ID), (int)(alpha * 255));
                ImGui::PopStyleVar();
                builder.EndOutput();
            }

            builder.End();
        }

        // Comment nodes
        for (const NodePtr& node : m_pGraph->GetNodes())
        {
            if (node->Type != NodeType::Comment)
                continue;

            const float commentAlpha = 0.75f;

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentAlpha);
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 64));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 64));
            ed::BeginNode(node->ID);
            ImGui::PushID(node->ID.AsPointer());
            ImGui::BeginVertical("content");
            ImGui::BeginHorizontal("horizontal");
            ImGui::Spring(1);
            ImGui::TextUnformatted(node->Name.c_str());
            ImGui::Spring(1);
            ImGui::EndHorizontal();
            ed::Group(node->Size);
            ImGui::EndVertical();
            ImGui::PopID();
            ed::EndNode();
            ed::PopStyleColor(2);
            ImGui::PopStyleVar();

            if (ed::BeginGroupHint(node->ID))
            {
                //auto alpha   = static_cast<int>(commentAlpha * ImGui::GetStyle().Alpha * 255);
                auto bgAlpha = static_cast<int>(ImGui::GetStyle().Alpha * 255);

                //ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentAlpha * ImGui::GetStyle().Alpha);

                auto min = ed::GetGroupMin();
                //auto max = ed::GetGroupMax();

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
                    IM_COL32(255, 255, 255, 64 * bgAlpha / 255), 4.0f);

                drawList->AddRect(
                    hintFrameBounds.GetTL(),
                    hintFrameBounds.GetBR(),
                    IM_COL32(255, 255, 255, 128 * bgAlpha / 255), 4.0f);

                //ImGui::PopStyleVar();
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

                    if (startPin->Kind == PinKind::Input)
                    {
                        std::swap(startPin, endPin);
                        std::swap(startPinId, endPinId);
                    }

                    if (startPin && endPin)
                    {
                        if (endPin == startPin)
                        {
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else if (endPin->Kind == startPin->Kind)
                        {
                            showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else if (endPin->Node == startPin->Node)
                        {
                            showLabel("x Cannot connect to self", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 0, 0), 1.0f);
                        }
                        else if (endPin->Type != startPin->Type)
                        {
                            showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                        }
                        else
                        {
                            showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                            if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                            {
                                Link link(GetNextId(), startPinId, endPinId);
                                link.Color = GetIconColor(startPin->Type);
                                m_pGraph->AddLink(link);
                            }
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
                        m_pGraph->DeleteNode(nodeId);
                    }
                }

                ed::LinkId linkId = 0;
                while (ed::QueryDeletedLink(&linkId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        m_pGraph->DeleteLink(linkId);
                    }
                }
            }
            ed::EndDelete();
        }

        ImGui::SetCursorScreenPos(cursorTopLeft);
    }

    DrawContextMenu();
    ed::End();
}

void GraphView::DrawContextMenu()
{
    auto openPopupPosition = ImGui::GetMousePos();
    ed::Suspend();
    if (ed::ShowNodeContextMenu(&contextNodeId))
        ImGui::OpenPopup("Node Context Menu");
    else if (ed::ShowPinContextMenu(&contextPinId))
        ImGui::OpenPopup("Pin Context Menu");
    else if (ed::ShowLinkContextMenu(&contextLinkId))
        ImGui::OpenPopup("Link Context Menu");
    else if (ed::ShowBackgroundContextMenu())
    {
        ImGui::OpenPopup("Create New Node");
        newNodeLinkPin = nullptr;
    }
    ed::Resume();

    ed::Suspend();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    if (ImGui::BeginPopup("Node Context Menu"))
    {
        NodePtr node = m_pGraph->FindNode(contextNodeId);

        ImGui::TextUnformatted("Node Context Menu");
        ImGui::Separator();
        if (node)
        {
            ImGui::Text("ID: %p", node->ID.AsPointer());
            ImGui::Text("Type: %s", node->Type == NodeType::Blueprint ? "Blueprint" : "Comment");
            ImGui::Text("Inputs: %d", (int)node->Inputs.size());
            ImGui::Text("Outputs: %d", (int)node->Outputs.size());
        }
        else
            ImGui::Text("Unknown node: %p", contextNodeId.AsPointer());
        ImGui::Separator();
        if (ImGui::MenuItem("Delete"))
            ed::DeleteNode(contextNodeId);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Pin Context Menu"))
    {
        Pin* pin = m_pGraph->FindPin(contextPinId);

        ImGui::TextUnformatted("Pin Context Menu");
        ImGui::Separator();
        if (pin)
        {
            ImGui::Text("ID: %p", pin->ID.AsPointer());
            if (pin->Node)
                ImGui::Text("Node: %p", pin->Node->ID.AsPointer());
            else
                ImGui::Text("Node: %s", "<none>");
        }
        else
            ImGui::Text("Unknown pin: %p", contextPinId.AsPointer());

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Link Context Menu"))
    {
        Link* link = m_pGraph->FindLink(contextLinkId);

        ImGui::TextUnformatted("Link Context Menu");
        ImGui::Separator();
        if (link)
        {
            ImGui::Text("ID: %p", link->ID.AsPointer());
            ImGui::Text("From: %p", link->StartPinID.AsPointer());
            ImGui::Text("To: %p", link->EndPinID.AsPointer());
        }
        else
            ImGui::Text("Unknown link: %p", contextLinkId.AsPointer());
        ImGui::Separator();
        if (ImGui::MenuItem("Delete"))
            ed::DeleteLink(contextLinkId);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Create New Node"))
    {
        auto newNodePostion = openPopupPosition;
        //ImGui::SetCursorScreenPos(ImGui::GetMousePosOnOpeningCurrentPopup());

        //auto drawList = ImGui::GetWindowDrawList();
        //drawList->AddCircleFilled(ImGui::GetMousePosOnOpeningCurrentPopup(), 10.0f, 0xFFFF00FF);

        NodePtr node = nullptr;
        if (ImGui::MenuItem("Branch"))
            node = SpawnNode(BuildBranchNode(*m_pIDGenerator));
        if (ImGui::MenuItem("Print"))
            node = SpawnNode(BuildPrintNode(*m_pIDGenerator));
        if (ImGui::MenuItem("PrintNumber"))
            node = SpawnNode(BuildPrintNumberNode(*m_pIDGenerator));
        if (ImGui::MenuItem("GetBoolVar"))
            node = SpawnNode(GetBoolVariable(*m_pIDGenerator));
        if (ImGui::MenuItem("CreateString"))
            node = SpawnNode(CreateString(*m_pIDGenerator));
        if (ImGui::MenuItem("Add"))
            node = SpawnNode(AddNumbers(*m_pIDGenerator));
        if (ImGui::MenuItem("ReadFile"))
            node = SpawnNode(CreateReadFileNode(*m_pIDGenerator));

        for (auto& def : m_pNodeRegistry->definitions)
        {
            if (ImGui::MenuItem(def.name.c_str()))
            {
                node = SpawnNode(def.MakeNode(*m_pIDGenerator));
            }
        }

        if (node)
        {
            BuildNode(node);

            createNewNode = false;

            ed::SetNodePosition(node->ID, newNodePostion);

            if (auto startPin = newNodeLinkPin)
            {
                auto& pins = startPin->Kind == PinKind::Input ? node->Outputs : node->Inputs;

                for (auto& pin : pins)
                {
                    if (m_pGraph->CanCreateLink(startPin, &pin))
                    {
                        auto endPin = &pin;
                        if (startPin->Kind == PinKind::Input)
                            std::swap(startPin, endPin);

                        Link link(GetNextId(), startPin->ID, endPin->ID);
                        link.Color = GetIconColor(startPin->Type);
                        m_pGraph->AddLink(link);
                        break;
                    }
                }
            }
        }

        ImGui::EndPopup();
    }
    else
        createNewNode = false;
    ImGui::PopStyleVar();
    ed::Resume();
}