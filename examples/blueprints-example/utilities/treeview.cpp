# include "treeview.h"

# include <imgui_internal.h>

namespace Editor
{
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
            if (ImGui::InputText("##RenameInput", buffer, IM_ARRAYSIZE(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                // Save the new name when Enter is pressed
                node.onRename(std::string(buffer));
                editingItem = -1;
                buffer[0] = '\0';
            }

            // Exit edit mode if the user clicks elsewhere
            if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
            {
                editingItem = -1;
                buffer[0] = '\0';
            }
        }
        else
        {
            // View mode: Render the selectable
            if (ImGui::Selectable(node.label.c_str(), isSelected))
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

    void RenderTreeNode(TreeNode& node, int& selectedItem, int& editingItem)
    {
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
        ImGui::Image(node.icon, ImVec2(24, 24));
        ImGui::SameLine();
        if (RenamableSelectable(node, selectedItem == node.id, editingItem))
        {
            selectedItem = node.id; // Mark this node as selected
            if (node.onclick)
                node.onclick();
        }
        if (node.contextMenu)
            node.contextMenu();

        ImGui::PopID();

        // Render children if node is expanded
        if (node.isOpen && !node.children.empty())
        {
            ImGui::Indent(); // Indent for child nodes
            for (auto& child : node.children)
            {
                RenderTreeNode(child, selectedItem, editingItem);
            }
            ImGui::Unindent(); // Unindent after finishing children
        }
    }
}
