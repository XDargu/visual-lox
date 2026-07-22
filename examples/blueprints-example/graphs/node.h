#pragma once

#include "../script/scriptElement.h"

#include "../utilities/drawing.h"

#include <imgui_node_editor.h>

#include <string>
#include <vector>
#include <memory>

#include <Value.h>


namespace ed = ax::NodeEditor;
struct CompilerContext;
struct Script;

// Immutable capabilities copied from the registered node definition.
enum class NodeDefinitionFlags
{
    None = 0,
    ReadOnly = 1 << 0,
    DynamicInputs = 1 << 1,
    Pure = 1 << 2,
};

// Mutable state belonging to one node instance in one graph.
enum class NodeInstanceFlags
{
    None = 0,
    Error = 1 << 0,
};

constexpr inline NodeDefinitionFlags operator~ (NodeDefinitionFlags a) { return (NodeDefinitionFlags)~(int)a; }
constexpr inline NodeDefinitionFlags operator| (NodeDefinitionFlags a, NodeDefinitionFlags b) { return (NodeDefinitionFlags)((int)a | (int)b); }
constexpr inline NodeDefinitionFlags operator& (NodeDefinitionFlags a, NodeDefinitionFlags b) { return (NodeDefinitionFlags)((int)a & (int)b); }
constexpr inline NodeDefinitionFlags& operator|= (NodeDefinitionFlags& a, NodeDefinitionFlags b) { return (NodeDefinitionFlags&)((int&)a |= (int)b); }
constexpr inline bool HasFlag(NodeDefinitionFlags a, NodeDefinitionFlags b) { return (int)(a & b) != 0; }

constexpr inline NodeInstanceFlags operator~ (NodeInstanceFlags a) { return (NodeInstanceFlags)~(int)a; }
constexpr inline NodeInstanceFlags operator| (NodeInstanceFlags a, NodeInstanceFlags b) { return (NodeInstanceFlags)((int)a | (int)b); }
constexpr inline NodeInstanceFlags operator& (NodeInstanceFlags a, NodeInstanceFlags b) { return (NodeInstanceFlags)((int)a & (int)b); }
constexpr inline NodeInstanceFlags& operator|= (NodeInstanceFlags& a, NodeInstanceFlags b) { return (NodeInstanceFlags&)((int&)a |= (int)b); }
constexpr inline bool HasFlag(NodeInstanceFlags a, NodeInstanceFlags b) { return (int)(a & b) != 0; }
constexpr inline NodeInstanceFlags ClearFlag(NodeInstanceFlags a, NodeInstanceFlags b) { return a & ~b; }

enum class PinType
{
    Flow,
    Bool,
    Int,
    Float,
    String,
    List,
    Object,
    Function,
    Any,
    Error
};

inline ImColor GetIconColor(PinType type)
{
    switch (type)
    {
        default:
        case PinType::Flow:     return ImColor(255, 255, 255);
        case PinType::Bool:     return ImColor(220, 48, 48);
        case PinType::Int:      return ImColor(68, 201, 156);
        case PinType::Float:    return ImColor(147, 226, 74);
        case PinType::String:   return ImColor(124, 21, 153);
        case PinType::List:     return ImColor(51, 150, 215);
        case PinType::Object:   return ImColor(51, 150, 215);
        case PinType::Function: return ImColor(218, 0, 183);
        case PinType::Any:      return ImColor(200, 200, 200);
        case PinType::Error:    return ImColor(0, 0, 0);
    }
};

inline ax::Drawing::IconType GetPinIcon(PinType type)
{
    switch (type)
    {
        case PinType::Flow:     return ax::Drawing::IconType::Flow;
        case PinType::Bool:     return ax::Drawing::IconType::Circle;
        case PinType::Int:      return ax::Drawing::IconType::Circle;
        case PinType::Float:    return ax::Drawing::IconType::Circle;
        case PinType::String:   return ax::Drawing::IconType::Circle;
        case PinType::List:     return ax::Drawing::IconType::Square;
        case PinType::Object:   return ax::Drawing::IconType::Circle;
        case PinType::Function: return ax::Drawing::IconType::RoundSquare;
        case PinType::Any:      return ax::Drawing::IconType::Circle;
        case PinType::Error:    return ax::Drawing::IconType::Circle;
        default:                return ax::Drawing::IconType::Circle;
    }
}

enum class PinKind
{
    Output,
    Input
};

enum class NodeType
{
    Blueprint,
    SimpleGet,
    SimpleLargeBody,
    Comment,
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Pin
{
    ed::PinId   ID;
    ::NodePtr   Node;
    std::string Name;
    PinType     Type;
    PinKind     Kind;

    Pin(int id, const char* name, PinType type) :
        ID(id), Node(nullptr), Name(name), Type(type), Kind(PinKind::Input)
    {
    }
};

enum class NodeCategory
{
    Begin,
    Return,
    Function,
    Flow,
    Variable
};

enum class CompilationStage
{
    BeginSequence,
    EndSequence,
    BeforeInput,
    BeginInputs,
    EndInputs,
    PullOutput,
    BeginOutput,
    EndOutput,
    BeginNode,

    // Special cases
    ConstFoldedInputs,
};

class Compiler;
struct Graph;
struct IDGenerator;

struct Node
{
    ed::NodeId       ID;
    std::string      Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor          Color;
    NodeType         Type = NodeType::Blueprint;
    NodeCategory     Category = NodeCategory::Begin;
    ImVec2           Size;
    NodeDefinitionFlags DefinitionFlags = NodeDefinitionFlags::None;
    NodeInstanceFlags   InstanceFlags = NodeInstanceFlags::None;

    std::vector<Value> InputValues;

    std::string State;
    std::string SavedState;

    std::string Error;

    // Stable persistence metadata. Display names are intentionally not used as
    // identifiers: compiled nodes such as Math::Add are displayed as "+".
    std::string SerializationType;
    std::string DefinitionId;

    // Reference to: functionId, variableId
    ScriptElementID refId;

    Node(int id, const char* name, ImColor color = ImColor(255, 255, 255)) :
        ID(id), Name(name), Color(color), Size(0, 0)
    {
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const = 0;

    bool IsPure() const { return HasFlag(DefinitionFlags, NodeDefinitionFlags::Pure); }

    virtual void Refresh(const Script& script, IDGenerator& IDGenerator) {}

    // Dynamic node operations
    virtual void AddInput(IDGenerator& IDGenerator) {};
    virtual void RemoveInput(ed::PinId pinId) {};
    virtual bool CanRemoveInput(ed::PinId pinId) const { return false; };
    virtual bool CanAddInput() const { return false; };

    Pin* FindOutputByName(const std::string& name);
    Pin* FindInputByName(const std::string& name);
};

using NodePtr = std::shared_ptr<Node>;

struct NodeIdLess
{
    bool operator()(const ed::NodeId& lhs, const ed::NodeId& rhs) const
    {
        return lhs.AsPointer() < rhs.AsPointer();
    }
};

struct NodeUtils
{
    static void BuildNode(const NodePtr& node);
};
