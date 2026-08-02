#pragma once

#include "../graphs/uuid.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace SerializationModel
{
using ExtensionFields = std::map<std::string, std::string>;

enum class PortIdentityKind
{
    Fixed,
    Script,
    Dynamic,
};

struct PortIdentity
{
    PortIdentityKind kind = PortIdentityKind::Fixed;
    std::string key;
    ScriptPortId scriptPortId;
    std::string family;
    DynamicSlotId slotId;
    std::string member;

    bool operator==(const PortIdentity& other) const
    {
        return kind == other.kind && key == other.key && scriptPortId == other.scriptPortId && family == other.family && slotId == other.slotId && member == other.member;
    }
};

struct Port
{
    PortIdentity identity;
    std::string displayName;
    std::optional<std::string> encodedValue;
    std::optional<std::string> lastKnownTypeHint;
    bool resolved = true;
    ExtensionFields extensions;
};

struct DefinitionReference
{
    std::string id;
    uint32_t revision = 1;
};

struct SymbolReference
{
    ModuleId moduleId;
    ScriptElementUuid symbolId;
    std::string displayName;
};

struct Node
{
    GraphNodeId id;
    DefinitionReference definition;
    std::string displayName;
    std::optional<SymbolReference> target;
    std::string editorState;
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    ExtensionFields extensions;
};

struct LinkEndpoint
{
    GraphNodeId nodeId;
    PortIdentity port;
    std::string lastKnownDisplayName;
    std::optional<std::string> lastKnownTypeHint;
};

struct Link
{
    PersistentLinkId id;
    LinkEndpoint from;
    LinkEndpoint to;
    bool resolved = true;
    ExtensionFields extensions;
};

struct Graph
{
    std::vector<Node> nodes;
    std::vector<Link> links;
    ExtensionFields extensions;
};

struct DefinitionPort
{
    ScriptPortId id;
    std::string displayName;
    std::string encodedDeclaredType;
    std::optional<std::string> encodedDefault;
    ExtensionFields extensions;
};

struct Function
{
    ScriptElementUuid id;
    std::string displayName;
    std::vector<DefinitionPort> inputs;
    std::vector<DefinitionPort> outputs;
    Graph graph;
    ExtensionFields extensions;
};

struct Document
{
    int sourceFormatVersion = 0;
    int formatVersion = 0;
    ModuleId moduleId;
    Function main;
    std::vector<Function> functions;
    ExtensionFields documentExtensions;
    ExtensionFields scriptExtensions;
};
}
