#pragma once

#include "../script/script.h"

#include <imgui.h>

#include <string>
#include <vector>
#include <functional>

namespace Editor
{
    struct TreeNode
    {
        int id = -1;
        std::string label;
        std::vector<TreeNode> children;
        bool isOpen = false;
        std::function<void()> onclick;
        std::function<void(std::string)> onRename;
        std::function<void()> contextMenu;
        ImTextureID icon = nullptr;
    };

    void RenderTreeNode(TreeNode& node, int& selectedItem, int& editingItem);
}