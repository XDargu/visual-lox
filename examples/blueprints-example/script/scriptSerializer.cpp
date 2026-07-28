#include "scriptSerializer.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/function.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"
#include "../native/nodes/object.h"

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

const Json* OptionalField(const Json& value, const char* name);

std::string OptionalStringField(const Json& value, const char* name,
                                std::string fallback = {})
{
    const Json* field = OptionalField(value, name);
    if (!field) return fallback;
    if (!field->is_string())
        throw SerializationError("Field '" + std::string(name) + "' has the wrong type.");
    return field->get<crude_json::string>();
}

int IntField(const Json& value, const char* name)
{
    const double number = Field(value, name, crude_json::type_t::number).get<crude_json::number>();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
        throw SerializationError("Field '" + std::string(name) + "' must be an integer.");
    return static_cast<int>(number);
}

bool BoolField(const Json& value, const char* name)
{
    return Field(value, name, crude_json::type_t::boolean).get<crude_json::boolean>();
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
    case PinType::Nil: return "nil";
    case PinType::Bool: return "bool";
    case PinType::Int: return "int";
    case PinType::Float: return "number";
    case PinType::String: return "string";
    case PinType::List: return "list";
    case PinType::Range: return "range";
    case PinType::Object: return "object";
    case PinType::Function: return "function";
    case PinType::Tuple: return "tuple";
    case PinType::Iterable: return "iterable";
    case PinType::TypeVariable: return "variable";
    case PinType::Any: return "any";
    case PinType::Error: return "error";
    }
    throw SerializationError("Unknown pin type.");
}

PinType ParsePinType(const std::string& type)
{
    if (type == "flow") return PinType::Flow;
    if (type == "nil") return PinType::Nil;
    if (type == "bool") return PinType::Bool;
    if (type == "int") return PinType::Int;
    if (type == "number") return PinType::Float;
    if (type == "string") return PinType::String;
    if (type == "list") return PinType::List;
    if (type == "range") return PinType::Range;
    if (type == "object") return PinType::Object;
    if (type == "function") return PinType::Function;
    if (type == "tuple") return PinType::Tuple;
    if (type == "iterable") return PinType::Iterable;
    if (type == "variable") return PinType::TypeVariable;
    if (type == "any") return PinType::Any;
    if (type == "error") return PinType::Error;
    throw SerializationError("Unknown pin type '" + type + "'.");
}

const Json* OptionalField(const Json& value, const char* name)
{
    if (!value.is_object()) return nullptr;
    const Object& object = value.get<Object>();
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

Json SerializeTypeRef(const TypeRef& type)
{
    Json result(Object{});
    result["kind"] = PinTypeName(type.kind);
    result["class_id"] = static_cast<double>(type.classId);
    result["input_count"] = static_cast<double>(type.functionInputCount);
    result["name"] = type.name;
    Json parameters(Array{});
    for (const TypeRef& parameter : type.parameters)
        parameters.push_back(SerializeTypeRef(parameter));
    result["parameters"] = std::move(parameters);
    return result;
}

TypeRef DeserializeTypeRef(const Json& json, int depth = 0)
{
    if (depth > 32)
        throw SerializationError("Type nesting exceeds 32 levels.");
    const std::string kind = StringField(json, "kind");
    const bool legacyOptional = kind == "optional";
    TypeRef result(legacyOptional ? PinType::Any : ParsePinType(kind));
    result.classId = IntField(json, "class_id");
    result.functionInputCount = IntField(json, "input_count");
    result.name = StringField(json, "name");
    for (const Json& parameter :
         Field(json, "parameters", crude_json::type_t::array).get<Array>())
        result.parameters.push_back(DeserializeTypeRef(parameter, depth + 1));
    if ((result.kind == PinType::List || result.kind == PinType::Iterable) &&
        result.parameters.size() != 1)
        throw SerializationError("Container types require one element type.");
    // Version 3 briefly persisted Optional<T>. Nil is now permitted by every
    // value type, so loading that declaration simply recovers T.
    if (legacyOptional)
        return result.parameters.size() == 1
            ? result.parameters.front() : TypeRef(PinType::Any);
    return result;
}

TypeRef MigratedTypeOfValue(const Value& value)
{
    TypeRef type = TypeOfValue(value);
    if (type == PinType::Nil)
        type = PinType::Any;
    return type;
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
        result["step"] = asRange(value)->step;
        result["include_start"] = asRange(value)->includeStart;
        result["include_end"] = asRange(value)->includeEnd;
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
        const Object& object = json.get<Object>();
        double step = 1.0;
        bool includeStart = true;
        bool includeEnd = true;
        if (const auto it = object.find("step"); it != object.end())
        {
            if (!it->second.is_number())
                throw SerializationError("Range step must be a number.");
            step = it->second.get<crude_json::number>();
        }
        if (const auto it = object.find("include_start"); it != object.end())
        {
            if (!it->second.is_boolean())
                throw SerializationError("Range include_start must be a boolean.");
            includeStart = it->second.get<crude_json::boolean>();
        }
        if (const auto it = object.find("include_end"); it != object.end())
        {
            if (!it->second.is_boolean())
                throw SerializationError("Range include_end must be a boolean.");
            includeEnd = it->second.get<crude_json::boolean>();
        }
        if (!std::isfinite(min) || !std::isfinite(max) ||
            !std::isfinite(step) || step == 0.0)
            throw SerializationError("Range bounds and non-zero step must be finite.");
        return Value(newRange(min, max, step, includeStart, includeEnd));
    }
    if (type == "function") return Value(newFunction());
    throw SerializationError("Unknown value type '" + type + "'.");
}

Json SerializeProperty(const ScriptProperty& property)
{
    Json result(Object{});
    result["id"] = static_cast<double>(property.ID.id);
    result["name"] = property.Name;
    result["description"] = property.Description;
    result["declared_type"] = SerializeTypeRef(property.type);
    result["default"] = SerializeValue(property.defaultValue);
    return result;
}

ScriptPropertyPtr DeserializeProperty(const Json& json, IdSet& ids)
{
    const int id = IntField(json, "id");
    ids.Add(id, "Property");
    ScriptPropertyPtr property = std::make_shared<ScriptProperty>(id, StringField(json, "name").c_str());
    property->Description = OptionalStringField(json, "description");
    property->defaultValue = DeserializeValue(Field(json, "default", crude_json::type_t::object));
    if (const Json* type = OptionalField(json, "declared_type"))
        property->type = DeserializeTypeRef(*type);
    else
        property->type = MigratedTypeOfValue(property->defaultValue);
    return property;
}

Json SerializeDefinitionPort(const BasicFunctionDef::Input& port)
{
    Json result(Object{});
    result["id"] = static_cast<double>(port.id);
    result["name"] = port.name;
    result["description"] = port.description;
    result["declared_type"] = SerializeTypeRef(port.type);
    result["default"] = SerializeValue(port.value);
    return result;
}

BasicFunctionDef::Input DeserializeDefinitionPort(const Json& json, IdSet& ids)
{
    BasicFunctionDef::Input port;
    port.id = IntField(json, "id");
    ids.Add(port.id, "Function port");
    port.name = StringField(json, "name");
    port.description = OptionalStringField(json, "description");
    port.value = DeserializeValue(Field(json, "default", crude_json::type_t::object));
    if (const Json* type = OptionalField(json, "declared_type"))
        port.type = DeserializeTypeRef(*type);
    else
        port.type = MigratedTypeOfValue(port.value);
    return port;
}

Json SerializePin(const Pin& pin)
{
    Json result(Object{});
    result["id"] = static_cast<double>(pin.ID.Get());
    result["name"] = pin.Name;
    result["description"] = pin.Description;
    result["type"] = PinTypeName(pin.DeclaredType.kind);
    result["declared_type"] = SerializeTypeRef(pin.DeclaredType);
    return result;
}

Pin DeserializePin(const Json& json, IdSet& ids)
{
    const int id = IntField(json, "id");
    ids.Add(id, "Pin");
    const std::string name = StringField(json, "name");
    const Json* declaredType = OptionalField(json, "declared_type");
    TypeRef type = declaredType
        ? DeserializeTypeRef(*declaredType)
        : TypeRef(ParsePinType(StringField(json, "type")));
    return Pin(id, name.c_str(), std::move(type),
        OptionalStringField(json, "description"));
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
    result["description"] = node.Description;

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
    if (kind == "class.construct")
    {
        ScriptClassPtr scriptClass = ScriptUtils::FindClassById(script, reference);
        NodePtr node = BuildConstructObjectNode(constructionIds, scriptClass, reference);
        if (!scriptClass) node->Refresh(script, constructionIds);
        return node;
    }
    if (kind == "class.this")
    {
        const ScriptClassPtr ownerClass =
            owner ? ScriptUtils::FindOwningClass(script, owner->ID.id) : nullptr;
        return BuildThisNode(constructionIds, ownerClass
            ? TypeRef::Object(ownerClass->ID.id, ownerClass->Name)
            : TypeRef(PinType::Object));
    }
    if (kind == "property.get" || kind == "property.set")
    {
        ScriptPropertyPtr property = ScriptUtils::FindClassPropertyById(script, reference);
        const ScriptClassPtr ownerClass =
            ScriptUtils::FindOwningClass(script, reference.id);
        const TypeRef instanceType = ownerClass
            ? TypeRef::Object(ownerClass->ID.id, ownerClass->Name)
            : TypeRef(PinType::Object);
        NodePtr node = kind == "property.get"
            ? BuildGetPropertyNode(constructionIds, property, reference, instanceType)
            : BuildSetPropertyNode(constructionIds, property, reference, instanceType);
        if (!property) node->Refresh(script, constructionIds);
        return node;
    }
    if (kind == "method.call" || kind == "method.get")
    {
        ScriptFunctionPtr method = ScriptUtils::FindFunctionById(script, reference);
        const ScriptClassPtr ownerClass =
            ScriptUtils::FindOwningClass(script, reference.id);
        if (!ownerClass) method = nullptr;
        const TypeRef instanceType = ownerClass
            ? TypeRef::Object(ownerClass->ID.id, ownerClass->Name)
            : TypeRef(PinType::Object);
        NodePtr node = kind == "method.call"
            ? BuildMethodCallNode(
                constructionIds, method, reference, instanceType)
            : BuildGetMethodNode(
                constructionIds, method, reference, instanceType);
        if (!method) node->Refresh(script, constructionIds);
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
        if (const Json* description = OptionalField(nodeJson, "description"))
        {
            if (!description->is_string())
                throw SerializationError("Node description has the wrong type.");
            node->Description = description->get<crude_json::string>();
        }

        const Array& inputs = Field(nodeJson, "inputs", crude_json::type_t::array).get<Array>();
        const bool hasDynamicInputs = HasFlag(node->DefinitionFlags, NodeDefinitionFlags::DynamicInputs);
        const bool isMissingReference = HasFlag(node->InstanceFlags, NodeInstanceFlags::Error) &&
            (node->SerializationType == "variable.get" || node->SerializationType == "variable.set" ||
             node->SerializationType == "function.get" || node->SerializationType == "function.call" ||
             node->SerializationType == "class.construct" || node->SerializationType == "property.get" ||
             node->SerializationType == "property.set" || node->SerializationType == "method.call" ||
             node->SerializationType == "method.get");
        if (!isMissingReference && ((!hasDynamicInputs && inputs.size() != node->Inputs.size()) ||
            (hasDynamicInputs && (inputs.size() < node->Inputs.size() || inputs.size() > 64)))
           )
            throw SerializationError("Node " + std::to_string(nodeId) + " has an invalid input layout.");
        const std::vector<Pin> definitionInputs = node->Inputs;
        node->Inputs.clear();
        for (const Json& pin : inputs)
            node->Inputs.push_back(DeserializePin(pin, ids));
        if (definitionInputs.size() == node->Inputs.size())
            for (size_t i = 0; i < node->Inputs.size(); ++i)
            {
                if (!OptionalField(inputs[i], "description"))
                    node->Inputs[i].Description = definitionInputs[i].Description;
                if (!OptionalField(inputs[i], "declared_type") &&
                    definitionInputs[i].DeclaredType.IsGeneric())
                    node->Inputs[i].Type = node->Inputs[i].DeclaredType =
                        definitionInputs[i].DeclaredType;
            }
        if (hasDynamicInputs)
            for (size_t i = 0; i < node->Inputs.size(); ++i)
                if (!OptionalField(inputs[i], "declared_type") &&
                    node->Inputs[i].Type != PinType::Flow)
                    node->Inputs[i].Type = node->Inputs[i].DeclaredType =
                        node->DynamicInputType();

        const Array& outputs = Field(nodeJson, "outputs", crude_json::type_t::array).get<Array>();
        const bool validDynamicOutputs =
            (node->DefinitionId == "Flow::Match" &&
                outputs.size() == inputs.size() - 1) ||
            (node->DefinitionId == "Flow::Switch" &&
                outputs.size() == inputs.size());
        if (!isMissingReference && !validDynamicOutputs && outputs.size() != node->Outputs.size())
            throw SerializationError("Node " + std::to_string(nodeId) + " has an invalid output layout.");
        const std::vector<Pin> definitionOutputs = node->Outputs;
        node->Outputs.clear();
        for (const Json& pin : outputs)
            node->Outputs.push_back(DeserializePin(pin, ids));
        if (definitionOutputs.size() == node->Outputs.size())
            for (size_t i = 0; i < node->Outputs.size(); ++i)
            {
                if (!OptionalField(outputs[i], "description"))
                    node->Outputs[i].Description = definitionOutputs[i].Description;
                if (!OptionalField(outputs[i], "declared_type") &&
                    definitionOutputs[i].DeclaredType.IsGeneric())
                    node->Outputs[i].Type = node->Outputs[i].DeclaredType =
                        definitionOutputs[i].DeclaredType;
            }

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
    graph.RefreshTypes();

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
    result["description"] = function.functionDef->description;
    result["pure"] =
        HasFlag(function.functionDef->flags, NodeDefinitionFlags::Pure);

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
    function->functionDef->description = OptionalStringField(json, "description");
    if (const Json* pure = OptionalField(json, "pure"))
    {
        if (!pure->is_boolean())
            throw SerializationError("Function purity has the wrong type.");
        if (pure->get<crude_json::boolean>())
            function->functionDef->flags |= NodeDefinitionFlags::Pure;
    }

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

Json SerializeClass(const ScriptClass& scriptClass)
{
    Json result(Object{});
    result["id"] = static_cast<double>(scriptClass.ID.id);
    result["name"] = scriptClass.Name;
    Json properties(Array{});
    for (const ScriptPropertyPtr& property : scriptClass.properties)
        properties.push_back(SerializeProperty(*property));
    result["properties"] = std::move(properties);
    Json methods(Array{});
    for (const ScriptFunctionPtr& method : scriptClass.methods)
        methods.push_back(SerializeFunction(*method));
    result["methods"] = std::move(methods);
    result["has_constructor"] = scriptClass.constructor != nullptr;
    result["constructor"] = scriptClass.constructor
        ? SerializeFunction(*scriptClass.constructor) : Json(Object{});
    return result;
}

ScriptClassPtr DeserializeClassShell(const Json& json, IdSet& ids)
{
    const int id = IntField(json, "id");
    ids.Add(id, "Class");
    ScriptClassPtr scriptClass = std::make_shared<ScriptClass>(id, StringField(json, "name").c_str());
    for (const Json& property : Field(json, "properties", crude_json::type_t::array).get<Array>())
        scriptClass->properties.push_back(DeserializeProperty(property, ids));
    for (const Json& method : Field(json, "methods", crude_json::type_t::array).get<Array>())
        scriptClass->methods.push_back(DeserializeFunctionShell(method, ids));
    if (BoolField(json, "has_constructor"))
        scriptClass->constructor = DeserializeFunctionShell(
            Field(json, "constructor", crude_json::type_t::object), ids);
    return scriptClass;
}

Json SerializeScript(const Script& script)
{
    if (!script.main)
        throw SerializationError("The script has no main function.");
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
    Json classes(Array{});
    for (const ScriptClassPtr& scriptClass : script.classes)
        classes.push_back(SerializeClass(*scriptClass));
    scriptJson["classes"] = std::move(classes);
    root["script"] = std::move(scriptJson);
    return root;
}

void DeserializeScript(const Json& root, const NodeRegistry& registry, Script& script, int& nextId)
{
    if (StringField(root, "format") != "visual-lox")
        throw SerializationError("This is not a Visual Lox document.");
    const int version = IntField(root, "format_version");
    if (version < 1 || version > ScriptSerializer::FormatVersion)
        throw SerializationError("Unsupported .vlox format version " + std::to_string(version) + ".");

    const Json& scriptJson = Field(root, "script", crude_json::type_t::object);
    IdSet ids;
    script.ID = IntField(scriptJson, "id");
    ids.Add(script.ID, "Script");

    const Array& classes = Field(scriptJson, "classes", crude_json::type_t::array).get<Array>();
    if (version == 1 && !classes.empty())
        throw SerializationError("Version 1 documents cannot contain classes.");

    const Array& variables = Field(scriptJson, "variables", crude_json::type_t::array).get<Array>();
    for (const Json& variable : variables)
        script.variables.push_back(DeserializeProperty(variable, ids));

    const Json& mainJson = Field(scriptJson, "main", crude_json::type_t::object);
    script.main = DeserializeFunctionShell(mainJson, ids);

    const Array& functionJsons = Field(scriptJson, "functions", crude_json::type_t::array).get<Array>();
    for (const Json& functionJson : functionJsons)
        script.functions.push_back(DeserializeFunctionShell(functionJson, ids));
    if (version >= 2)
        for (const Json& classJson : classes)
            script.classes.push_back(DeserializeClassShell(classJson, ids));

    IDGenerator constructionIds;
    DeserializeGraph(Field(mainJson, "graph", crude_json::type_t::object), registry, script,
                     script.main, script.main->Graph, ids, constructionIds);
    for (size_t i = 0; i < functionJsons.size(); ++i)
    {
        DeserializeGraph(Field(functionJsons[i], "graph", crude_json::type_t::object), registry, script,
                         script.functions[i], script.functions[i]->Graph, ids, constructionIds);
    }
    for (size_t classIndex = 0; classIndex < classes.size(); ++classIndex)
    {
        const Json& classJson = classes[classIndex];
        ScriptClassPtr scriptClass = script.classes[classIndex];
        const Array& methods = Field(classJson, "methods", crude_json::type_t::array).get<Array>();
        for (size_t methodIndex = 0; methodIndex < methods.size(); ++methodIndex)
            DeserializeGraph(Field(methods[methodIndex], "graph", crude_json::type_t::object),
                registry, script, scriptClass->methods[methodIndex],
                scriptClass->methods[methodIndex]->Graph, ids, constructionIds);
        if (scriptClass->constructor)
        {
            const Json& constructor = Field(classJson, "constructor", crude_json::type_t::object);
            DeserializeGraph(Field(constructor, "graph", crude_json::type_t::object), registry,
                script, scriptClass->constructor, scriptClass->constructor->Graph, ids, constructionIds);
        }
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
    clone->Description = sourceNode->Description;
    clone->Inputs.clear();
    clone->Outputs.clear();
    clone->InputValues.clear();

    for (size_t i = 0; i < sourceNode->Inputs.size(); ++i)
    {
        const Pin& sourcePin = sourceNode->Inputs[i];
        const int newId = ids.GetNextId();
        pinMap[sourcePin.ID.Get()] = newId;
        clone->Inputs.emplace_back(newId, sourcePin.Name.c_str(),
            sourcePin.DeclaredType, sourcePin.Description);
        clone->InputValues.push_back(CloneValue(sourceNode->InputValues[i]));
    }
    for (const Pin& sourcePin : sourceNode->Outputs)
    {
        const int newId = ids.GetNextId();
        pinMap[sourcePin.ID.Get()] = newId;
        clone->Outputs.emplace_back(newId, sourcePin.Name.c_str(),
            sourcePin.DeclaredType, sourcePin.Description);
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
        clone->functionDef->description = sourceFunction->functionDef->description;
        clone->functionDef->flags = sourceFunction->functionDef->flags;
        for (const BasicFunctionDef::Input& input : sourceFunction->functionDef->inputs)
        {
            const int newId = ids.GetNextId();
            referenceMap[input.id] = newId;
            clone->functionDef->inputs.push_back(
                { input.name, CloneValue(input.value), newId, input.type,
                  input.description });
        }
        for (const BasicFunctionDef::Input& output : sourceFunction->functionDef->outputs)
        {
            const int newId = ids.GetNextId();
            referenceMap[output.id] = newId;
            clone->functionDef->outputs.push_back(
                { output.name, CloneValue(output.value), newId, output.type,
                  output.description });
        }
        for (const ScriptPropertyPtr& variable : sourceFunction->variables)
        {
            const int newId = ids.GetNextId();
            referenceMap[variable->ID.id] = newId;
            ScriptPropertyPtr variableClone = std::make_shared<ScriptProperty>(newId, variable->Name.c_str());
            variableClone->Description = variable->Description;
            variableClone->type = variable->type;
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
        clone->Description = variable->Description;
        clone->type = variable->type;
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
        BasicFunctionDef::Input clone{
            sourcePort->name, CloneValue(sourcePort->value), pastedPortId,
            sourcePort->type, sourcePort->description };
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
