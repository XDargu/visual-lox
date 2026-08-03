# include "treeview.h"

# include <imgui_internal.h>

# include <algorithm>
# include <cctype>

namespace Editor
{
    namespace
    {
        ImVec4 ColorForKind(TreeNodeKind kind)
        {
            switch (kind)
            {
            case TreeNodeKind::Script:        return ImVec4(0.55f, 0.72f, 0.92f, 1.0f);
            case TreeNodeKind::Function:      return ImVec4(0.30f, 0.78f, 0.96f, 1.0f);
            case TreeNodeKind::Variable:      return ImVec4(0.94f, 0.70f, 0.28f, 1.0f);
            case TreeNodeKind::Input:         return ImVec4(0.38f, 0.84f, 0.56f, 1.0f);
            case TreeNodeKind::Output:        return ImVec4(0.96f, 0.52f, 0.38f, 1.0f);
            case TreeNodeKind::Class:         return ImVec4(0.74f, 0.52f, 0.96f, 1.0f);
            case TreeNodeKind::ClassMethod:   return ImVec4(0.48f, 0.66f, 1.00f, 1.0f);
            case TreeNodeKind::Constructor:   return ImVec4(0.90f, 0.56f, 0.96f, 1.0f);
            case TreeNodeKind::ClassProperty: return ImVec4(0.96f, 0.67f, 0.38f, 1.0f);
            default:                          return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            }
        }

        std::string Lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        bool MatchesFilter(const TreeNode& node, const std::string& filter)
        {
            if (filter.empty() || Lowercase(node.label).find(filter) != std::string::npos)
                return true;

            return std::any_of(node.children.begin(), node.children.end(),
                [&](const TreeNode& child) { return MatchesFilter(child, filter); });
        }
    }

    bool RenamableSelectable(TreeNode& node, bool isSelected, int& editingItem)
    {
        bool clicked = false;

        static char buffer[128] = "";
        static int bufferItemId = -1;

        const bool isEditing = editingItem == node.id;

        if (isEditing)
        {
            if (bufferItemId != node.id)
            {
                strncpy(buffer, node.label.c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0';
                bufferItemId = node.id;
                ImGui::SetKeyboardFocusHere();
            }

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            const bool submitted = ImGui::InputText("##RenameInput", buffer, IM_ARRAYSIZE(buffer),
                ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll);
            const bool cancelled =
                ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape), false);
            if (cancelled)
            {
                editingItem = -1;
                buffer[0] = '\0';
                bufferItemId = -1;
            }
            else if (submitted || ImGui::IsItemDeactivatedAfterEdit())
            {
                if (node.onRename && node.label != buffer)
                    node.onRename(std::string(buffer));
                editingItem = -1;
                buffer[0] = '\0';
                bufferItemId = -1;
            }
            else if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
            {
                editingItem = -1;
                buffer[0] = '\0';
                bufferItemId = -1;
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ColorForKind(node.kind));
            if (ImGui::Selectable(node.label.c_str(), isSelected,
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0, ImMax(18.0f, ImGui::GetTextLineHeight() + 2.0f))))
            {
                clicked = true;
            }
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && isSelected && node.onRename)
            {
                editingItem = node.id;
                bufferItemId = -1;
            }
        }

        return clicked;
    }

    void RenderTreeNode(TreeNode& node, int& selectedItem, int& editingItem,
                        const char* filter, int* scrollToItem)
    {
        const std::string normalizedFilter = filter ? Lowercase(filter) : std::string();
        if (!MatchesFilter(node, normalizedFilter))
            return;

        const bool filtering = !normalizedFilter.empty();

        ImGui::PushID(node.id);
        const float rowHeight = ImMax(18.0f, ImGui::GetTextLineHeight() + 2.0f);
        const float toggleWidth = 11.0f;
        if (node.children.empty())
        {
            ImGui::Dummy(ImVec2(toggleWidth, rowHeight));
        }
        else
        {
            const ImVec2 arrowMin = ImGui::GetCursorScreenPos();
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            if (ImGui::InvisibleButton("##toggle", ImVec2(toggleWidth, rowHeight)))
                node.isOpen = !node.isOpen;
            ImGui::PopItemFlag();

            const ImU32 arrowColor = ImGui::GetColorU32(
                ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            const ImVec2 center = arrowMin + ImVec2(toggleWidth * 0.5f, rowHeight * 0.5f);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (node.isOpen)
            {
                drawList->AddTriangleFilled(
                    center + ImVec2(-3.5f, -1.5f),
                    center + ImVec2(3.5f, -1.5f),
                    center + ImVec2(0.0f, 2.5f), arrowColor);
            }
            else
            {
                drawList->AddTriangleFilled(
                    center + ImVec2(-1.5f, -3.5f),
                    center + ImVec2(-1.5f, 3.5f),
                    center + ImVec2(2.5f, 0.0f), arrowColor);
            }
        }
        
        ImGui::SameLine(0.0f, 2.0f);
        if (!node.iconText.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ColorForKind(node.kind));
            ImGui::TextUnformatted(node.iconText.c_str());
            ImGui::PopStyleColor();
        }
        else if (node.icon)
        {
            ImGui::Image(node.icon, ImVec2(16, 16));
        }
        else
        {
            ImGui::Dummy(ImVec2(14, rowHeight));
        }
        ImGui::SameLine(0.0f, 4.0f);
        const bool rowActivated =
            RenamableSelectable(node, selectedItem == node.id, editingItem);
        const bool rowFocused = ImGui::IsItemFocused();
        if (rowActivated)
        {
            selectedItem = node.id;
            if (node.onclick)
                node.onclick();
        }

        if (editingItem != node.id && rowFocused && !node.children.empty() &&
            GImGui->NavMoveRequest)
        {
            if (GImGui->NavMoveDir == ImGuiDir_Right && !node.isOpen)
            {
                node.isOpen = true;
                ImGui::NavMoveRequestCancel();
            }
            else if (GImGui->NavMoveDir == ImGuiDir_Left && node.isOpen)
            {
                node.isOpen = false;
                ImGui::NavMoveRequestCancel();
            }
        }

        const bool rowHovered = ImGui::IsItemHovered();
        if (scrollToItem && *scrollToItem == node.id)
        {
            ImGui::SetScrollHereY(0.5f);
            *scrollToItem = -1;
        }
        if (editingItem != node.id && node.isDraggable &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            const TreeNodeDragPayload payload{ node.kind, node.id, node.dragOwnerId };
            ImGui::SetDragDropPayload(
                ScriptItemDragPayloadType, &payload, sizeof(payload));
            ImGui::TextColored(ColorForKind(node.kind), "%s  %s",
                               node.iconText.c_str(), node.label.c_str());
            ImGui::TextDisabled("Drop on the graph to show related nodes");
            ImGui::EndDragDropSource();
        }
        if (rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            selectedItem = node.id;
            if (node.onclick)
                node.onclick();
            ImGui::OpenPopup("##TreeNodeContextMenu");
        }
        if (node.contextMenu && ImGui::BeginPopup("##TreeNodeContextMenu"))
        {
            node.contextMenu();
            ImGui::EndPopup();
        }
        if (node.afterLabel)
            node.afterLabel();
        if (rowHovered && node.tooltip)
            node.tooltip();

        ImGui::PopID();

        if ((node.isOpen || filtering) && !node.children.empty())
        {
            ImGui::Indent(14.0f);
            for (auto& child : node.children)
            {
                RenderTreeNode(child, selectedItem, editingItem, filter, scrollToItem);
            }
            ImGui::Unindent(14.0f);
        }
    }
}
