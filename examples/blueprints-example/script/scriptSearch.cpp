#include "scriptSearch.h"

#include <Object.h>
#include <Value.h>

#include <algorithm>
#include <cctype>
#include <functional>

namespace
{
std::string Lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool Contains(const std::string& value, const std::string& normalizedQuery)
{
    return Lowercase(value).find(normalizedQuery) != std::string::npos;
}

std::string SearchableValueText(const Value& value, int depth = 0)
{
    if (isNil(value))
        return "nil";
    if (isBoolean(value))
        return asBoolean(value) ? "true" : "false";
    if (isNumber(value))
        return std::to_string(asNumber(value));
    if (!isObject(value) || !asObject(value))
        return "<object>";

    switch (getObjType(value))
    {
    case ObjType::STRING:
        return asString(value)->chars;
    case ObjType::RANGE:
        return std::to_string(asRange(value)->min) + ".." +
               std::to_string(asRange(value)->max);
    case ObjType::FUNCTION:
    case ObjType::CLOSURE:
        // Function values used as editor defaults are deliberately nameless
        // type placeholders. objectAsStr assumes a compiled function name and
        // dereferences null for these values.
        return "<function>";
    case ObjType::NATIVE:
        return "<native function>";
    case ObjType::LIST:
    {
        if (depth >= 64)
            return "<nested list>";
        std::string text;
        for (const Value& item : asList(value)->items)
        {
            if (!text.empty())
                text += ",";
            text += SearchableValueText(item, depth + 1);
        }
        return text;
    }
    case ObjType::CLASS:
        return "<class>";
    case ObjType::INSTANCE:
        return "<instance>";
    case ObjType::BOUND_METHOD:
        return "<method>";
    case ObjType::UPVALUE:
        return "<upvalue>";
    case ObjType::COUNT:
        break;
    }
    return "<object>";
}

std::string FunctionName(const ScriptFunctionPtr& function)
{
    return function && function->functionDef
        ? function->functionDef->name : std::string("Function");
}

std::string FunctionLocation(
    const Script& script, const ScriptFunctionPtr& function)
{
    if (!function)
        return "Script";
    if (const ScriptClassPtr owner =
            ScriptUtils::FindOwningClass(script, function->ID.id))
        return owner->Name + " / " + FunctionName(function);
    return FunctionName(function);
}

void ForEachFunction(
    const Script& script,
    const std::function<void(const ScriptFunctionPtr&)>& callback)
{
    if (script.main)
        callback(script.main);
    for (const ScriptFunctionPtr& function : script.functions)
        callback(function);
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (scriptClass->constructor)
            callback(scriptClass->constructor);
        for (const ScriptFunctionPtr& method : scriptClass->methods)
            callback(method);
    }
}

void AddFunctionMatches(
    const Script& script, const ScriptFunctionPtr& function,
    const std::string& query, std::vector<ScriptSearchResult>& results)
{
    if (!function || !function->functionDef)
        return;

    const BasicFunctionDef& definition = *function->functionDef;
    const std::string location = FunctionLocation(script, function);
    std::string detail;
    if (Contains(definition.name, query))
        detail = "Function name";
    else if (Contains(definition.description, query))
        detail = "Function description";

    if (!detail.empty())
    {
        results.push_back({
            ScriptSearchResultKind::Definition,
            function->ID.id,
            function->ID.id,
            -1,
            definition.name,
            detail,
            location,
        });
    }

    const auto addPorts =
        [&](const std::vector<BasicFunctionDef::Input>& ports, const char* kind)
        {
            for (const BasicFunctionDef::Input& port : ports)
            {
                std::string portDetail;
                if (Contains(port.name, query))
                    portDetail = std::string(kind) + " name";
                else if (Contains(port.description, query))
                    portDetail = std::string(kind) + " description";
                else if (Contains(port.type.ToString(), query))
                    portDetail = std::string(kind) + " type";
                else if (Contains(SearchableValueText(port.value), query))
                    portDetail = std::string(kind) + " default value";

                if (!portDetail.empty())
                {
                    results.push_back({
                        ScriptSearchResultKind::FunctionPort,
                        port.id,
                        function->ID.id,
                        -1,
                        port.name.empty() ? kind : port.name,
                        portDetail,
                        location,
                    });
                }
            }
        };
    addPorts(definition.inputs, "Input");
    addPorts(definition.outputs, "Output");
}

void AddPropertyMatch(
    const ScriptPropertyPtr& property, const std::string& query,
    const std::string& location, std::vector<ScriptSearchResult>& results)
{
    if (!property)
        return;

    std::string detail;
    if (Contains(property->Name, query))
        detail = "Variable name";
    else if (Contains(property->Description, query))
        detail = "Variable description";
    else if (Contains(property->type.ToString(), query))
        detail = "Variable type";
    else if (Contains(SearchableValueText(property->defaultValue), query))
        detail = "Default value";

    if (!detail.empty())
    {
        results.push_back({
            ScriptSearchResultKind::Definition,
            property->ID.id,
            ScriptElementID::Invalid,
            -1,
            property->Name,
            detail,
            location,
        });
    }
}

std::string NodeDisplayName(const Node& node)
{
    if (!node.Name.empty())
        return node.Name;
    for (const Pin& output : node.Outputs)
        if (!output.Name.empty())
            return output.Name;
    return "Node";
}

std::string NodeMatchDetail(const Node& node, const std::string& query)
{
    if (Contains(node.Name, query))
        return "Node name";
    if (Contains(node.Description, query))
        return "Node description";
    if (Contains(node.DefinitionId, query))
        return "Node definition";
    if (Contains(node.SerializationType, query))
        return "Node type";

    for (size_t index = 0; index < node.Inputs.size(); ++index)
    {
        const Pin& pin = node.Inputs[index];
        if (Contains(pin.Name, query))
            return "Input pin: " + pin.Name;
        if (Contains(pin.Description, query))
            return "Input description: " + pin.Name;
        if (Contains(pin.Type.ToString(), query))
            return "Input type: " + pin.Type.ToString();
        if (index < node.InputValues.size())
        {
            const std::string inputValue =
                SearchableValueText(node.InputValues[index]);
            if (Contains(inputValue, query))
                return "Input value: " + inputValue;
        }
    }
    for (const Pin& pin : node.Outputs)
    {
        if (Contains(pin.Name, query))
            return "Output pin: " + pin.Name;
        if (Contains(pin.Description, query))
            return "Output description: " + pin.Name;
        if (Contains(pin.Type.ToString(), query))
            return "Output type: " + pin.Type.ToString();
    }
    return {};
}

void AddGraphNode(
    const Script& script, const ScriptFunctionPtr& function,
    const NodePtr& node, const std::string& detail,
    std::vector<ScriptSearchResult>& results)
{
    results.push_back({
        ScriptSearchResultKind::GraphNode,
        node->refId.id,
        function->ID.id,
        static_cast<int>(node->ID.Get()),
        NodeDisplayName(*node),
        detail,
        FunctionLocation(script, function),
    });
}

bool AddDefinitionById(
    const Script& script, int definitionId,
    std::vector<ScriptSearchResult>& results)
{
    if (definitionId == script.ID.id)
    {
        results.push_back({
            ScriptSearchResultKind::Definition, definitionId,
            ScriptElementID::Invalid, -1, "Script", "Definition", "Script",
        });
        return true;
    }

    bool found = false;
    ForEachFunction(script, [&](const ScriptFunctionPtr& function)
    {
        if (found || !function || !function->functionDef)
            return;
        if (function->ID.id == definitionId)
        {
            results.push_back({
                ScriptSearchResultKind::Definition,
                definitionId,
                function->ID.id,
                -1,
                FunctionName(function),
                "Definition",
                FunctionLocation(script, function),
            });
            found = true;
            return;
        }
        const auto findPort =
            [&](const std::vector<BasicFunctionDef::Input>& ports,
                const char* kind) -> bool
            {
                const auto port = std::find_if(
                    ports.begin(), ports.end(),
                    [&](const BasicFunctionDef::Input& value)
                    {
                        return value.id == definitionId;
                    });
                if (port == ports.end())
                    return false;
                results.push_back({
                    ScriptSearchResultKind::FunctionPort,
                    definitionId,
                    function->ID.id,
                    -1,
                    port->name.empty() ? kind : port->name,
                    "Definition",
                    FunctionLocation(script, function),
                });
                return true;
            };
        found = findPort(function->functionDef->inputs, "Input") ||
                findPort(function->functionDef->outputs, "Output");
    });
    if (found)
        return true;

    for (const ScriptPropertyPtr& property : script.variables)
    {
        if (property && property->ID.id == definitionId)
        {
            results.push_back({
                ScriptSearchResultKind::Definition, definitionId,
                ScriptElementID::Invalid, -1, property->Name, "Definition", "Script",
            });
            return true;
        }
    }
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (!scriptClass)
            continue;
        if (scriptClass->ID.id == definitionId)
        {
            results.push_back({
                ScriptSearchResultKind::Definition, definitionId,
                ScriptElementID::Invalid, -1, scriptClass->Name, "Definition", "Script",
            });
            return true;
        }
        for (const ScriptPropertyPtr& property : scriptClass->properties)
        {
            if (property && property->ID.id == definitionId)
            {
                results.push_back({
                    ScriptSearchResultKind::Definition, definitionId,
                    ScriptElementID::Invalid, -1, property->Name, "Definition",
                    scriptClass->Name,
                });
                return true;
            }
        }
    }
    return false;
}
}

std::vector<ScriptSearchResult> ScriptSearch::Text(
    const Script& script, const std::string& query)
{
    std::vector<ScriptSearchResult> results;
    const std::string normalizedQuery = Lowercase(query);
    if (normalizedQuery.empty())
        return results;

    if (Contains("Script", normalizedQuery))
    {
        results.push_back({
            ScriptSearchResultKind::Definition,
            script.ID.id,
            ScriptElementID::Invalid,
            -1,
            "Script",
            "Script",
            "Script",
        });
    }

    ForEachFunction(script, [&](const ScriptFunctionPtr& function)
    {
        AddFunctionMatches(script, function, normalizedQuery, results);
    });

    for (const ScriptPropertyPtr& property : script.variables)
        AddPropertyMatch(property, normalizedQuery, "Script", results);

    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (!scriptClass)
            continue;
        if (Contains(scriptClass->Name, normalizedQuery))
        {
            results.push_back({
                ScriptSearchResultKind::Definition,
                scriptClass->ID.id,
                ScriptElementID::Invalid,
                -1,
                scriptClass->Name,
                "Class name",
                "Script",
            });
        }
        for (const ScriptPropertyPtr& property : scriptClass->properties)
            AddPropertyMatch(property, normalizedQuery, scriptClass->Name, results);
    }

    ForEachFunction(script, [&](const ScriptFunctionPtr& function)
    {
        if (!function)
            return;
        for (const NodePtr& node : function->Graph.GetNodes())
        {
            if (!node)
                continue;
            const std::string detail = NodeMatchDetail(*node, normalizedQuery);
            if (!detail.empty())
                AddGraphNode(script, function, node, detail, results);
        }
    });
    return results;
}

std::vector<ScriptSearchResult> ScriptSearch::References(
    const Script& script, int referenceId, int definitionId)
{
    std::vector<ScriptSearchResult> results;
    AddDefinitionById(script, definitionId, results);

    ForEachFunction(script, [&](const ScriptFunctionPtr& function)
    {
        if (!function)
            return;
        for (const NodePtr& node : function->Graph.GetNodes())
        {
            if (node && node->refId.id == referenceId)
                AddGraphNode(script, function, node, "Reference", results);
        }
    });
    return results;
}
