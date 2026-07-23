# include "treeview.h"

# include <imgui_internal.h>

# include <algorithm>
# include <cctype>

namespace Editor
{
    namespace
    {
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

        static char buffer[128] = "";      // Buffer for renaming (edit mode)

        const bool isEditing = editingItem == node.id;

        if (isEditing)
        {
            if (buffer[0] == '\0') // External edit request
            {
                // Copy the current name to the buffer for editing
                strncpy(buffer, node.label.c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0'; // Ensure null termination
                ImGui::SetKeyboardFocusHere();
            }

            // Edit mode: Render InputText for renaming
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Full width
            const bool submitted = ImGui::InputText("##RenameInput", buffer, IM_ARRAYSIZE(buffer),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
            if (submitted || ImGui::IsItemDeactivatedAfterEdit())
            {
                // Commit one rename when Enter is pressed or focus leaves the
                // field. Keystrokes only update the local edit buffer.
                if (node.onRename && node.label != buffer)
                    node.onRename(std::string(buffer));
                editingItem = -1;
                buffer[0] = '\0';
            }

            // Exit unchanged edits when the user clicks elsewhere.
            else if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
            {
                editingItem = -1;
                buffer[0] = '\0';
            }
        }
        else
        {
            // View mode: Render the selectable
            if (ImGui::Selectable(node.label.c_str(), isSelected,
                                  ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 26)))
            {
                clicked = true;
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && isSelected && node.onRename)
            {
                // Enter edit mode on click
                editingItem = node.id;

                // Copy the current name to the buffer for editing
                strncpy(buffer, node.label.c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0'; // Ensure null termination
            }
        }

        return clicked;
    }

    void RenderTreeNode(TreeNode& node, int& selectedItem, int& editingItem,
                        const char* filter)
    {
        const std::string normalizedFilter = filter ? Lowercase(filter) : std::string();
        if (!MatchesFilter(node, normalizedFilter))
            return;

        const bool filtering = !normalizedFilter.empty();

        // Render expand/collapse button
        ImGui::PushID(node.id); // Ensure unique ID for the arrow button
        if (node.children.empty())
        {
            ImGui::Dummy(ImVec2(16, 0)); // Empty space for alignment
        }
        else if (ImGui::ArrowButton("##toggle", node.isOpen ? ImGuiDir_Down : ImGuiDir_Right))
        {
            node.isOpen = !node.isOpen; // Toggle node open/close
        }
        
        // Render the selectable label
        ImGui::SameLine();
        if (!node.iconText.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.67f, 0.96f, 1.0f));
            ImGui::TextUnformatted(node.iconText.c_str());
            ImGui::PopStyleColor();
        }
        else if (node.icon)
        {
            ImGui::Image(node.icon, ImVec2(20, 20));
        }
        else
        {
            ImGui::Dummy(ImVec2(16, 20));
        }
        ImGui::SameLine();
        if (RenamableSelectable(node, selectedItem == node.id, editingItem))
        {
            selectedItem = node.id; // Mark this node as selected
            if (node.onclick)
                node.onclick();
        }

        const bool rowHovered = ImGui::IsItemHovered();
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

        ImGui::PopID();

        // Render children if node is expanded
        if ((node.isOpen || filtering) && !node.children.empty())
        {
            ImGui::Indent(); // Indent for child nodes
            for (auto& child : node.children)
            {
                RenderTreeNode(child, selectedItem, editingItem, filter);
            }
            ImGui::Unindent(); // Unindent after finishing children
        }
    }
}
