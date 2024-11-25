# include "treeview.h"

# include <imgui_internal.h>

namespace Editor
{
    void RenderTreeNode(TreeNode& node, int& selectedItem)
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
        ImGui::PopID();

        // Render the selectable label
        ImGui::SameLine();
        ImGui::Image(node.icon, ImVec2(24, 24));
        ImGui::SameLine();
        if (ImGui::Selectable(node.label.c_str(), selectedItem == node.id))
        {
            selectedItem = node.id; // Mark this node as selected
            if (node.onclick)
                node.onclick();
        }
        if (node.contextMenu)
            node.contextMenu();

        // Render children if node is expanded
        if (node.isOpen && !node.children.empty())
        {
            ImGui::Indent(); // Indent for child nodes
            for (auto& child : node.children)
            {
                RenderTreeNode(child, selectedItem);
            }
            ImGui::Unindent(); // Unindent after finishing children
        }
    }
}
