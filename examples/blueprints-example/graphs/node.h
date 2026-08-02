#pragma once

#include "../script/scriptElement.h"
#include "typeSystem.h"
#include "uuid.h"

#include "../utilities/drawing.h"

#include <imgui_node_editor.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <utility>

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
    Protected = 1 << 3,
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
constexpr inline NodeDefinitionFlags ClearFlag(NodeDefinitionFlags a, NodeDefinitionFlags b) { return a & ~b; }

constexpr inline NodeInstanceFlags operator~ (NodeInstanceFlags a) { return (NodeInstanceFlags)~(int)a; }
constexpr inline NodeInstanceFlags operator| (NodeInstanceFlags a, NodeInstanceFlags b) { return (NodeInstanceFlags)((int)a | (int)b); }
constexpr inline NodeInstanceFlags operator& (NodeInstanceFlags a, NodeInstanceFlags b) { return (NodeInstanceFlags)((int)a & (int)b); }
constexpr inline NodeInstanceFlags& operator|= (NodeInstanceFlags& a, NodeInstanceFlags b) { return (NodeInstanceFlags&)((int&)a |= (int)b); }
constexpr inline bool HasFlag(NodeInstanceFlags a, NodeInstanceFlags b) { return (int)(a & b) != 0; }
constexpr inline NodeInstanceFlags ClearFlag(NodeInstanceFlags a, NodeInstanceFlags b) { return a & ~b; }

inline ImColor GetIconColor(PinType type)
{
    switch (type)
    {
        default:
        case PinType::Flow:     return ImColor(255, 255, 255);
        case PinType::Nil:      return ImColor(145, 145, 145);
        case PinType::Bool:     return ImColor(220, 48, 48);
        case PinType::Int:      return ImColor(68, 201, 156);
        case PinType::Float:    return ImColor(147, 226, 74);
        case PinType::String:   return ImColor(124, 21, 153);
        case PinType::List:     return ImColor(51, 150, 215);
        case PinType::Map:      return ImColor(45, 170, 190);
        case PinType::Range:    return ImColor(230, 153, 45);
        case PinType::Object:   return ImColor(51, 150, 215);
        case PinType::Function: return ImColor(218, 0, 183);
        case PinType::Tuple:    return ImColor(90, 175, 205);
        case PinType::Iterable: return ImColor(51, 150, 215);
        case PinType::TypeVariable: return ImColor(200, 200, 200);
        case PinType::Any:      return ImColor(200, 200, 200);
        case PinType::Error:    return ImColor(0, 0, 0);
    }
};

inline ax::Drawing::IconType GetPinIcon(PinType type)
{
    switch (type)
    {
        case PinType::Flow:     return ax::Drawing::IconType::Flow;
        case PinType::Nil:      return ax::Drawing::IconType::Circle;
        case PinType::Bool:     return ax::Drawing::IconType::Circle;
        case PinType::Int:      return ax::Drawing::IconType::Circle;
        case PinType::Float:    return ax::Drawing::IconType::Circle;
        case PinType::String:   return ax::Drawing::IconType::Circle;
        case PinType::List:     return ax::Drawing::IconType::Square;
        case PinType::Map:      return ax::Drawing::IconType::Square;
        case PinType::Range:    return ax::Drawing::IconType::Square;
        case PinType::Object:   return ax::Drawing::IconType::Circle;
        case PinType::Function: return ax::Drawing::IconType::RoundSquare;
        case PinType::Tuple:    return ax::Drawing::IconType::Square;
        case PinType::Iterable: return ax::Drawing::IconType::Square;
        case PinType::TypeVariable: return ax::Drawing::IconType::Circle;
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
    CommentBox,
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct GenericTypeProperty
{
    std::string variableName;
    std::string label = "Type";
    std::string key;

    bool operator==(const GenericTypeProperty& other) const
    {
        return key == other.key && variableName == other.variableName && label == other.label;
    }
};

struct DynamicInputProps
{
    int minInputs = 1;
    int maxInputs = 16;
    TypeRef type = PinType::Any;
    Value defaultValue;
    std::string description;
    std::string familyKey = "item";
    std::string memberKey = "value";
    std::string orderingMemberKey = "value";
};

enum class PortIdentityKind
{
    None,
    Fixed,
    Script,
    Dynamic,
};

struct PortIdentity
{
    PortIdentityKind kind = PortIdentityKind::None;
    std::string key;
    ScriptPortId scriptPortId;
    DynamicSlotId dynamicSlot;
    std::string family;
    std::string member;

    static PortIdentity Fixed(std::string key)
    {
        PortIdentity result;
        result.kind = PortIdentityKind::Fixed;
        result.key = std::move(key);
        return result;
    }

    static PortIdentity Script(ScriptPortId persistentPortId)
    {
        PortIdentity result;
        result.kind = PortIdentityKind::Script;
        result.scriptPortId = persistentPortId;
        return result;
    }

    static PortIdentity Dynamic(std::string familyKey, DynamicSlotId slotId, std::string memberKey)
    {
        PortIdentity result;
        result.kind = PortIdentityKind::Dynamic;
        result.family = std::move(familyKey);
        result.dynamicSlot = slotId;
        result.member = std::move(memberKey);
        return result;
    }

    bool operator==(const PortIdentity& other) const
    {
        return kind == other.kind && key == other.key && scriptPortId == other.scriptPortId && dynamicSlot == other.dynamicSlot &&
            family == other.family && member == other.member;
    }
};

inline bool PortIdentitiesMatch(const PortIdentity& left, const PortIdentity& right)
{
    if (left.kind != right.kind) return false;
    if (left.kind == PortIdentityKind::Script)
    {
        return left.scriptPortId == right.scriptPortId;
    }
    if (left.kind == PortIdentityKind::Fixed) return left.key == right.key;
    if (left.kind == PortIdentityKind::Dynamic)
        return left.family == right.family && left.dynamicSlot == right.dynamicSlot && left.member == right.member;
    return true;
}

struct Pin
{
    ed::PinId   ID;
    ::NodePtr   Node;
    std::string Name;
    std::string Description;
    TypeRef     Type;
    // Generic node definitions keep their unresolved pattern here. For normal
    // pins this is identical to Type.
    TypeRef     DeclaredType;
    PinKind     Kind;
    PortIdentity Identity;

    Pin(int id, const char* name, TypeRef type, std::string description = {}) :
        ID(id), Node(nullptr), Name(name), Description(std::move(description)),
        Type(type), DeclaredType(std::move(type)),
        Kind(PinKind::Input)
    {
        if (!Description.empty() && Description.back() == '.')
            Description.pop_back();
    }
};

struct InputPin : Pin
{
    using Pin::Pin;

    InputPin(Pin pin, Value literalValue = {}) : Pin(std::move(pin)), LiteralValue(literalValue) {}

    Value LiteralValue;
};

enum class NodeCategory
{
    Begin,
    Return,
    Function,
    Flow,
    Variable,
    CommentBox
};

enum class CompilationStage
{
    BeginSequence,
    EndSequence,
    BeforeInput,
    BeginInputs,
    EndInputs,
    PullOutput,
    BeforeOutput,
    BeginOutput,
    EndOutput,
    BeginNode,
    BeforeDeferredInput,
    AfterDeferredInput,

    // Special cases
    ConstFoldedInputs,
};

class Compiler;
struct Graph;
struct IDGenerator;

struct Node
{
    ed::NodeId       ID;
    GraphNodeId      PersistentId{ Uuid::NewV4() };
    std::string      Name;
    std::string      Description;
    std::vector<InputPin> Inputs;
    std::vector<Pin> Outputs;
    std::vector<InputPin> UnresolvedInputs;
    std::vector<Pin> UnresolvedOutputs;
    ImColor          Color;
    NodeType         Type = NodeType::Blueprint;
    NodeCategory     Category = NodeCategory::Begin;
    ImVec2           Size;
    bool             ShowInputPinNames = true;
    bool             ShowOutputPinNames = true;
    NodeDefinitionFlags DefinitionFlags = NodeDefinitionFlags::None;
    NodeInstanceFlags   InstanceFlags = NodeInstanceFlags::None;
    bool IsSerializationPlaceholder = false;

    std::string State;
    std::string SavedState;

    std::string Error;

    // Stable persistence metadata. Display names are intentionally not used as
    // identifiers: compiled nodes such as Math::Add are displayed as "+".
    std::string SerializationType;
    std::string DefinitionId;
    uint32_t DefinitionRevision = 1;

    // Explicit bindings for generic type variables on this node instance.
    // Missing entries remain inferred from connected pins.
    std::map<std::string, TypeRef> TypeOverrides;

    // Definition metadata controls which generic bindings appear in the
    // Inspector. ResolvedTypeVariables is transient graph-inference state.
    std::vector<GenericTypeProperty> GenericTypeProperties;
    std::map<std::string, TypeRef> ResolvedTypeVariables;

    // Reference to: functionId, variableId
    ScriptElementID refId;
    ModuleId refModuleId;
    ScriptElementUuid refPersistentId;

    Node(int id, const char* name, ImColor color = ImColor(255, 255, 255), std::string description = {}) :
        ID(id), Name(name), Description(std::move(description)), Color(color), Size(0, 0)
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
    virtual TypeRef DynamicInputType() const { return TypeRef(PinType::Any); }
    virtual void ConfigureDynamicInputs(const DynamicInputProps& properties) {}
    virtual bool IsValidDynamicInputCount(size_t inputCount) const
    {
        return inputCount >= Inputs.size() && inputCount <= 64;
    }
    virtual bool IsInputDeferred(int inputIndex) const { return false; }
    virtual bool ShouldCompileDeferredInput(int inputIndex, int outputIndex) const
    {
        return false;
    }
    virtual int GetReceiverInputIndex() const { return -1; }

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
