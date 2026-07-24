#pragma once

#include "../script/script.h"

#include <imgui.h>

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace Editor
{
    enum class TreeNodeKind
    {
        None,
        Script,
        Function,
        Variable,
        Input,
        Output,
        Class,
        ClassMethod,
        Constructor,
        ClassProperty,
    };

    struct TreeNodeDragPayload
    {
        TreeNodeKind kind = TreeNodeKind::None;
        int id = -1;
        int ownerId = -1;
    };

    inline constexpr const char* ScriptItemDragPayloadType = "VLOX_SCRIPT_ITEM";

    struct TreeNode
    {
        int id = -1;
        int parentId = -1;
        int dragOwnerId = -1;
        std::string label;
        std::vector<TreeNode> children;
        bool isOpen = false;
        bool isDraggable = false;
        std::function<void()> onclick;
        std::function<void(std::string)> onRename;
        std::function<void()> contextMenu;
        std::function<void()> afterLabel;
        ImTextureID icon = nullptr;
        std::string iconText;
        TreeNodeKind kind = TreeNodeKind::None;
        std::shared_ptr<IScriptElement> pElement;

        void AddChild(const TreeNode& node)
        {
            children.push_back(node);
            children.back().parentId = id;
        }
    };

    void RenderTreeNode(TreeNode& node, int& selectedItem, int& editingItem,
                        const char* filter = nullptr);
}
