#include "scriptSerializer.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/function.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"

#include <Object.h>
#include <Vm.h>
#include <crude_json.h>

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace
{
using Json = crude_json::value;
using Object = crude_json::object;
using Array = crude_json::array;

class SerializationError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class GarbageCollectionPause
{
public:
    GarbageCollectionPause()
        : vm(VM::getInstance()), wasAllowed(vm.isGarbageCollectionAllowed())
    {
        vm.allowGarbageCollection(false);
    }

    ~GarbageCollectionPause() { vm.allowGarbageCollection(wasAllowed); }

private:
    VM& vm;
    bool wasAllowed;
};

const Json& Field(const Json& value, const char* name, crude_json::type_t type)
{
    if (!value.is_object())
        throw SerializationError("Expected an object while reading '" + std::string(name) + "'.");

    const Object& object = value.get<Object>();
    const auto it = object.find(name);
    if (it == object.end())
        throw SerializationError("Missing required field '" + std::string(name) + "'.");
    if (it->second.type() != type)
        throw SerializationError("Field '" + std::string(name) + "' has the wrong type.");
    return it->second;
}

std::string StringField(const Json& value, const char* name)
{
    return Field(value, name, crude_json::type_t::string).get<crude_json::string>();
}

int IntField(const Json& value, const char* name)
{
    const double number = Field(value, name, crude_json::type_t::number).get<crude_json::number>();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
        throw SerializationError("Field '" + std::string(name) + "' must be an integer.");
    return static_cast<int>(number);
}

class IdSet
{
public:
    void Add(int id, const char* description)
    {
        if (id <= 0)
            throw SerializationError(std::string(description) + " ID must be positive.");
        if (!ids.insert(id).second)
            throw SerializationError("Duplicate ID " + std::to_string(id) + ".");
        if (id > maximum)
            maximum = id;
    }

    int Next() const
    {
        if (maximum == std::numeric_limits<int>::max())
            throw SerializationError("The document has exhausted the ID range.");
        return maximum + 1;
    }

private:
    std::set<int> ids;
    int maximum = 0;
};

const char* PinTypeName(PinType type)
{
    switch (type)
    {
    case PinType::Flow: return "flow";
    case PinType::Bool: return "bool";
    case PinType::Int: return "int";
    case PinType::Float: return "number";
    case PinType::String: return "string";
    case PinType::List: return "list";
    case PinType::Object: return "object";
    case PinType::Function: return "function";
    case PinType::Any: return "any";
    case PinType::Error: return "error";
    }
    throw SerializationError("Unknown pin type.");
}

PinType ParsePinType(const std::string& type)
{
    if (type == "flow") return PinType::Flow;
    if (type == "bool") return PinType::Bool;
    if (type == "int") return PinType::Int;
    if (type == "number") return PinType::Float;
    if (type == "string") return PinType::String;
    if (type == "list") return PinType::List;
    if (type == "object") return PinType::Object;
    if (type == "function") return PinType::Function;
    if (type == "any") return PinType::Any;
    if (type == "error") return PinType::Error;
    throw SerializationError("Unknown pin type '" + type + "'.");
}

Json SerializeValue(const Value& value, int depth = 0)
{
    if (depth > 64)
        throw SerializationError("Value nesting exceeds 64 levels.");

    Json result(Object{});
    if (isNil(value))
    {
        result["type"] = "nil";
    }
    else if (isBoolean(value))
    {
        result["type"] = "bool";
        result["value"] = asBoolean(value);
    }
    else if (isNumber(value))
    {
        if (!std::isfinite(asNumber(value)))
            throw SerializationError("NaN and infinity cannot be saved.");
        result["type"] = "number";
        result["value"] = asNumber(value);
    }
    else if (isString(value))
    {
        result["type"] = "string";
        result["value"] = asString(value)->chars;
    }
    else if (isList(value))
    {
        result["type"] = "list";
        Json items(Array{});
        for (const Value& item : asList(value)->items)
            items.push_back(SerializeValue(item, depth + 1));
        result["items"] = std::move(items);
    }
    else if (isRange(value))
    {
        result["type"] = "range";
        result["min"] = asRange(value)->min;
        result["max"] = asRange(value)->max;
    }
    else if (isFunction(value) || isClosure(value))
    {
        // Function defaults are type placeholders in the visual editor. Runtime
        // closures are rebuilt by compilation and are never persisted.
        result["type"] = "function";
    }
    else
    {
        throw SerializationError("This script contains a runtime object that cannot be persisted.");
    }

    return result;
}

Value DeserializeValue(const Json& json, int depth = 0)
{
    if (depth > 64)
        throw SerializationError("Value nesting exceeds 64 levels.");

    const std::string type = StringField(json, "type");
    if (type == "nil") return Value();
    if (type == "bool") return Value(Field(json, "value", crude_json::type_t::boolean).get<crude_json::boolean>());
    if (type == "number")
    {
        const double value = Field(json, "value", crude_json::type_t::number).get<crude_json::number>();
        if (!std::isfinite(value))
            throw SerializationError("NaN and infinity are not valid values.");
        return Value(value);
    }
    if (type == "string")
    {
        std::string value = StringField(json, "value");
        return Value(takeString(std::move(value)));
    }
    if (type == "list")
    {
        ObjList* list = newList();
        const Array& items = Field(json, "items", crude_json::type_t::array).get<Array>();
        for (const Json& item : items)
            list->append(DeserializeValue(item, depth + 1));
        return Value(list);
    }
    if (type == "range")
    {
        const double min = Field(json, "min", crude_json::type_t::number).get<crude_json::number>();
        const double max = Field(json, "max", crude_json::type_t::number).get<crude_json::number>();
        if (!std::isfinite(min) || !std::isfinite(max))
            throw SerializationError("Range bounds must be finite.");
        return Value(newRange(min, max));
    }
    if (type == "function") return Value(newFunction());
    throw SerializationError("Unknown value type '" + type + "'.");
}

Json SerializeProperty(const ScriptProperty& property)
{
    Json result(Object{});
    result["id"] = static_cast<double>(property.ID.id);
    result["name"] = property.Name;
    result["default"] = SerializeValue(property.defaultValue);
    return result;
}

ScriptPropertyPtr DeserializeProperty(const Json& json, IdSet& ids)
{
    const int id = IntField(json, "id");
    ids.Add(id, "Property");
    ScriptPropertyPtr property = std::make_shared<ScriptProperty>(id, StringField(json, "name").c_str());
    property->defaultValue = DeserializeValue(Field(json, "default", crude_json::type_t::object));
    return property;
}

Json SerializeDefinitionPort(const BasicFunctionDef::Input& port)
{
    Json result(Object{});
    result["id"] = static_cast<double>(port.id);
    result["name"] = port.name;
    result["default"] = SerializeValue(port.value);
    return result;
}

BasicFunctionDef::Input DeserializeDefinitionPort(const Json& json, IdSet& ids)
{
    BasicFunctionDef::Input port;
    port.id = IntField(json, "id");
    ids.Add(port.id, "Function port");
    port.name = StringField(json, "name");
    port.value = DeserializeValue(Field(json, "default", crude_json::type_t::object));
    return port;
}

Json SerializePin(const Pin& pin)
{
    Json result(Object{});
    result["id"] = static_cast<double>(pin.ID.Get());
    result["name"] = pin.Name;
    result["type"] = PinTypeName(pin.Type);
    return result;
}

Pin DeserializePin(const Json& json, IdSet& ids)
{
    const int id = IntField(json, "id");
    ids.Add(id, "Pin");
    const std::string name = StringField(json, "name");
    return Pin(id, name.c_str(), ParsePinType(StringField(json, "type")));
}

Json SerializeNode(const Node& node)
{
    if (node.SerializationType.empty())
        throw SerializationError("Node " + std::to_string(node.ID.Get()) + " has no stable serialization type.");

    Json result(Object{});
    result["id"] = static_cast<double>(node.ID.Get());
    result["kind"] = node.SerializationType;
    result["definition"] = node.DefinitionId;
    result["reference_id"] = static_cast<double>(node.refId.id);
    result["state"] = node.State;

    Json inputs(Array{});
    for (const Pin& pin : node.Inputs)
        inputs.push_back(SerializePin(pin));
    result["inputs"] = std::move(inputs);

    Json outputs(Array{});
    for (const Pin& pin : node.Outputs)
        outputs.push_back(SerializePin(pin));
    result["outputs"] = std::move(outputs);

    if (node.InputValues.size() != node.Inputs.size())
        throw SerializationError("Node " + std::to_string(node.ID.Get()) + " has mismatched inputs and default values.");
    Json values(Array{});
    for (const Value& value : node.InputValues)
        values.push_back(SerializeValue(value));
    result["input_values"] = std::move(values);
    return result;
}

NodePtr CreateNode(const Json& json, const NodeRegistry& registry, const Script& script,
                   const ScriptFunctionPtr& owner, IDGenerator& constructionIds)
{
    const std::string kind = StringField(json, "kind");
    const std::string definition = StringField(json, "definition");
    const ScriptElementID reference(IntField(json, "reference_id"));

    if (kind == "begin") return BuildBeginNode(constructionIds, owner);
    if (kind == "return") return BuildReturnNode(constructionIds, *owner);
    if (kind == "variable.get" || kind == "variable.set")
    {
        ScriptPropertyPtr property = ScriptUtils::FindVariableById(script, reference);
        NodePtr node = kind == "variable.get"
            ? BuildGetVariableNode(constructionIds, property, reference)
            : BuildSetVariableNode(constructionIds, property, reference);
        if (!property)
            node->Refresh(script, constructionIds);
        return node;
    }
    if (kind == "function.call" || kind == "function.get")
    {
        BasicFunctionDefPtr functionDefinition;
        if (reference.IsValid())
        {
            ScriptFunctionPtr function = ScriptUtils::FindFunctionById(script, reference);
            if (function)
                functionDefinition = function->functionDef;
        }
        else
        {
            const NativeFunctionDef* native = registry.FindNative(definition);
            if (!native)
                throw SerializationError("Unknown native function definition '" + definition + "'.");
            functionDefinition = native->functionDef;
        }

        NodePtr node = kind == "function.call"
            ? BuildFunctionNode(constructionIds, functionDefinition, reference)
            : BuildGetFunctionNode(constructionIds, functionDefinition, reference);
        if (!functionDefinition)
            node->Refresh(script, constructionIds);
        return node;
    }
    if (kind == "compiled")
    {
        CompiledNodeDefPtr compiled = registry.FindCompiled(definition);
        if (!compiled)
            throw SerializationError("Unknown compiled node definition '" + definition + "'.");
        return compiled->MakeNode(constructionIds);
    }

    throw SerializationError("Unknown node kind '" + kind + "'.");
}

Json SerializeGraph(const Graph& graph)
{
    Json result(Object{});
    Json nodes(Array{});
    for (const NodePtr& node : graph.GetNodes())
        nodes.push_back(SerializeNode(*node));
    result["nodes"] = std::move(nodes);

    Json links(Array{});
    for (const Link& link : graph.GetLinks())
    {
        Json item(Object{});
        item["id"] = static_cast<double>(link.ID.Get());
        item["start_pin_id"] = static_cast<double>(link.StartPinID.Get());
        item["end_pin_id"] = static_cast<double>(link.EndPinID.Get());
        links.push_back(std::move(item));
    }
    result["links"] = std::move(links);
    return result;
}

void DeserializeGraph(const Json& json, const NodeRegistry& registry, const Script& script,
                      const ScriptFunctionPtr& owner, Graph& graph, IdSet& ids,
                      IDGenerator& constructionIds)
{
    const Array& nodes = Field(json, "nodes", crude_json::type_t::array).get<Array>();
    int beginNodeCount = 0;
    for (const Json& nodeJson : nodes)
    {
        NodePtr node = CreateNode(nodeJson, registry, script, owner, constructionIds);
        const int nodeId = IntField(nodeJson, "id");
        ids.Add(nodeId, "Node");
        node->ID = ed::NodeId(nodeId);
        node->State = StringField(nodeJson, "state");

        const Array& inputs = Field(nodeJson, "inputs", crude_json::type_t::array).get<Array>();
        const bool hasDynamicInputs = HasFlag(node->DefinitionFlags, NodeDefinitionFlags::DynamicInputs);
        const bool isMissingReference = HasFlag(node->InstanceFlags, NodeInstanceFlags::Error) &&
            (node->SerializationType == "variable.get" || node->SerializationType == "variable.set" ||
             node->SerializationType == "function.get" || node->SerializationType == "function.call");
        if (!isMissingReference && ((!hasDynamicInputs && inputs.size() != node->Inputs.size()) ||
            (hasDynamicInputs && (inputs.size() < node->Inputs.size() || inputs.size() > 64)))
           )
            throw SerializationError("Node " + std::to_string(nodeId) + " has an invalid input layout.");
        node->Inputs.clear();
        for (const Json& pin : inputs)
            node->Inputs.push_back(DeserializePin(pin, ids));

        const Array& outputs = Field(nodeJson, "outputs", crude_json::type_t::array).get<Array>();
        if (!isMissingReference && outputs.size() != node->Outputs.size())
            throw SerializationError("Node " + std::to_string(nodeId) + " has an invalid output layout.");
        node->Outputs.clear();
        for (const Json& pin : outputs)
            node->Outputs.push_back(DeserializePin(pin, ids));

        node->InputValues.clear();
        const Array& values = Field(nodeJson, "input_values", crude_json::type_t::array).get<Array>();
        for (const Json& value : values)
            node->InputValues.push_back(DeserializeValue(value));
        if (node->InputValues.size() != node->Inputs.size())
            throw SerializationError("Node " + std::to_string(nodeId) + " has mismatched inputs and values.");

        NodeUtils::BuildNode(node);
        graph.AddNode(node);
        if (node->SerializationType == "begin")
            ++beginNodeCount;
    }

    if (beginNodeCount != 1)
        throw SerializationError("Every function graph must contain exactly one Begin node.");

    const Array& links = Field(json, "links", crude_json::type_t::array).get<Array>();
    std::set<int> connectedDataInputs;
    std::set<int> connectedFlowOutputs;
    for (const Json& linkJson : links)
    {
        const int id = IntField(linkJson, "id");
        ids.Add(id, "Link");
        const int startId = IntField(linkJson, "start_pin_id");
        const int endId = IntField(linkJson, "end_pin_id");
        const Pin* start = graph.FindPin(ed::PinId(startId));
        const Pin* end = graph.FindPin(ed::PinId(endId));
        if (!start || !end)
            throw SerializationError("Link " + std::to_string(id) + " references a missing pin.");
        if (start->Kind != PinKind::Output || end->Kind != PinKind::Input)
            throw SerializationError("Link " + std::to_string(id) + " has reversed pin directions.");
        if (!GraphUtils::AreTypesCompatible(start->Type, end->Type))
            throw SerializationError("Link " + std::to_string(id) + " connects incompatible pin types.");
        if (start->Node == end->Node)
            throw SerializationError("Link " + std::to_string(id) + " connects a node to itself.");
        if (start->Type == PinType::Flow)
        {
            if (!connectedFlowOutputs.insert(startId).second)
                throw SerializationError("Flow output pin " + std::to_string(startId) + " has multiple links.");
        }
        else if (!connectedDataInputs.insert(endId).second)
        {
            throw SerializationError("Data input pin " + std::to_string(endId) + " has multiple links.");
        }
        Link link{ ed::LinkId(id), ed::PinId(startId), ed::PinId(endId) };
        link.Color = GetIconColor(start->Type);
        graph.AddLink(link);
    }
}

Json SerializeFunction(const ScriptFunction& function)
{
    Json result(Object{});
    result["id"] = static_cast<double>(function.ID.id);
    result["name"] = function.functionDef->name;

    Json inputs(Array{});
    for (const BasicFunctionDef::Input& input : function.functionDef->inputs)
        inputs.push_back(SerializeDefinitionPort(input));
    result["inputs"] = std::move(inputs);

    Json outputs(Array{});
    for (const BasicFunctionDef::Input& output : function.functionDef->outputs)
        outputs.push_back(SerializeDefinitionPort(output));
    result["outputs"] = std::move(outputs);

    Json variables(Array{});
    for (const ScriptPropertyPtr& variable : function.variables)
        variables.push_back(SerializeProperty(*variable));
    result["variables"] = std::move(variables);
    result["graph"] = SerializeGraph(function.Graph);
    return result;
}

ScriptFunctionPtr DeserializeFunctionShell(const Json& json, IdSet& ids)
{
    const int id = IntField(json, "id");
    ids.Add(id, "Function");
    ScriptFunctionPtr function = std::make_shared<ScriptFunction>(id, StringField(json, "name").c_str());

    const Array& inputs = Field(json, "inputs", crude_json::type_t::array).get<Array>();
    for (const Json& input : inputs)
        function->functionDef->inputs.push_back(DeserializeDefinitionPort(input, ids));
    const Array& outputs = Field(json, "outputs", crude_json::type_t::array).get<Array>();
    for (const Json& output : outputs)
        function->functionDef->outputs.push_back(DeserializeDefinitionPort(output, ids));
    const Array& variables = Field(json, "variables", crude_json::type_t::array).get<Array>();
    for (const Json& variable : variables)
        function->variables.push_back(DeserializeProperty(variable, ids));
    return function;
}

Json SerializeScript(const Script& script)
{
    if (!script.main)
        throw SerializationError("The script has no main function.");
    if (!script.classes.empty())
        throw SerializationError("Class serialization is not available in format version 1.");

    Json root(Object{});
    root["format"] = "visual-lox";
    root["format_version"] = static_cast<double>(ScriptSerializer::FormatVersion);

    Json scriptJson(Object{});
    scriptJson["id"] = static_cast<double>(script.ID.id);
    scriptJson["main"] = SerializeFunction(*script.main);
    Json functions(Array{});
    for (const ScriptFunctionPtr& function : script.functions)
        functions.push_back(SerializeFunction(*function));
    scriptJson["functions"] = std::move(functions);
    Json variables(Array{});
    for (const ScriptPropertyPtr& variable : script.variables)
        variables.push_back(SerializeProperty(*variable));
    scriptJson["variables"] = std::move(variables);
    scriptJson["classes"] = Json(Array{});
    root["script"] = std::move(scriptJson);
    return root;
}

void DeserializeScript(const Json& root, const NodeRegistry& registry, Script& script, int& nextId)
{
    if (StringField(root, "format") != "visual-lox")
        throw SerializationError("This is not a Visual Lox document.");
    const int version = IntField(root, "format_version");
    if (version != ScriptSerializer::FormatVersion)
        throw SerializationError("Unsupported .vlox format version " + std::to_string(version) + ".");

    const Json& scriptJson = Field(root, "script", crude_json::type_t::object);
    IdSet ids;
    script.ID = IntField(scriptJson, "id");
    ids.Add(script.ID, "Script");

    const Array& classes = Field(scriptJson, "classes", crude_json::type_t::array).get<Array>();
    if (!classes.empty())
        throw SerializationError("Class serialization is not available in format version 1.");

    const Array& variables = Field(scriptJson, "variables", crude_json::type_t::array).get<Array>();
    for (const Json& variable : variables)
        script.variables.push_back(DeserializeProperty(variable, ids));

    const Json& mainJson = Field(scriptJson, "main", crude_json::type_t::object);
    script.main = DeserializeFunctionShell(mainJson, ids);

    const Array& functionJsons = Field(scriptJson, "functions", crude_json::type_t::array).get<Array>();
    for (const Json& functionJson : functionJsons)
        script.functions.push_back(DeserializeFunctionShell(functionJson, ids));

    IDGenerator constructionIds;
    DeserializeGraph(Field(mainJson, "graph", crude_json::type_t::object), registry, script,
                     script.main, script.main->Graph, ids, constructionIds);
    for (size_t i = 0; i < functionJsons.size(); ++i)
    {
        DeserializeGraph(Field(functionJsons[i], "graph", crude_json::type_t::object), registry, script,
                         script.functions[i], script.functions[i]->Graph, ids, constructionIds);
    }

    nextId = ids.Next();
}
}

SerializationResult ScriptSerializer::Save(const Script& script, const std::string& path)
{
    try
    {
        if (path.empty())
            return SerializationResult::Fail("No file path was provided.");
        const Json document = SerializeScript(script);
        if (!document.save(path, 2))
            return SerializationResult::Fail("Could not write '" + path + "'.");
        return SerializationResult::Ok();
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}

SerializationResult ScriptSerializer::Load(const std::string& path, const NodeRegistry& registry,
                                            Script& outputScript, IDGenerator& idGenerator)
{
    try
    {
        if (path.empty())
            return SerializationResult::Fail("No file path was provided.");
        const auto loaded = Json::load(path);
        if (!loaded.second || loaded.first.is_discarded())
            return SerializationResult::Fail("Could not read valid JSON from '" + path + "'.");

        GarbageCollectionPause pause;
        Script staged;
        int nextId = 1;
        DeserializeScript(loaded.first, registry, staged, nextId);
        outputScript = std::move(staged);
        idGenerator.Reset(nextId);
        return SerializationResult::Ok();
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}

SerializationResult ScriptSerializer::SerializeToString(const Script& script, std::string& output)
{
    try
    {
        output = SerializeScript(script).dump(2);
        return SerializationResult::Ok();
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}

SerializationResult ScriptSerializer::DeserializeFromString(const std::string& data,
                                                              const NodeRegistry& registry,
                                                              Script& outputScript,
                                                              IDGenerator& idGenerator)
{
    try
    {
        const Json document = Json::parse(data);
        if (document.is_discarded())
            return SerializationResult::Fail("Could not parse a Visual Lox document from memory.");

        GarbageCollectionPause pause;
        Script staged;
        int nextId = 1;
        DeserializeScript(document, registry, staged, nextId);
        outputScript = std::move(staged);
        idGenerator.Reset(nextId);
        return SerializationResult::Ok();
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}

namespace
{
ScriptFunctionPtr FindAnyFunction(const Script& script, int id)
{
    if (script.main && script.main->ID == id)
        return script.main;
    return ScriptUtils::FindFunctionById(script, id);
}

Value CloneValue(const Value& value)
{
    return DeserializeValue(SerializeValue(value));
}

std::string OffsetNodeState(const std::string& state, double offset)
{
    if (state.empty() || offset == 0.0)
        return state;
    Json parsed = Json::parse(state);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("location"))
        return state;
    Json& location = parsed["location"];
    if (!location.is_array() || location.get<Array>().size() < 2 ||
        !location[0].is_number() || !location[1].is_number())
        return state;
    location[0] = location[0].get<crude_json::number>() + offset;
    location[1] = location[1].get<crude_json::number>() + offset;
    return parsed.dump();
}

NodePtr CloneNode(const NodePtr& sourceNode, const NodeRegistry& registry,
                  const Script& destination, const ScriptFunctionPtr& owner,
                  IDGenerator& ids, const std::map<int, int>& referenceMap,
                  std::map<int, int>& pinMap, double positionOffset)
{
    Json definition = SerializeNode(*sourceNode);
    const auto reference = referenceMap.find(sourceNode->refId.id);
    if (reference != referenceMap.end())
        definition["reference_id"] = static_cast<double>(reference->second);

    IDGenerator constructionIds;
    NodePtr clone = CreateNode(definition, registry, destination, owner, constructionIds);
    clone->ID = ed::NodeId(ids.GetNextId());
    clone->State = OffsetNodeState(sourceNode->State, positionOffset);
    clone->Inputs.clear();
    clone->Outputs.clear();
    clone->InputValues.clear();

    for (size_t i = 0; i < sourceNode->Inputs.size(); ++i)
    {
        const Pin& sourcePin = sourceNode->Inputs[i];
        const int newId = ids.GetNextId();
        pinMap[sourcePin.ID.Get()] = newId;
        clone->Inputs.emplace_back(newId, sourcePin.Name.c_str(), sourcePin.Type);
        clone->InputValues.push_back(CloneValue(sourceNode->InputValues[i]));
    }
    for (const Pin& sourcePin : sourceNode->Outputs)
    {
        const int newId = ids.GetNextId();
        pinMap[sourcePin.ID.Get()] = newId;
        clone->Outputs.emplace_back(newId, sourcePin.Name.c_str(), sourcePin.Type);
    }
    NodeUtils::BuildNode(clone);
    return clone;
}

void CloneLinks(const Graph& source, Graph& destination, IDGenerator& ids,
                const std::map<int, int>& pinMap)
{
    for (const Link& sourceLink : source.GetLinks())
    {
        const auto start = pinMap.find(sourceLink.StartPinID.Get());
        const auto end = pinMap.find(sourceLink.EndPinID.Get());
        if (start == pinMap.end() || end == pinMap.end())
            continue;
        Link link{ ed::LinkId(ids.GetNextId()), ed::PinId(start->second), ed::PinId(end->second) };
        const Pin* startPin = destination.FindPin(link.StartPinID);
        link.Color = startPin ? GetIconColor(startPin->Type) : ImColor(255, 255, 255);
        destination.AddLink(link);
    }
}
}

SerializationResult ScriptSerializer::CloneNodes(const Script& source, int sourceFunctionId,
                                                   const std::vector<int>& nodeIds,
                                                   const NodeRegistry& registry, Script& destination,
                                                   int destinationFunctionId, IDGenerator& ids,
                                                   std::vector<int>& pastedNodeIds)
{
    try
    {
        GarbageCollectionPause pause;
        ScriptFunctionPtr sourceFunction = FindAnyFunction(source, sourceFunctionId);
        ScriptFunctionPtr destinationFunction = FindAnyFunction(destination, destinationFunctionId);
        if (!sourceFunction || !destinationFunction)
            return SerializationResult::Fail("The source or destination function no longer exists.");

        std::set<int> selected(nodeIds.begin(), nodeIds.end());
        std::map<int, int> pinMap;
        std::map<int, int> referenceMap;
        pastedNodeIds.clear();
        for (const NodePtr& node : sourceFunction->Graph.GetNodes())
        {
            if (selected.find(node->ID.Get()) == selected.end() ||
                HasFlag(node->DefinitionFlags, NodeDefinitionFlags::Protected))
                continue;
            NodePtr clone = CloneNode(node, registry, destination, destinationFunction,
                                      ids, referenceMap, pinMap, 30.0);
            pastedNodeIds.push_back(clone->ID.Get());
            destinationFunction->Graph.AddNode(clone);
        }
        if (pastedNodeIds.empty())
            return SerializationResult::Fail("The selection contains no copyable nodes.");
        CloneLinks(sourceFunction->Graph, destinationFunction->Graph, ids, pinMap);
        return SerializationResult::Ok();
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}

SerializationResult ScriptSerializer::CloneFunction(const Script& source, int functionId,
                                                      const NodeRegistry& registry, Script& destination,
                                                      IDGenerator& ids, int& pastedFunctionId)
{
    try
    {
        GarbageCollectionPause pause;
        ScriptFunctionPtr sourceFunction = ScriptUtils::FindFunctionById(source, functionId);
        if (!sourceFunction)
            return SerializationResult::Fail("The copied function no longer exists.");

        std::map<int, int> referenceMap;
        pastedFunctionId = ids.GetNextId();
        referenceMap[sourceFunction->ID.id] = pastedFunctionId;
        ScriptFunctionPtr clone = std::make_shared<ScriptFunction>(pastedFunctionId,
                                                                   sourceFunction->functionDef->name.c_str());
        for (const BasicFunctionDef::Input& input : sourceFunction->functionDef->inputs)
        {
            const int newId = ids.GetNextId();
            referenceMap[input.id] = newId;
            clone->functionDef->inputs.push_back({ input.name, CloneValue(input.value), newId });
        }
        for (const BasicFunctionDef::Input& output : sourceFunction->functionDef->outputs)
        {
            const int newId = ids.GetNextId();
            referenceMap[output.id] = newId;
            clone->functionDef->outputs.push_back({ output.name, CloneValue(output.value), newId });
        }
        for (const ScriptPropertyPtr& variable : sourceFunction->variables)
        {
            const int newId = ids.GetNextId();
            referenceMap[variable->ID.id] = newId;
            ScriptPropertyPtr variableClone = std::make_shared<ScriptProperty>(newId, variable->Name.c_str());
            variableClone->defaultValue = CloneValue(variable->defaultValue);
            clone->variables.push_back(variableClone);
        }

        // Install the shell first so recursive function-reference nodes resolve.
        destination.functions.push_back(clone);
        std::map<int, int> pinMap;
        for (const NodePtr& node : sourceFunction->Graph.GetNodes())
        {
            NodePtr nodeClone = CloneNode(node, registry, destination, clone, ids,
                                          referenceMap, pinMap, 0.0);
            clone->Graph.AddNode(nodeClone);
        }
        CloneLinks(sourceFunction->Graph, clone->Graph, ids, pinMap);
        return SerializationResult::Ok();
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}

SerializationResult ScriptSerializer::CloneVariable(const Script& source, int variableId,
                                                      Script& destination, IDGenerator& ids,
                                                      int& pastedVariableId)
{
    ScriptPropertyPtr variable = ScriptUtils::FindVariableById(source, variableId);
    if (!variable)
        return SerializationResult::Fail("The copied variable no longer exists.");
    try
    {
        GarbageCollectionPause pause;
        pastedVariableId = ids.GetNextId();
        ScriptPropertyPtr clone = std::make_shared<ScriptProperty>(pastedVariableId, variable->Name.c_str());
        clone->defaultValue = CloneValue(variable->defaultValue);
        destination.variables.push_back(clone);
        return SerializationResult::Ok();
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}

SerializationResult ScriptSerializer::CloneFunctionPort(const Script& source, int sourceFunctionId,
                                                          int portId, bool output, Script& destination,
                                                          int destinationFunctionId, IDGenerator& ids,
                                                          int& pastedPortId)
{
    ScriptFunctionPtr sourceFunction = FindAnyFunction(source, sourceFunctionId);
    ScriptFunctionPtr destinationFunction = FindAnyFunction(destination, destinationFunctionId);
    if (!sourceFunction || !destinationFunction)
        return SerializationResult::Fail("The source or destination function no longer exists.");
    const BasicFunctionDef::Input* sourcePort = output
        ? sourceFunction->functionDef->FindOutputByID(portId)
        : sourceFunction->functionDef->FindInputByID(portId);
    if (!sourcePort)
        return SerializationResult::Fail("The copied function port no longer exists.");
    try
    {
        GarbageCollectionPause pause;
        pastedPortId = ids.GetNextId();
        BasicFunctionDef::Input clone{ sourcePort->name, CloneValue(sourcePort->value), pastedPortId };
        if (output) destinationFunction->functionDef->outputs.push_back(std::move(clone));
        else destinationFunction->functionDef->inputs.push_back(std::move(clone));
        ScriptUtils::RefreshFunctionRefs(destination, destinationFunctionId, ids);
        return SerializationResult::Ok();
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}
