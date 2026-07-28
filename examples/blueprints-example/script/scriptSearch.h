#pragma once

#include "script.h"

#include <string>
#include <vector>

enum class ScriptSearchResultKind
{
    Definition,
    FunctionPort,
    GraphNode,
};

struct ScriptSearchResult
{
    ScriptSearchResultKind kind = ScriptSearchResultKind::Definition;
    int elementId = ScriptElementID::Invalid;
    int functionId = ScriptElementID::Invalid;
    int nodeId = -1;
    std::string label;
    std::string detail;
    std::string location;
};

struct ScriptSearch
{
    // Searches script definitions, function ports, graph node names and
    // descriptions, pin names/descriptions/types, and unconnected input values.
    static std::vector<ScriptSearchResult> Text(
        const Script& script, const std::string& query);

    // definitionId is normally the same as referenceId. It may differ for
    // constructors and function ports, whose graph nodes reference their owner.
    static std::vector<ScriptSearchResult> References(
        const Script& script, int referenceId, int definitionId);

    // Native and compiled nodes do not reference a user-authored script
    // element. Their persisted node type and definition identify every
    // instance of the same registered node definition.
    static std::vector<ScriptSearchResult> References(
        const Script& script, const Node& node);
};
