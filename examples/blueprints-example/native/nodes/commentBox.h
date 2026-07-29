#pragma once

#include "../../graphs/idgeneration.h"
#include "../../graphs/node.h"

enum class CommentBoxColor
{
    Gray,
    Yellow,
    Blue,
    Green,
    Red,
    Purple,
};

inline constexpr int CommentBoxColorCount = 6;

inline const char* CommentBoxColorName(CommentBoxColor color)
{
    switch (color)
    {
        case CommentBoxColor::Gray:   return "gray";
        case CommentBoxColor::Yellow: return "yellow";
        case CommentBoxColor::Blue:   return "blue";
        case CommentBoxColor::Green:  return "green";
        case CommentBoxColor::Red:    return "red";
        case CommentBoxColor::Purple: return "purple";
    }
    return "gray";
}

inline const char* CommentBoxColorLabel(CommentBoxColor color)
{
    switch (color)
    {
        case CommentBoxColor::Gray:   return "Gray";
        case CommentBoxColor::Yellow: return "Yellow";
        case CommentBoxColor::Blue:   return "Blue";
        case CommentBoxColor::Green:  return "Green";
        case CommentBoxColor::Red:    return "Red";
        case CommentBoxColor::Purple: return "Purple";
    }
    return "Gray";
}

inline CommentBoxColor ParseCommentBoxColor(const std::string& name)
{
    for (int index = 0; index < CommentBoxColorCount; ++index)
    {
        const CommentBoxColor color = static_cast<CommentBoxColor>(index);
        if (name == CommentBoxColorName(color))
            return color;
    }
    return CommentBoxColor::Gray;
}

inline ImColor CommentBoxImColor(CommentBoxColor color)
{
    switch (color)
    {
        case CommentBoxColor::Gray:   return ImColor(145, 145, 145);
        case CommentBoxColor::Yellow: return ImColor(230, 190, 70);
        case CommentBoxColor::Blue:   return ImColor(70, 145, 225);
        case CommentBoxColor::Green:  return ImColor(70, 180, 110);
        case CommentBoxColor::Red:    return ImColor(220, 90, 90);
        case CommentBoxColor::Purple: return ImColor(165, 105, 220);
    }
    return ImColor(145, 145, 145);
}

struct CommentBoxNode : public Node
{
    CommentBoxNode(int id, const char* text, CommentBoxColor color)
        : Node(id, text, CommentBoxImColor(color)), BoxColor(color)
    {
        Type = NodeType::CommentBox;
        Category = NodeCategory::CommentBox;
        SerializationType = "comment_box";
        Description = "A resizable annotation box that groups and documents nodes without affecting execution.";
        Size = ImVec2(300.0f, 200.0f);
    }

    void SetBoxColor(CommentBoxColor color)
    {
        BoxColor = color;
        Color = CommentBoxImColor(color);
    }

    void Compile(CompilerContext&, const Graph&, CompilationStage, int) const override
    {
    }

    CommentBoxColor BoxColor;
};

inline NodePtr BuildCommentBoxNode(IDGenerator& idGenerator, const std::string& text = "Comment Box",
                                   CommentBoxColor color = CommentBoxColor::Gray)
{
    return std::make_shared<CommentBoxNode>(idGenerator.GetNextId(), text.c_str(), color);
}
