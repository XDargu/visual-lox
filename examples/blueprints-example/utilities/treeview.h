#pragma once

#include "../script/script.h"

#include <imgui.h>

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace Editor
{
    struct TreeNode
    {
        int id = -1;
        int parentId = -1;
        std::string label;
        std::vector<TreeNode> children;
        bool isOpen = false;
        std::function<void()> onclick;
        std::function<void(std::string)> onRename;
        std::function<void()> contextMenu;
        ImTextureID icon = nullptr;
        std::shared_ptr<IScriptElement> pElement;

        void AddChild(const TreeNode& node)
        {
            children.push_back(node);
            children.back().parentId = id;
        }
    };

    void RenderTreeNode(TreeNode& node, int& selectedItem, int& editingItem);
}