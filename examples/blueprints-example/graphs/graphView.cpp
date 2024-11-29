#pragma once

#include "graphView.h"

#include "nodeRegistry.h"

#include "../native/nodes/begin.h"
#include "../utilities/utils.h"

#include "../native/nodes/variable.h"
#include "../native/nodes/function.h"

#include "../script/script.h"

#include <Compiler.h>

#include <misc/imgui_stdlib.h>
#include <imgui_node_editor_internal.h>

#include <string_view>
#include <stack>

namespace Utils
{
    bool FilterString(std::string_view target, std::string_view filter)
    {
        if (filter.empty()) return true;

        return target.find(filter) != std::string::npos;
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
    Value& inputValue = node->InputValues[inputIdx];
    
    GraphViewUtils::DrawTypeInput(input.Type, inputValue);
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
    NodeUtils::BuildNode(node);
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

void GraphView::SetGraph(Script* pTargetScript, const ScriptFunctionPtr& pScriptFunction, Graph* pTargetGraph)
{
    m_pGraph = pTargetGraph;
    m_pScript = pTargetScript;
    m_pScriptFunction = pScriptFunction;

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

    // We should add the nodes here in a better wya
    // TODO: Improve this. This calls IMGUI!
    // I might need to manually expose this
    for (auto& node : m_pGraph->GetNodes())
    {
        ed::BeginNode(node->ID);
        ed::EndNode();
    }

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
            if (node->Type != NodeType::Blueprint && node->Type != NodeType::SimpleGet && node->Type != NodeType::SimpleLargeBody)
                continue;

            const bool isSimpleGet = node->Type == NodeType::SimpleGet;
            const bool isSimpleLarge = node->Type == NodeType::SimpleLargeBody;

            const bool isDisconnected = std::find(processedNodes.begin(), processedNodes.end(), node) == processedNodes.end();

            const float alpha = ImGui::GetStyle().Alpha;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha * (isDisconnected ? 0.4f : 1.0f));

            builder.Begin(node->ID);
            if (!(isSimpleGet || isSimpleLarge))
            {
                builder.Header(node->Color);
                ImGui::Spring(0);
                ImGui::TextUnformatted(node->Name.c_str());
                // Test error
                if (HasFlag(node->Flags, NodeFlags::Error))
                {
                    ImGui::Text("Error: %s", node->Error.c_str());
                }
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

                if (!m_pGraph->IsPinLinked(input.ID))
                    DrawPinInput(input, idx);

                ImGui::PopStyleVar();
                builder.EndInput();
                ++idx;
            }

            if (HasFlag(node->Flags, NodeFlags::DynamicInputs) && node->CanAddInput())
            {
                if (ImGui::Button("Add Pin"))
                {
                    node->AddInput(*m_pIDGenerator);
                    NodeUtils::BuildNode(node);
                }
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

            ImGui::PopStyleVar();
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
                        if (m_pGraph->CanCreateLink(startPin, endPin))
                        {
                            showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                            if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                            {
                                Link link(GetNextId(), startPinId, endPinId);
                                link.Color = GetIconColor(startPin->Type);
                                m_pGraph->AddLink(link);
                            }
                        }
                        else if (endPin == startPin)
                        {
                            // No message while hovering over the source pin
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else
                        {
                            const std::string reason =m_pGraph->LinkCreationFailedReason(*startPin, *endPin);
                            showLabel(("x " + reason).c_str(), ImColor(45, 32, 32, 180));
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
    static bool addNodePopupOpened = false;
    static ImVec2 openPopupPosition;

    if (!addNodePopupOpened)
        openPopupPosition = ImGui::GetMousePos();

    ed::Suspend();
    if (ed::ShowNodeContextMenu(&contextNodeId))
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
    else if (ed::ShowBackgroundContextMenu())
    {
        addNodePopupOpened = true;
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

        if (pin->Type == PinType::Any)
        {
            const int inputIdx = GraphUtils::FindNodeInputIdx(*pin);
            Value& inputValue = pin->Node->InputValues[inputIdx];

            GraphViewUtils::DrawTypeSelection(inputValue, [&](PinType newType)
            {
                // TODO: At some point this should become a new editor action!
                switch (newType)
                {
                case PinType::Bool: inputValue = Value(false); break;
                case PinType::Float: inputValue = Value(0.0); break;
                case PinType::String: inputValue = Value(takeString("", 0)); break;
                case PinType::List: inputValue = Value(newList()); break;
                case PinType::Function: inputValue = Value(newFunction()); break;
                case PinType::Any: inputValue = Value(); break;
                }
            });
        }

        if (HasFlag(pin->Node->Flags, NodeFlags::DynamicInputs) && pin->Kind == PinKind::Input)
        {
            if (pin->Node->CanRemoveInput(pin->ID))
            {
                if (ImGui::MenuItem("Remove Pin"))
                {
                    pin->Node->RemoveInput(pin->ID);
                }
            }
        }

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

    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImGui::GetWindowSize() * 0.5f);
    if (ImGui::BeginPopup("Create New Node"))
    {
        addNodePopupOpened = true;
        bool searchChanged = false;

        static std::string searchFilter = "";
        static std::string searchFilterLower = "";
        if (ImGui::InputText("##search", &searchFilter))
        {
            searchChanged = true;
            searchFilterLower = Utils::to_lower(searchFilter);
        }

        struct Data
        {
            std::string name;
            std::string fullName;
            std::function<NodePtr(IDGenerator&)> creationFun;
            std::map<std::string, Data> children;
            int depth;
        };

        Data root;
        root.name = "Nodes";
        root.fullName = "Nodes";
        root.depth = 0;

        for (auto& def : m_pNodeRegistry->nativeDefinitions)
        {
            // Call
            {
                if (Utils::FilterString(Utils::to_lower(def.functionDef->name), searchFilterLower))
                {
                    Data* current = &root;
                    int depth = 1;
                    const std::vector<std::string> tokens = Utils::split(def.functionDef->name, "::");

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
                        }

                        current = &child;
                        depth++;
                    }
                }
            }

            // Get
            {
                const std::string getFuncName = "Get::" + def.functionDef->name;

                if (Utils::FilterString(Utils::to_lower(getFuncName), searchFilterLower))
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
                            child.creationFun = [&](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildGetFunctionNode(IDGenerator, def.functionDef, ScriptElementID::Invalid);
                            };
                        }

                        current = &child;
                        depth++;
                    }
                }
            }
        }

        for (auto& def : m_pNodeRegistry->compiledDefinitions)
        {
            if (!Utils::FilterString(Utils::to_lower(def->name), searchFilterLower))
                continue;

            Data* current = &root;
            int depth = 1;
            const std::vector<std::string> tokens = Utils::split(def->name, "::");

            for (const std::string& token : tokens)
            {
                Data& child = current->children[token];

                child.name = token;
                child.depth = depth;
                child.fullName = token;

                if (token == tokens.back())
                {
                    // Last element!
                    child.creationFun = [&](IDGenerator& IDGenerator) -> NodePtr
                    {
                        return def->nodeCreationFunc(IDGenerator);
                    };
                    child.fullName = def->name;
                }

                current = &child;
                depth++;
            }
        }

        for (auto& def : m_pScript->variables)
        {
            // Get
            {
                const std::string getVar = "Variables::Get::" + def->Name;

                if (Utils::FilterString(Utils::to_lower(getVar), searchFilterLower))
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
                            child.creationFun = [&](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildGetVariableNode(IDGenerator, def);
                            };
                        }

                        current = &child;
                        depth++;
                    }
                }
            }

            // Set
            {
                const std::string setVar = "Variables::Set::" + def->Name;

                if (Utils::FilterString(Utils::to_lower(setVar), searchFilterLower))
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
                            child.creationFun = [&](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildSetVariableNode(IDGenerator, def);
                            };
                        }

                        current = &child;
                        depth++;
                    }
                }
            }
        }

        for (auto& def : m_pScript->functions)
        {
            // Call
            {
                const std::string fullFuncName = "Functions::" + def->functionDef->name;

                if (Utils::FilterString(Utils::to_lower(fullFuncName), searchFilterLower))
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
                            child.creationFun = [&](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return def->functionDef->MakeNode(IDGenerator, def->ID);
                            };
                        }

                        current = &child;
                        depth++;
                    }
                }
            }

            // Get
            {
                const std::string getFuncName = "Functions::Get::" + def->functionDef->name;

                if (Utils::FilterString(Utils::to_lower(getFuncName), searchFilterLower))
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
                            child.creationFun = [&](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildGetFunctionNode(IDGenerator, def->functionDef, def->ID);
                            };
                        }

                        current = &child;
                        depth++;
                    }
                }
            }
        }

        // TODO: Only show return if we can return!
        {
            {
                const std::string fullFuncName = "Flow::Return";

                if (Utils::FilterString(Utils::to_lower(fullFuncName), searchFilterLower))
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
                            child.creationFun = [&](IDGenerator& IDGenerator) -> NodePtr
                            {
                                return BuildReturnNode(IDGenerator, *m_pScriptFunction);
                            };
                        }

                        current = &child;
                        depth++;
                    }
                }
            }
        }

        NodePtr node = nullptr;
        auto newNodePostion = openPopupPosition;

        std::stack<const Data*> stack;
        stack.push(&root);

        int currentDepth = 0;

        while (!stack.empty())
        {
            const Data* top = stack.top();
            stack.pop();

            if (top->depth > currentDepth)
            {
                const int depthDiff = top->depth - currentDepth;
                for (int i = 0; i < depthDiff; ++i)
                    ImGui::Indent();
            }

            if (top->depth < currentDepth)
            {
                const int depthDiff = currentDepth - top->depth;
                for (int i = 0; i < depthDiff; ++i)
                    ImGui::Unindent();
            }

            bool isSelected = false;
            if (ImGui::Selectable((top->fullName + "##" + top->fullName).c_str(), &isSelected))
            {
                if (top->children.empty())
                    node = SpawnNode(top->creationFun(*m_pIDGenerator));
            }

            currentDepth = top->depth;

            for (const auto& [name, child] : top->children)
            {
                stack.push(&child);
            }
        }

        /*static std::string searchFilter = "";
        static int selectedNodeIdx = 0;

        if (addNodePopupOpened)
            searchFilter = "";

        bool searchChanged = false;

        if (ImGui::InputText("##search", &searchFilter))
        {
            searchChanged = true;
        }

        if (addNodePopupOpened)
            ImGui::SetKeyboardFocusHere(0);

        auto newNodePostion = openPopupPosition;
        //ImGui::SetCursorScreenPos(ImGui::GetMousePosOnOpeningCurrentPopup());

        //auto drawList = ImGui::GetWindowDrawList();
        //drawList->AddCircleFilled(ImGui::GetMousePosOnOpeningCurrentPopup(), 10.0f, 0xFFFF00FF);

        const int total = m_pNodeRegistry->compiledDefinitions.size() + m_pNodeRegistry->nativeDefinitions.size();

        if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
        {
            selectedNodeIdx--;
            if (selectedNodeIdx < 0) selectedNodeIdx = 0;
        }
        else if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
        {
            selectedNodeIdx++;
            if (selectedNodeIdx >= total) selectedNodeIdx = total - 1;
        }

        NodePtr node = nullptr;

        int idx = 0;

        const bool isEnterDown = ImGui::IsKeyDown(ImGuiKey_Enter);
        
        for (auto& def : m_pNodeRegistry->compiledDefinitions)
        {
            if (Utils::FilterString(def->name, searchFilter))
            {
                if (searchChanged)
                {
                    selectedNodeIdx = idx;
                    searchChanged = false;
                }

                bool isSelected = selectedNodeIdx == idx;
                if (ImGui::Selectable(def->name.c_str(), &isSelected) || (isSelected && isEnterDown))
                {
                    node = SpawnNode(def->MakeNode(*m_pIDGenerator));
                }
            }

            ++idx;
        }

        for (auto& def : m_pNodeRegistry->nativeDefinitions)
        {
            if (Utils::FilterString(def->name, searchFilter))
            {
                if (ImGui::MenuItem(def->name.c_str()))
                {
                    node = SpawnNode(def->MakeNode(*m_pIDGenerator));
                }
            }
        }*/

        if (node)
        {
            NodeUtils::BuildNode(node);

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
    {
        createNewNode = false;
        addNodePopupOpened = false;
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

    return false;
}

/* static */  bool GraphViewUtils::DrawTypeInput(const PinType pinType, Value& inputValue)
{
    if (pinType == PinType::Bool || pinType == PinType::String || pinType == PinType::Float)
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
    else
        currentIdx = 5;

    ImGui::PushItemWidth(80.0f);
    if (ImGui::Combo("Type", &currentIdx, "Bool\0Number\0String\0List\0Function\0Any\0"))
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
        else
            onChange(PinType::Any);
    }
    ImGui::PopItemWidth();
}