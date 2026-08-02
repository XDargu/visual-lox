#include "scriptSerializer.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/commentBox.h"
#include "../native/nodes/function.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"
#include "../native/nodes/object.h"

#include <Object.h>
#include <Vm.h>
#include <crude_json.h>

#include <algorithm>
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

void CaptureSerializedFields(const Json& json, std::map<std::string, std::string>& extensions)
{
    if (!json.is_object()) return;
    extensions.clear();
    for (const auto& [name, value] : json.get<Object>()) extensions[name] = value.dump();
}

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
    case PinType::Map: return "map";
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
    if (type == "map") return PinType::Map;
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

template<typename DurableIdentity>
DurableIdentity OptionalDurableId(const Json& value, const char* name, DurableIdentity fallback)
{
    const Json* field = OptionalField(value, name);
    if (!field) return fallback;
    if (!field->is_string()) throw SerializationError("Field '" + std::string(name) + "' has the wrong type.");
    DurableIdentity result = DurableIdentity::Parse(field->get<crude_json::string>());
    if (!result.IsValid()) throw SerializationError("Field '" + std::string(name) + "' cannot be the nil UUID.");
    return result;
}

template<typename DurableIdentity>
DurableIdentity RequiredDurableId(const Json& value, const char* name)
{
    DurableIdentity result = DurableIdentity::Parse(StringField(value, name));
    if (!result.IsValid()) throw SerializationError("Field '" + std::string(name) + "' cannot be the nil UUID.");
    return result;
}

int DeserializeRuntimeId(const Json& json, const char* description, int formatVersion, IdSet& ids, IDGenerator* runtimeIds)
{
    const int id = formatVersion >= 8 ? runtimeIds->GetNextId() : IntField(json, "id");
    ids.Add(id, description);
    return id;
}

template<typename DurableIdentity>
std::string RuntimeIdKey(const char* scope, DurableIdentity identity)
{
    return std::string(scope) + ":" + identity.ToString();
}

int CachedRuntimeId(Script& script, const std::string& key, const char* description, IdSet& ids, IDGenerator& runtimeIds, int suggestedId = 0)
{
    const auto cached = script.RuntimeIdCache.find(key);
    const int id = cached != script.RuntimeIdCache.end() ? cached->second : suggestedId > 0 ? suggestedId : runtimeIds.GetNextId();
    ids.Add(id, description);
    script.RuntimeIdCache[key] = id;
    return id;
}

template<typename DurableIdentity>
DurableIdentity DeserializePersistentId(const Json& json, int formatVersion, DurableIdentity legacyFallback)
{
    return formatVersion >= 8 ? RequiredDurableId<DurableIdentity>(json, "id") : OptionalDurableId(json, "uuid", legacyFallback);
}

Json SerializeTypeRef(const TypeRef& type, const Script* script = nullptr)
{
    Json result(Object{});
    result["kind"] = PinTypeName(type.kind);
    ModuleId moduleId = type.moduleId;
    ScriptElementUuid symbolId = type.symbolId;
    if (script && type.kind == PinType::Object && type.classId >= 0 && !symbolId.IsValid())
    {
        const ScriptClassPtr scriptClass = ScriptUtils::FindClassById(*script, ScriptElementID(type.classId));
        if (scriptClass)
        {
            moduleId = script->ModuleIdentity;
            symbolId = scriptClass->PersistentId;
        }
    }
    if (!script) result["class_id"] = static_cast<double>(type.classId);
    if (moduleId.IsValid()) result["module"] = moduleId.ToString();
    if (symbolId.IsValid()) result["symbol"] = symbolId.ToString();
    result["input_count"] = static_cast<double>(type.functionInputCount);
    result["name"] = type.name;
    Json parameters(Array{});
    for (const TypeRef& parameter : type.parameters)
        parameters.push_back(SerializeTypeRef(parameter, script));
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
    result.classId = OptionalField(json, "class_id") ? IntField(json, "class_id") : -1;
    if (const Json* module = OptionalField(json, "module"))
    {
        if (!module->is_string()) throw SerializationError("Type module UUID has the wrong type.");
        result.moduleId = ModuleId::Parse(module->get<crude_json::string>());
    }
    if (const Json* symbol = OptionalField(json, "symbol"))
    {
        if (!symbol->is_string()) throw SerializationError("Type symbol UUID has the wrong type.");
        result.symbolId = ScriptElementUuid::Parse(symbol->get<crude_json::string>());
    }
    result.functionInputCount = IntField(json, "input_count");
    result.name = StringField(json, "name");
    for (const Json& parameter :
         Field(json, "parameters", crude_json::type_t::array).get<Array>())
        result.parameters.push_back(DeserializeTypeRef(parameter, depth + 1));
    if ((result.kind == PinType::List || result.kind == PinType::Iterable) &&
        result.parameters.size() != 1)
        throw SerializationError("Container types require one element type.");
    if (result.kind == PinType::Map && result.parameters.size() != 2)
        throw SerializationError("Map types require a key type and a value type.");
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
    else if (isMap(value))
    {
        result["type"] = "map";
        Json entries(Array{});
        for (const MapEntry& entry : asMap(value)->entries)
        {
            if (!entry.active)
                continue;
            Json serializedEntry(Object{});
            serializedEntry["key"] = SerializeValue(entry.key, depth + 1);
            serializedEntry["value"] = SerializeValue(entry.value, depth + 1);
            entries.push_back(std::move(serializedEntry));
        }
        result["entries"] = std::move(entries);
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
    if (type == "map")
    {
        ObjMap* map = newMap();
        const Array& entries = Field(json, "entries", crude_json::type_t::array).get<Array>();
        for (const Json& entry : entries)
        {
            const Value key = DeserializeValue(Field(entry, "key", crude_json::type_t::object), depth + 1);
            const Value value = DeserializeValue(Field(entry, "value", crude_json::type_t::object), depth + 1);
            map->set(key, value);
        }
        return Value(map);
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

Json SerializeProperty(const ScriptProperty& property, const Script& script)
{
    Json result(Object{});
    result["id"] = property.PersistentId.ToString();
    result["name"] = property.Name;
    result["description"] = property.Description;
    result["declared_type"] = SerializeTypeRef(property.type, &script);
    result["default"] = SerializeValue(property.defaultValue);
    return result;
}

ScriptPropertyPtr DeserializeProperty(const Json& json, IdSet& ids, Script& script, int formatVersion, IDGenerator* runtimeIds)
{
    const ScriptElementUuid persistentId = formatVersion >= 8 ? RequiredDurableId<ScriptElementUuid>(json, "id") : ScriptElementUuid{};
    const int id = formatVersion >= 8 ? CachedRuntimeId(script, RuntimeIdKey("element", persistentId), "Property", ids, *runtimeIds)
                                      : DeserializeRuntimeId(json, "Property", formatVersion, ids, runtimeIds);
    ScriptPropertyPtr property = std::make_shared<ScriptProperty>(id, StringField(json, "name").c_str());
    CaptureSerializedFields(json, property->SerializedExtensions);
    property->PersistentId = formatVersion >= 8 ? persistentId
        : DeserializePersistentId(json, formatVersion, ScriptElementUuid::FromLegacy(script.ModuleIdentity.Value(), "element:" + std::to_string(id)));
    property->Description = OptionalStringField(json, "description");
    property->defaultValue = DeserializeValue(Field(json, "default", crude_json::type_t::object));
    if (const Json* type = OptionalField(json, "declared_type"))
        property->type = DeserializeTypeRef(*type);
    else
        property->type = MigratedTypeOfValue(property->defaultValue);
    return property;
}

Json SerializeDefinitionPort(const BasicFunctionDef::Input& port, const Script& script)
{
    Json result(Object{});
    result["id"] = port.persistentId.ToString();
    result["name"] = port.name;
    result["description"] = port.description;
    result["declared_type"] = SerializeTypeRef(port.type, &script);
    result["default"] = SerializeValue(port.value);
    return result;
}

BasicFunctionDef::Input DeserializeDefinitionPort(const Json& json, IdSet& ids, Script& script, ScriptElementUuid functionId, const char* direction,
                                                  int formatVersion, IDGenerator* runtimeIds)
{
    BasicFunctionDef::Input port;
    CaptureSerializedFields(json, port.serializedExtensions);
    const ScriptPortId persistentId = formatVersion >= 8 ? RequiredDurableId<ScriptPortId>(json, "id") : ScriptPortId{};
    port.id = formatVersion >= 8 ? CachedRuntimeId(script, RuntimeIdKey("port", persistentId), "Function port", ids, *runtimeIds)
                                 : DeserializeRuntimeId(json, "Function port", formatVersion, ids, runtimeIds);
    port.persistentId = formatVersion >= 8 ? persistentId : DeserializePersistentId(json, formatVersion,
        ScriptPortId::FromLegacy(functionId.Value(), std::string(direction) + ":" + std::to_string(port.id)));
    port.name = StringField(json, "name");
    port.description = OptionalStringField(json, "description");
    port.value = DeserializeValue(Field(json, "default", crude_json::type_t::object));
    if (const Json* type = OptionalField(json, "declared_type"))
        port.type = DeserializeTypeRef(*type);
    else
        port.type = MigratedTypeOfValue(port.value);
    return port;
}

Json SerializeGenericTypeProperty(const GenericTypeProperty& property)
{
    Json result(Object{});
    result["variable_name"] = property.variableName;
    result["label"] = property.label;
    result["key"] = property.key.empty() ? property.variableName : property.key;
    return result;
}

GenericTypeProperty DeserializeGenericTypeProperty(const Json& json)
{
    GenericTypeProperty property;
    property.variableName = StringField(json, "variable_name");
    property.label = StringField(json, "label");
    property.key = OptionalStringField(json, "key", property.variableName);
    if (property.variableName.empty() || property.label.empty())
        throw SerializationError("Generic type properties require a variable name and label.");
    return property;
}

const char* PortIdentityKindName(PortIdentityKind kind)
{
    switch (kind)
    {
    case PortIdentityKind::Fixed: return "fixed";
    case PortIdentityKind::Script: return "script";
    case PortIdentityKind::Dynamic: return "dynamic";
    default: return "none";
    }
}

Json SerializePortIdentity(const PortIdentity& identity)
{
    Json result(Object{});
    result["kind"] = PortIdentityKindName(identity.kind);
    if (identity.kind == PortIdentityKind::Fixed)
    {
        if (identity.key.empty()) throw SerializationError("Fixed port identity has no key.");
        result["key"] = identity.key;
    }
    else if (identity.kind == PortIdentityKind::Script)
    {
        if (!identity.scriptPortId.IsValid()) throw SerializationError("Script port identity has no UUID.");
        result["port_id"] = identity.scriptPortId.ToString();
    }
    else if (identity.kind == PortIdentityKind::Dynamic)
    {
        if (identity.family.empty() || !identity.dynamicSlot.IsValid() || identity.member.empty())
            throw SerializationError("Dynamic port identity is incomplete.");
        result["family"] = identity.family;
        result["slot"] = identity.dynamicSlot.ToString();
        result["member"] = identity.member;
    }
    else
    {
        throw SerializationError("Port has no semantic identity.");
    }
    return result;
}

PortIdentity DeserializePortIdentity(const Json& json)
{
    PortIdentity result;
    const std::string kind = StringField(json, "kind");
    if (kind == "fixed") result.kind = PortIdentityKind::Fixed;
    else if (kind == "script") result.kind = PortIdentityKind::Script;
    else if (kind == "dynamic") result.kind = PortIdentityKind::Dynamic;
    else throw SerializationError("Unknown semantic port identity kind '" + kind + "'.");
    result.key = OptionalStringField(json, "key");
    result.family = OptionalStringField(json, "family");
    result.member = OptionalStringField(json, "member");
    if (const Json* portId = OptionalField(json, "port_id"))
    {
        if (!portId->is_string()) throw SerializationError("Script port UUID has the wrong type.");
        result.scriptPortId = ScriptPortId::Parse(portId->get<crude_json::string>());
    }
    if (const Json* legacyPortId = OptionalField(json, "legacy_port_id"))
    {
        if (!legacyPortId->is_number()) throw SerializationError("Semantic port legacy ID has the wrong type.");
        result.legacyScriptPortId = static_cast<int>(legacyPortId->get<crude_json::number>());
    }
    if (const Json* slot = OptionalField(json, "slot"))
    {
        if (!slot->is_string()) throw SerializationError("Dynamic port slot UUID has the wrong type.");
        result.dynamicSlot = DynamicSlotId::Parse(slot->get<crude_json::string>());
    }
    return result;
}

std::string RuntimePinIdKey(GraphNodeId nodeId, PinKind direction, const PortIdentity& identity)
{
    std::string result = "pin:" + nodeId.ToString() + ":" + (direction == PinKind::Input ? "input:" : "output:") + PortIdentityKindName(identity.kind);
    if (identity.kind == PortIdentityKind::Fixed) return result + ":" + identity.key;
    if (identity.kind == PortIdentityKind::Script) return result + ":" + identity.scriptPortId.ToString();
    if (identity.kind == PortIdentityKind::Dynamic)
        return result + ":" + identity.family + ":" + identity.dynamicSlot.ToString() + ":" + identity.member;
    return result + ":none";
}

Json SerializePin(const Pin& pin, const Script& script, bool unresolved = false)
{
    if (pin.Identity.kind == PortIdentityKind::None)
        throw SerializationError("Port '" + pin.Name + "' has no semantic identity for format version 8.");
    Json result(Object{});
    result["identity"] = SerializePortIdentity(pin.Identity);
    result["display_name"] = pin.Name;
    result["type_hint"] = SerializeTypeRef(pin.DeclaredType, &script);
    if (unresolved)
        result["resolution"] = "unresolved";
    return result;
}

Json SerializeInputPin(const InputPin& pin, const Script& script, bool unresolved = false)
{
    Json result = SerializePin(pin, script, unresolved);
    result["value"] = SerializeValue(pin.LiteralValue);
    return result;
}

Pin DeserializePin(const Json& json, IdSet& ids, int formatVersion, IDGenerator* runtimeIds, int preferredRuntimeId = 0)
{
    const int id = formatVersion >= 8 && preferredRuntimeId > 0 ? preferredRuntimeId : DeserializeRuntimeId(json, "Pin", formatVersion, ids, runtimeIds);
    if (formatVersion >= 8 && preferredRuntimeId > 0) ids.Add(id, "Pin");
    const std::string name = formatVersion >= 8 ? StringField(json, "display_name") : StringField(json, "name");
    const Json* declaredType = OptionalField(json, formatVersion >= 8 ? "type_hint" : "declared_type");
    TypeRef type = declaredType
        ? DeserializeTypeRef(*declaredType)
        : TypeRef(ParsePinType(StringField(json, "type")));
    Pin result(id, name.c_str(), std::move(type), formatVersion >= 8 ? std::string{} : OptionalStringField(json, "description"));
    CaptureSerializedFields(json, result.SerializedExtensions);
    if (const Json* identity = OptionalField(json, "identity"))
    {
        if (!identity->is_object()) throw SerializationError("Semantic port identity has the wrong type.");
        result.Identity = DeserializePortIdentity(*identity);
    }
    else if (formatVersion >= 8)
    {
        throw SerializationError("Format version 8 port has no semantic identity.");
    }
    return result;
}

bool IsSerializedPortUnresolved(const Json& json)
{
    const Json* resolution = OptionalField(json, "resolution");
    if (!resolution) return false;
    if (!resolution->is_string()) throw SerializationError("Saved port resolution has the wrong type.");
    const std::string value = resolution->get<crude_json::string>();
    if (value == "unresolved") return true;
    if (value == "resolved") return false;
    throw SerializationError("Unknown saved port resolution '" + value + "'.");
}

bool IsActivePin(const Pin* pin)
{
    if (!pin || !pin->Node || pin->Node->IsSerializationPlaceholder) return false;
    const Node& node = *pin->Node;
    const auto contains = [&](const auto& pins)
    {
        return std::any_of(pins.begin(), pins.end(), [&](const Pin& candidate) { return candidate.ID == pin->ID; });
    };
    return contains(node.Inputs) || contains(node.Outputs);
}

bool PortIdentityMatches(const Pin& saved, const Pin& definition, const std::map<std::string, std::string>* aliases = nullptr)
{
    if (saved.Identity.kind != PortIdentityKind::None && definition.Identity.kind != PortIdentityKind::None)
    {
        if (PortIdentitiesMatch(saved.Identity, definition.Identity)) return true;
        if (aliases && saved.Identity.kind == PortIdentityKind::Fixed && definition.Identity.kind == PortIdentityKind::Fixed)
        {
            const auto alias = aliases->find(saved.Identity.key);
            return alias != aliases->end() && alias->second == definition.Identity.key;
        }
        return false;
    }
    return saved.Name == definition.Name && saved.Kind == definition.Kind;
}

void ReconcileFixedInputs(Node& node, const std::vector<InputPin>& definitionPins, const std::vector<Value>& definitionValues, IdSet& ids,
                          const std::map<std::string, std::string>* aliases)
{
    std::vector<std::pair<Pin, Value>> saved;
    saved.reserve(node.Inputs.size() + node.UnresolvedInputs.size());
    for (size_t index = 0; index < node.Inputs.size(); ++index)
        saved.emplace_back(node.Inputs[index], node.InputValues[index]);
    for (size_t index = 0; index < node.UnresolvedInputs.size(); ++index)
        saved.emplace_back(node.UnresolvedInputs[index], node.UnresolvedInputValues[index]);
    const bool useLegacyPositions = saved.size() == definitionPins.size() &&
        std::all_of(saved.begin(), saved.end(), [](const auto& item) { return item.first.Identity.kind == PortIdentityKind::None; });

    node.Inputs.clear();
    node.InputValues.clear();
    for (size_t index = 0; index < definitionPins.size(); ++index)
    {
        const Pin& definition = definitionPins[index];
        auto existing = std::find_if(saved.begin(), saved.end(), [&](const auto& item) { return PortIdentityMatches(item.first, definition, aliases); });
        if (existing == saved.end() && useLegacyPositions && !saved.empty()) existing = saved.begin();
        if (existing == saved.end())
        {
            node.Inputs.push_back(definition);
            node.InputValues.push_back(definitionValues[index]);
            ids.Add(static_cast<int>(definition.ID.Get()), "Migrated pin");
            continue;
        }

        Pin reconciled = existing->first;
        reconciled.Name = definition.Name;
        reconciled.Type = definition.Type;
        reconciled.DeclaredType = definition.DeclaredType;
        reconciled.Description = definition.Description;
        reconciled.Identity = definition.Identity;
        node.Inputs.push_back(std::move(reconciled));
        node.InputValues.push_back(existing->second);
        saved.erase(existing);
    }

    node.UnresolvedInputs.clear();
    node.UnresolvedInputValues.clear();
    for (auto& [pin, value] : saved)
    {
        node.UnresolvedInputs.push_back(std::move(pin));
        node.UnresolvedInputValues.push_back(value);
    }
}

void ReconcileFixedOutputs(Node& node, const std::vector<Pin>& definitionPins, IdSet& ids, const std::map<std::string, std::string>* aliases)
{
    std::vector<Pin> saved = node.Outputs;
    saved.insert(saved.end(), node.UnresolvedOutputs.begin(), node.UnresolvedOutputs.end());
    const bool useLegacyPositions = saved.size() == definitionPins.size() &&
        std::all_of(saved.begin(), saved.end(), [](const Pin& pin) { return pin.Identity.kind == PortIdentityKind::None; });

    node.Outputs.clear();
    for (const Pin& definition : definitionPins)
    {
        auto existing = std::find_if(saved.begin(), saved.end(), [&](const Pin& pin) { return PortIdentityMatches(pin, definition, aliases); });
        if (existing == saved.end() && useLegacyPositions && !saved.empty()) existing = saved.begin();
        if (existing == saved.end())
        {
            node.Outputs.push_back(definition);
            ids.Add(static_cast<int>(definition.ID.Get()), "Migrated pin");
            continue;
        }

        Pin reconciled = *existing;
        reconciled.Name = definition.Name;
        reconciled.Type = definition.Type;
        reconciled.DeclaredType = definition.DeclaredType;
        reconciled.Description = definition.Description;
        reconciled.Identity = definition.Identity;
        node.Outputs.push_back(std::move(reconciled));
        saved.erase(existing);
    }
    node.UnresolvedOutputs = std::move(saved);
}

void RestoreDynamicPortIdentities(Node& node, const std::vector<InputPin>& definitionInputs, const std::vector<Pin>& definitionOutputs)
{
    if (node.DefinitionId == "vlox.std.compiled.flow.switch")
    {
        if (!node.Inputs.empty()) node.Inputs[0].Identity = PortIdentity::Fixed("execute");
        for (size_t index = 1; index < node.Inputs.size(); ++index)
        {
            const DynamicSlotId slot = node.Inputs[index].Identity.kind == PortIdentityKind::Dynamic
                ? node.Inputs[index].Identity.dynamicSlot : DynamicSlotId::FromLegacy(node.PersistentId.Value(), "case:" + std::to_string(index - 1));
            node.Inputs[index].Identity = PortIdentity::Dynamic("case", slot, "condition");
            if (index - 1 < node.Outputs.size() - 1) node.Outputs[index - 1].Identity = PortIdentity::Dynamic("case", slot, "branch");
        }
        if (!node.Outputs.empty()) node.Outputs.back().Identity = PortIdentity::Fixed("default");
        return;
    }
    if (node.DefinitionId == "vlox.std.compiled.flow.match")
    {
        if (!node.Inputs.empty()) node.Inputs[0].Identity = PortIdentity::Fixed("execute");
        if (node.Inputs.size() > 1) node.Inputs[1].Identity = PortIdentity::Fixed("value");
        for (size_t index = 2; index < node.Inputs.size(); ++index)
        {
            const DynamicSlotId slot = node.Inputs[index].Identity.kind == PortIdentityKind::Dynamic
                ? node.Inputs[index].Identity.dynamicSlot : DynamicSlotId::FromLegacy(node.PersistentId.Value(), "case:" + std::to_string(index - 2));
            node.Inputs[index].Identity = PortIdentity::Dynamic("case", slot, "pattern");
            if (index - 2 < node.Outputs.size() - 1) node.Outputs[index - 2].Identity = PortIdentity::Dynamic("case", slot, "branch");
        }
        if (!node.Outputs.empty()) node.Outputs.back().Identity = PortIdentity::Fixed("default");
        return;
    }

    std::string family = "item";
    std::string member = "value";
    const auto definitionDynamic = std::find_if(definitionInputs.begin(), definitionInputs.end(), [](const Pin& pin) { return pin.Identity.kind == PortIdentityKind::Dynamic; });
    if (definitionDynamic != definitionInputs.end())
    {
        family = definitionDynamic->Identity.family;
        member = definitionDynamic->Identity.member;
    }
    size_t dataIndex = 0;
    for (size_t index = 0; index < node.Inputs.size(); ++index)
    {
        Pin& input = node.Inputs[index];
        if (input.Identity.kind == PortIdentityKind::None && index < definitionInputs.size() && input.Type == PinType::Flow)
            input.Identity = definitionInputs[index].Identity;
        if (input.Type == PinType::Flow) continue;
        if (input.Identity.kind == PortIdentityKind::None)
            input.Identity = PortIdentity::Dynamic(family, DynamicSlotId::FromLegacy(node.PersistentId.Value(), family + ":" + std::to_string(dataIndex)), member);
        ++dataIndex;
    }
    for (size_t index = 0; index < node.Outputs.size() && index < definitionOutputs.size(); ++index)
        if (node.Outputs[index].Identity.kind == PortIdentityKind::None)
            node.Outputs[index].Identity = definitionOutputs[index].Identity;
}

void AssignUnresolvedLegacyPortIdentities(Node& node)
{
    const auto assign = [&](Pin& pin, const char* direction)
    {
        if (pin.Identity.kind != PortIdentityKind::None) return;
        const DynamicSlotId slot = DynamicSlotId::FromLegacy(node.PersistentId.Value(),
            std::string("unresolved:") + direction + ":" + std::to_string(pin.ID.Get()));
        pin.Identity = PortIdentity::Dynamic("unresolved", slot, "port");
    };
    for (InputPin& pin : node.Inputs) assign(pin, "input");
    for (InputPin& pin : node.UnresolvedInputs) assign(pin, "input");
    for (Pin& pin : node.Outputs) assign(pin, "output");
    for (Pin& pin : node.UnresolvedOutputs) assign(pin, "output");
}

int MaximumSerializedId(const Json& json)
{
    int maximum = 0;
    if (json.is_array())
    {
        for (const Json& item : json.get<Array>()) maximum = std::max(maximum, MaximumSerializedId(item));
    }
    else if (json.is_object())
    {
        for (const auto& [name, item] : json.get<Object>())
        {
            if (name == "id" && item.is_number())
            {
                const double number = item.get<crude_json::number>();
                if (number > 0 && number <= std::numeric_limits<int>::max() && std::floor(number) == number)
                    maximum = std::max(maximum, static_cast<int>(number));
            }
            maximum = std::max(maximum, MaximumSerializedId(item));
        }
    }
    return maximum;
}

ScriptElementUuid FindPersistentElementId(const Script& script, ScriptElementID runtimeId)
{
    const auto functionElement = [&](const ScriptFunctionPtr& function) -> ScriptElementUuid
    {
        if (!function) return {};
        if (function->ID == runtimeId) return function->PersistentId;
        for (const ScriptPropertyPtr& variable : function->variables)
            if (variable->ID == runtimeId) return variable->PersistentId;
        return {};
    };
    if (const ScriptElementUuid id = functionElement(script.main); id.IsValid()) return id;
    for (const ScriptPropertyPtr& variable : script.variables)
        if (variable->ID == runtimeId) return variable->PersistentId;
    for (const ScriptFunctionPtr& function : script.functions)
        if (const ScriptElementUuid id = functionElement(function); id.IsValid()) return id;
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (scriptClass->ID == runtimeId) return scriptClass->PersistentId;
        for (const ScriptPropertyPtr& property : scriptClass->properties)
            if (property->ID == runtimeId) return property->PersistentId;
        for (const ScriptFunctionPtr& method : scriptClass->methods)
            if (const ScriptElementUuid id = functionElement(method); id.IsValid()) return id;
        if (const ScriptElementUuid id = functionElement(scriptClass->constructor); id.IsValid()) return id;
    }
    return {};
}

Json SerializeNode(const Node& node, const Script& script)
{
    if (node.SerializationType.empty())
        throw SerializationError("Node " + std::to_string(node.ID.Get()) + " has no stable serialization type.");

    Json result(Object{});
    result["id"] = node.PersistentId.ToString();
    result["kind"] = node.SerializationType;
    Json definition(Object{});
    definition["id"] = node.DefinitionId;
    definition["revision"] = static_cast<double>(node.DefinitionRevision);
    result["definition"] = std::move(definition);
    result["display_name"] = node.Name;
    ScriptElementUuid targetSymbol = node.refPersistentId;
    if (!targetSymbol.IsValid() && node.refId.IsValid()) targetSymbol = FindPersistentElementId(script, node.refId);
    if (targetSymbol.IsValid())
    {
        Json target(Object{});
        target["module"] = (node.refModuleId.IsValid() ? node.refModuleId : script.ModuleIdentity).ToString();
        target["symbol"] = targetSymbol.ToString();
        target["display_name"] = node.Name;
        result["target"] = std::move(target);
    }
    result["state"] = node.State;
    result["description"] = node.Description;
    if (node.Type == NodeType::CommentBox)
    {
        result["text"] = node.Name;
        result["comment_box_color"] = CommentBoxColorName(static_cast<const CommentBoxNode&>(node).BoxColor);
    }

    Json typeOverrides(Object{});
    for (const auto& [name, type] : node.TypeOverrides)
        typeOverrides[name] = SerializeTypeRef(type, &script);
    result["type_overrides"] = std::move(typeOverrides);

    Json inputs(Array{});
    for (const InputPin& pin : node.Inputs)
        inputs.push_back(SerializeInputPin(pin, script));
    for (const InputPin& pin : node.UnresolvedInputs)
        inputs.push_back(SerializeInputPin(pin, script, true));
    result["inputs"] = std::move(inputs);

    Json outputs(Array{});
    for (const Pin& pin : node.Outputs)
        outputs.push_back(SerializePin(pin, script));
    for (const Pin& pin : node.UnresolvedOutputs)
        outputs.push_back(SerializePin(pin, script, true));
    result["outputs"] = std::move(outputs);
    return result;
}

struct MissingSerializedNode final : Node
{
    MissingSerializedNode(int id, std::string name) : Node(id, name.c_str(), ImColor(190, 70, 70))
    {
        InstanceFlags |= NodeInstanceFlags::Error;
    }

    void Compile(CompilerContext&, const Graph&, CompilationStage, int) const override {}
};

ScriptElementID FindLegacyElementId(const Script& script, ScriptElementUuid persistentId)
{
    const auto functionId = [&](const ScriptFunctionPtr& function) -> ScriptElementID
    {
        if (!function) return ScriptElementID::Invalid;
        if (function->PersistentId == persistentId) return function->ID;
        for (const ScriptPropertyPtr& variable : function->variables)
            if (variable->PersistentId == persistentId) return variable->ID;
        return ScriptElementID::Invalid;
    };
    if (const ScriptElementID id = functionId(script.main); id.IsValid()) return id;
    for (const ScriptPropertyPtr& variable : script.variables)
        if (variable->PersistentId == persistentId) return variable->ID;
    for (const ScriptFunctionPtr& function : script.functions)
        if (const ScriptElementID id = functionId(function); id.IsValid()) return id;
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (scriptClass->PersistentId == persistentId) return scriptClass->ID;
        for (const ScriptPropertyPtr& property : scriptClass->properties)
            if (property->PersistentId == persistentId) return property->ID;
        for (const ScriptFunctionPtr& method : scriptClass->methods)
            if (const ScriptElementID id = functionId(method); id.IsValid()) return id;
        if (const ScriptElementID id = functionId(scriptClass->constructor); id.IsValid()) return id;
    }
    return ScriptElementID::Invalid;
}

NodePtr CreateMissingNode(IDGenerator& ids, const std::string& kind, const std::string& definition, uint32_t revision, const std::string& displayName, std::string error)
{
    NodePtr node = std::make_shared<MissingSerializedNode>(ids.GetNextId(), displayName.empty() ? definition : displayName);
    node->SerializationType = kind;
    node->DefinitionId = definition;
    node->DefinitionRevision = revision;
    node->IsSerializationPlaceholder = true;
    node->Error = std::move(error);
    return node;
}

NodePtr CreateNode(const Json& json, const NodeRegistry& registry, Script& script,
                   const ScriptFunctionPtr& owner, IDGenerator& constructionIds,
                   std::vector<SerializationDiagnostic>* diagnostics = nullptr, std::string path = {}, int formatVersion = 7)
{
    const std::string kind = StringField(json, "kind");
    const Json* definitionObject = formatVersion >= 8 ? &Field(json, "definition", crude_json::type_t::object) : nullptr;
    const std::string definition = definitionObject ? StringField(*definitionObject, "id") : StringField(json, "definition");
    ScriptElementID reference = formatVersion >= 8 ? ScriptElementID::Invalid : ScriptElementID(IntField(json, "reference_id"));
    if (const Json* target = OptionalField(json, "target"))
    {
        if (!target->is_object()) throw SerializationError("Node target has the wrong type.");
        const ModuleId targetModule = ModuleId::Parse(StringField(*target, "module"));
        const ScriptElementUuid targetSymbol = ScriptElementUuid::Parse(StringField(*target, "symbol"));
        if (targetModule == script.ModuleIdentity)
        {
            const ScriptElementID resolved = FindLegacyElementId(script, targetSymbol);
            if (resolved.IsValid()) reference = resolved;
            else
            {
                const std::string cacheKey = RuntimeIdKey("element", targetSymbol);
                const auto cached = script.RuntimeIdCache.find(cacheKey);
                reference = cached != script.RuntimeIdCache.end() ? ScriptElementID(cached->second) : ScriptElementID(constructionIds.GetNextId());
                script.RuntimeIdCache[cacheKey] = reference.id;
            }
        }
        else
        {
            reference = ScriptElementID::Invalid;
        }
    }
    const int savedRevisionValue = definitionObject ? IntField(*definitionObject, "revision")
        : OptionalField(json, "definition_revision") ? IntField(json, "definition_revision") : 1;
    if (savedRevisionValue < 1) throw SerializationError("Node definition revision must be positive.");
    const uint32_t savedRevision = static_cast<uint32_t>(savedRevisionValue);
    const std::string displayName = OptionalStringField(json, "display_name", definition);
    const auto recoverMissing = [&](std::string code, std::string message)
    {
        if (diagnostics)
            diagnostics->push_back({ SerializationDiagnosticSeverity::Warning, std::move(code), path, definition, message });
        return CreateMissingNode(constructionIds, kind, definition, savedRevision, displayName, std::move(message));
    };

    if (kind == "begin") return BuildBeginNode(constructionIds, owner);
    if (kind == "comment_box" || kind == "comment")
        return BuildCommentBoxNode(constructionIds, OptionalStringField(json, "text", "Comment Box"),
            ParseCommentBoxColor(OptionalStringField(json, "comment_box_color", "gray")));
    if (kind == "return") return BuildReturnNode(constructionIds, *owner);
    if (kind == "variable.get" || kind == "variable.set")
    {
        const ScriptElementID ownerId = owner ? owner->ID : ScriptElementID::Invalid;
        ScriptPropertyPtr property = owner
            ? ScriptUtils::FindVisibleVariableById(script, owner->ID.id, reference.id)
            : ScriptUtils::FindVariableById(script, reference);
        NodePtr node = kind == "variable.get"
            ? BuildGetVariableNode(constructionIds, property, reference, ownerId)
            : BuildSetVariableNode(constructionIds, property, reference, ownerId);
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
                return recoverMissing("definition.missing", "Missing native definition '" + definition + "'.");
            if (savedRevision > native->functionDef->revision)
                return recoverMissing("definition.newer_revision", "Saved definition revision is newer than the installed native definition.");
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
            return recoverMissing("definition.missing", "Missing compiled definition '" + definition + "'.");
        if (savedRevision > compiled->revision)
            return recoverMissing("definition.newer_revision", "Saved definition revision is newer than the installed compiled definition.");
        return compiled->MakeNode(constructionIds);
    }

    throw SerializationError("Unknown node kind '" + kind + "'.");
}

Json SerializeGraph(const Graph& graph, const Script& script)
{
    Json result(Object{});
    Json nodes(Array{});
    for (const NodePtr& node : graph.GetNodes())
        nodes.push_back(SerializeNode(*node, script));
    result["nodes"] = std::move(nodes);

    Json links(Array{});
    for (const Link& link : graph.GetLinks())
    {
        const Pin* start = graph.FindPin(link.StartPinID);
        const Pin* end = graph.FindPin(link.EndPinID);
        if (!start || !end || !start->Node || !end->Node)
            throw SerializationError("Link " + std::to_string(link.ID.Get()) + " references a missing pin or node.");
        const auto serializeEndpoint = [&](const Pin& pin)
        {
            Json endpoint(Object{});
            endpoint["node"] = pin.Node->PersistentId.ToString();
            endpoint["port"] = SerializePortIdentity(pin.Identity);
            endpoint["display_name"] = pin.Name;
            endpoint["type_hint"] = SerializeTypeRef(pin.DeclaredType, &script);
            return endpoint;
        };
        Json item(Object{});
        item["id"] = link.PersistentId.ToString();
        item["from"] = serializeEndpoint(*start);
        item["to"] = serializeEndpoint(*end);
        if (!link.IsResolved) item["resolution"] = "unresolved";
        links.push_back(std::move(item));
    }
    result["links"] = std::move(links);
    return result;
}

const Pin* FindSemanticEndpoint(const Graph& graph, const Json& endpoint, PinKind direction)
{
    if (!endpoint.is_object()) throw SerializationError("Link endpoint has the wrong type.");
    const GraphNodeId nodeId = GraphNodeId::Parse(StringField(endpoint, "node"));
    const PortIdentity identity = DeserializePortIdentity(Field(endpoint, "port", crude_json::type_t::object));
    const NodePtr node = graph.FindNodeIf([&](const NodePtr& candidate) { return candidate && candidate->PersistentId == nodeId; });
    if (!node) return nullptr;

    const auto find = [&](const auto& pins) -> const Pin*
    {
        const auto match = std::find_if(pins.begin(), pins.end(), [&](const Pin& pin) { return PortIdentitiesMatch(pin.Identity, identity); });
        return match == pins.end() ? nullptr : &*match;
    };
    if (direction == PinKind::Input)
    {
        if (const Pin* pin = find(node->Inputs)) return pin;
        return find(node->UnresolvedInputs);
    }
    if (const Pin* pin = find(node->Outputs)) return pin;
    return find(node->UnresolvedOutputs);
}

void ResolveTypeReference(TypeRef& type, const Script& script);

void DeserializeGraph(const Json& json, const NodeRegistry& registry, Script& script,
                      const ScriptFunctionPtr& owner, Graph& graph, IdSet& ids,
                      IDGenerator& constructionIds, std::vector<SerializationDiagnostic>& diagnostics, const std::string& path, int formatVersion)
{
    CaptureSerializedFields(json, graph.SerializedExtensions);
    const Array& nodes = Field(json, "nodes", crude_json::type_t::array).get<Array>();
    int beginNodeCount = 0;
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
    {
        const Json& nodeJson = nodes[nodeIndex];
        NodePtr node = CreateNode(nodeJson, registry, script, owner, constructionIds, &diagnostics,
            path + ".nodes[" + std::to_string(nodeIndex) + "]", formatVersion);
        CaptureSerializedFields(nodeJson, node->SerializedExtensions);
        if (const Json* target = OptionalField(nodeJson, "target"))
        {
            if (!target->is_object()) throw SerializationError("Node target has the wrong type.");
            node->refModuleId = ModuleId::Parse(StringField(*target, "module"));
            node->refPersistentId = ScriptElementUuid::Parse(StringField(*target, "symbol"));
        }
        const GraphNodeId persistentNodeId = formatVersion >= 8 ? RequiredDurableId<GraphNodeId>(nodeJson, "id") : GraphNodeId{};
        const int nodeId = formatVersion >= 8
            ? CachedRuntimeId(script, RuntimeIdKey("node", persistentNodeId), "Node", ids, constructionIds, static_cast<int>(node->ID.Get()))
            : IntField(nodeJson, "id");
        if (formatVersion < 8) ids.Add(nodeId, "Node");
        node->ID = ed::NodeId(nodeId);
        node->PersistentId = formatVersion >= 8 ? persistentNodeId : DeserializePersistentId(nodeJson, formatVersion,
            GraphNodeId::FromLegacy(script.ModuleIdentity.Value(), "node:" + std::to_string(nodeId)));
        node->State = StringField(nodeJson, "state");
        if (const Json* description = OptionalField(nodeJson, "description"))
        {
            if (!description->is_string())
                throw SerializationError("Node description has the wrong type.");
            node->Description = description->get<crude_json::string>();
        }
        if (const Json* typeOverrides = OptionalField(nodeJson, "type_overrides"))
        {
            if (!typeOverrides->is_object())
                throw SerializationError("Node type overrides have the wrong type.");
            for (const auto& [name, type] : typeOverrides->get<Object>())
            {
                TypeRef deserialized = DeserializeTypeRef(type);
                ResolveTypeReference(deserialized, script);
                node->TypeOverrides[name] = std::move(deserialized);
            }
        }

        const Array& inputs = Field(nodeJson, "inputs", crude_json::type_t::array).get<Array>();
        const size_t resolvedInputCount = static_cast<size_t>(std::count_if(inputs.begin(), inputs.end(),
            [](const Json& input) { return !IsSerializedPortUnresolved(input); }));
        const bool hasDynamicInputs = HasFlag(node->DefinitionFlags, NodeDefinitionFlags::DynamicInputs);
        const bool isMissingReference = HasFlag(node->InstanceFlags, NodeInstanceFlags::Error) &&
            (dynamic_cast<MissingSerializedNode*>(node.get()) || node->SerializationType == "variable.get" || node->SerializationType == "variable.set" ||
             node->SerializationType == "function.get" || node->SerializationType == "function.call" ||
             node->SerializationType == "class.construct" || node->SerializationType == "property.get" ||
             node->SerializationType == "property.set" || node->SerializationType == "method.call" ||
             node->SerializationType == "method.get");
        if (!isMissingReference && hasDynamicInputs && !node->IsValidDynamicInputCount(resolvedInputCount))
            throw SerializationError("Node " + std::to_string(nodeId) + " has an invalid input layout.");
        const std::vector<InputPin> definitionInputs = node->Inputs;
        const std::vector<Value> definitionInputValues = node->InputValues.Snapshot();
        node->Inputs.clear();
        node->UnresolvedInputs.clear();
        std::set<int> reusedInputIds;
        const auto preferredPinId = [&](const Json& savedPin, const auto& definitionPins, std::set<int>& reusedIds, PinKind direction)
        {
            if (formatVersion < 8) return 0;
            const PortIdentity savedIdentity = DeserializePortIdentity(Field(savedPin, "identity", crude_json::type_t::object));
            const auto cached = script.RuntimeIdCache.find(RuntimePinIdKey(node->PersistentId, direction, savedIdentity));
            if (cached != script.RuntimeIdCache.end())
            {
                reusedIds.insert(cached->second);
                return cached->second;
            }
            const auto reusable = std::find_if(definitionPins.begin(), definitionPins.end(), [&](const Pin& definitionPin)
            {
                if (reusedIds.count(definitionPin.ID.Get()) != 0 || definitionPin.Identity.kind != savedIdentity.kind) return false;
                if (PortIdentitiesMatch(definitionPin.Identity, savedIdentity)) return true;
                return savedIdentity.kind == PortIdentityKind::Dynamic && definitionPin.Identity.family == savedIdentity.family &&
                    definitionPin.Identity.member == savedIdentity.member;
            });
            if (reusable == definitionPins.end()) return 0;
            reusedIds.insert(reusable->ID.Get());
            return static_cast<int>(reusable->ID.Get());
        };
        for (const Json& pin : inputs)
        {
            Pin deserialized = DeserializePin(pin, ids, formatVersion, &constructionIds, preferredPinId(pin, definitionInputs, reusedInputIds, PinKind::Input));
            ResolveTypeReference(deserialized.Type, script);
            deserialized.DeclaredType = deserialized.Type;
            if (formatVersion >= 8) script.RuntimeIdCache[RuntimePinIdKey(node->PersistentId, PinKind::Input, deserialized.Identity)] = deserialized.ID.Get();
            if (IsSerializedPortUnresolved(pin)) node->UnresolvedInputs.push_back(std::move(deserialized));
            else node->Inputs.push_back(std::move(deserialized));
        }
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
        const size_t resolvedOutputCount = static_cast<size_t>(std::count_if(outputs.begin(), outputs.end(),
            [](const Json& output) { return !IsSerializedPortUnresolved(output); }));
        const bool validDynamicOutputs =
            (node->DefinitionId == "vlox.std.compiled.flow.match" &&
                resolvedOutputCount == resolvedInputCount - 1) ||
            (node->DefinitionId == "vlox.std.compiled.flow.switch" &&
                resolvedOutputCount == resolvedInputCount);
        if (!isMissingReference && hasDynamicInputs && !validDynamicOutputs && resolvedOutputCount != node->Outputs.size())
            throw SerializationError("Node " + std::to_string(nodeId) + " has an invalid output layout.");
        const std::vector<Pin> definitionOutputs = node->Outputs;
        node->Outputs.clear();
        node->UnresolvedOutputs.clear();
        std::set<int> reusedOutputIds;
        for (const Json& pin : outputs)
        {
            Pin deserialized = DeserializePin(pin, ids, formatVersion, &constructionIds, preferredPinId(pin, definitionOutputs, reusedOutputIds, PinKind::Output));
            ResolveTypeReference(deserialized.Type, script);
            deserialized.DeclaredType = deserialized.Type;
            if (formatVersion >= 8) script.RuntimeIdCache[RuntimePinIdKey(node->PersistentId, PinKind::Output, deserialized.Identity)] = deserialized.ID.Get();
            if (IsSerializedPortUnresolved(pin)) node->UnresolvedOutputs.push_back(std::move(deserialized));
            else node->Outputs.push_back(std::move(deserialized));
        }
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
        node->UnresolvedInputValues.clear();
        const bool hasInlineValues = std::any_of(inputs.begin(), inputs.end(), [](const Json& input) { return OptionalField(input, "value") != nullptr; });
        if (hasInlineValues && !std::all_of(inputs.begin(), inputs.end(), [](const Json& input) { return OptionalField(input, "value") != nullptr; }))
            throw SerializationError("Node " + std::to_string(nodeId) + " has only some input values stored with their ports.");
        const Json* legacyValues = OptionalField(nodeJson, "input_values");
        if (!hasInlineValues && !inputs.empty() && (!legacyValues || !legacyValues->is_array() || legacyValues->get<Array>().size() != inputs.size()))
            throw SerializationError("Node " + std::to_string(nodeId) + " has mismatched saved inputs and values.");
        for (size_t index = 0; index < inputs.size(); ++index)
        {
            const Json& encodedValue = hasInlineValues ? Field(inputs[index], "value", crude_json::type_t::object) : legacyValues->get<Array>()[index];
            Value value = DeserializeValue(encodedValue);
            if (IsSerializedPortUnresolved(inputs[index])) node->UnresolvedInputValues.push_back(value);
            else node->InputValues.push_back(value);
        }
        if (node->InputValues.size() != node->Inputs.size())
            throw SerializationError("Node " + std::to_string(nodeId) + " has mismatched inputs and values.");

        if (!isMissingReference && !hasDynamicInputs)
        {
            const BasicFunctionDef* installedDefinition = nullptr;
            if (node->SerializationType == "compiled")
            {
                const CompiledNodeDefPtr compiled = registry.FindCompiled(node->DefinitionId);
                if (compiled) installedDefinition = compiled->functionDef.get();
            }
            else if ((node->SerializationType == "function.call" || node->SerializationType == "function.get") && !node->refId.IsValid())
            {
                const NativeFunctionDef* native = registry.FindNative(node->DefinitionId);
                if (native) installedDefinition = native->functionDef.get();
            }
            ReconcileFixedInputs(*node, definitionInputs, definitionInputValues, ids, installedDefinition ? &installedDefinition->inputAliases : nullptr);
            ReconcileFixedOutputs(*node, definitionOutputs, ids, installedDefinition ? &installedDefinition->outputAliases : nullptr);
        }
        else if (!isMissingReference && hasDynamicInputs)
        {
            RestoreDynamicPortIdentities(*node, definitionInputs, definitionOutputs);
        }
        if (formatVersion < 8) AssignUnresolvedLegacyPortIdentities(*node);
        if (!node->UnresolvedInputs.empty() || !node->UnresolvedOutputs.empty())
            diagnostics.push_back({ SerializationDiagnosticSeverity::Warning, "port.unresolved",
                path + ".nodes[" + std::to_string(nodeIndex) + "]", node->PersistentId.ToString(),
                "One or more saved ports do not resolve against the installed definition." });

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
    for (size_t linkIndex = 0; linkIndex < links.size(); ++linkIndex)
    {
        const Json& linkJson = links[linkIndex];
        const PersistentLinkId persistentLinkId = formatVersion >= 8 ? RequiredDurableId<PersistentLinkId>(linkJson, "id") : PersistentLinkId{};
        const int id = formatVersion >= 8 ? CachedRuntimeId(script, RuntimeIdKey("link", persistentLinkId), "Link", ids, constructionIds)
                                          : IntField(linkJson, "id");
        if (formatVersion < 8) ids.Add(id, "Link");
        const Json* from = OptionalField(linkJson, "from");
        const Json* to = OptionalField(linkJson, "to");
        if ((from == nullptr) != (to == nullptr) || (formatVersion >= 8 && !from))
            throw SerializationError("Link must contain both semantic endpoints.");
        const Pin* start = from ? FindSemanticEndpoint(graph, *from, PinKind::Output) : graph.FindPin(ed::PinId(IntField(linkJson, "start_pin_id")));
        const Pin* end = to ? FindSemanticEndpoint(graph, *to, PinKind::Input) : graph.FindPin(ed::PinId(IntField(linkJson, "end_pin_id")));
        if (!start || !end)
            throw SerializationError("Link " + std::to_string(id) + " references a missing semantic endpoint.");
        const int startId = start->ID.Get();
        const int endId = end->ID.Get();
        if (start->Kind != PinKind::Output || end->Kind != PinKind::Input)
            throw SerializationError("Link " + std::to_string(id) + " has reversed pin directions.");
        const bool linkResolved = IsActivePin(start) && IsActivePin(end);
        if (linkResolved && !GraphUtils::AreTypesCompatible(start->Type, end->Type))
            throw SerializationError("Link " + std::to_string(id) + " connects incompatible pin types.");
        if (linkResolved && start->Node == end->Node)
            throw SerializationError("Link " + std::to_string(id) + " connects a node to itself.");
        if (linkResolved && start->Type == PinType::Flow)
        {
            if (!connectedFlowOutputs.insert(startId).second)
                throw SerializationError("Flow output pin " + std::to_string(startId) + " has multiple links.");
        }
        else if (linkResolved && !connectedDataInputs.insert(endId).second)
        {
            throw SerializationError("Data input pin " + std::to_string(endId) + " has multiple links.");
        }
        Link link{ ed::LinkId(id), ed::PinId(startId), ed::PinId(endId) };
        link.IsResolved = linkResolved;
        CaptureSerializedFields(linkJson, link.SerializedExtensions);
        link.PersistentId = formatVersion >= 8 ? persistentLinkId : DeserializePersistentId(linkJson, formatVersion,
            PersistentLinkId::FromLegacy(script.ModuleIdentity.Value(), "link:" + std::to_string(id)));
        link.Color = GetIconColor(start->Type);
        graph.AddLink(link);
        if (!linkResolved)
            diagnostics.push_back({ SerializationDiagnosticSeverity::Warning, "link.unresolved",
                path + ".links[" + std::to_string(linkIndex) + "]", link.PersistentId.ToString(),
                "The link is preserved but excluded from compilation until both endpoints resolve." });
    }
}

template<typename DurableIdentity>
void AddDurableIdentity(std::set<std::string>& identities, DurableIdentity identity, const char* description)
{
    if (!identity.IsValid()) throw SerializationError(std::string(description) + " UUID cannot be nil.");
    if (!identities.insert(identity.ToString()).second) throw SerializationError(std::string("Duplicate ") + description + " UUID '" + identity.ToString() + "'.");
}

void ValidateGraphDurableIdentities(const Graph& graph)
{
    std::set<std::string> nodeIds;
    std::set<std::string> linkIds;
    for (const NodePtr& node : graph.GetNodes()) AddDurableIdentity(nodeIds, node->PersistentId, "node");
    for (const Link& link : graph.GetLinks()) AddDurableIdentity(linkIds, link.PersistentId, "link");
}

void ValidateFunctionDurableIdentities(const ScriptFunction& function, std::set<std::string>& elementIds)
{
    AddDurableIdentity(elementIds, function.PersistentId, "script element");
    std::set<std::string> portIds;
    for (const BasicFunctionDef::Input& input : function.functionDef->inputs) AddDurableIdentity(portIds, input.persistentId, "script port");
    for (const BasicFunctionDef::Input& output : function.functionDef->outputs) AddDurableIdentity(portIds, output.persistentId, "script port");
    for (const ScriptPropertyPtr& variable : function.variables) AddDurableIdentity(elementIds, variable->PersistentId, "script element");
    ValidateGraphDurableIdentities(function.Graph);
}

void ValidateDurableIdentities(const Script& script)
{
    if (!script.ModuleIdentity.IsValid()) throw SerializationError("Module UUID cannot be nil.");
    std::set<std::string> elementIds;
    ValidateFunctionDurableIdentities(*script.main, elementIds);
    for (const ScriptPropertyPtr& variable : script.variables) AddDurableIdentity(elementIds, variable->PersistentId, "script element");
    for (const ScriptFunctionPtr& function : script.functions) ValidateFunctionDurableIdentities(*function, elementIds);
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        AddDurableIdentity(elementIds, scriptClass->PersistentId, "script element");
        for (const ScriptPropertyPtr& property : scriptClass->properties) AddDurableIdentity(elementIds, property->PersistentId, "script element");
        for (const ScriptFunctionPtr& method : scriptClass->methods) ValidateFunctionDurableIdentities(*method, elementIds);
        if (scriptClass->constructor) ValidateFunctionDurableIdentities(*scriptClass->constructor, elementIds);
    }
}

void CaptureRuntimeIdCache(Script& script)
{
    if (script.ID.IsValid()) script.RuntimeIdCache["script"] = script.ID.id;
    const auto property = [&](const ScriptPropertyPtr& value)
    {
        if (value && value->PersistentId.IsValid()) script.RuntimeIdCache[RuntimeIdKey("element", value->PersistentId)] = value->ID.id;
    };
    const auto graph = [&](const Graph& value)
    {
        for (const NodePtr& node : value.GetNodes())
        {
            if (!node || !node->PersistentId.IsValid()) continue;
            script.RuntimeIdCache[RuntimeIdKey("node", node->PersistentId)] = node->ID.Get();
            for (const InputPin& pin : node->Inputs)
                script.RuntimeIdCache[RuntimePinIdKey(node->PersistentId, PinKind::Input, pin.Identity)] = pin.ID.Get();
            for (const InputPin& pin : node->UnresolvedInputs)
                script.RuntimeIdCache[RuntimePinIdKey(node->PersistentId, PinKind::Input, pin.Identity)] = pin.ID.Get();
            for (const Pin& pin : node->Outputs)
                script.RuntimeIdCache[RuntimePinIdKey(node->PersistentId, PinKind::Output, pin.Identity)] = pin.ID.Get();
            for (const Pin& pin : node->UnresolvedOutputs)
                script.RuntimeIdCache[RuntimePinIdKey(node->PersistentId, PinKind::Output, pin.Identity)] = pin.ID.Get();
        }
        for (const Link& link : value.GetLinks())
            if (link.PersistentId.IsValid()) script.RuntimeIdCache[RuntimeIdKey("link", link.PersistentId)] = link.ID.Get();
    };
    const auto function = [&](const ScriptFunctionPtr& value)
    {
        if (!value) return;
        if (value->PersistentId.IsValid()) script.RuntimeIdCache[RuntimeIdKey("element", value->PersistentId)] = value->ID.id;
        for (const BasicFunctionDef::Input& input : value->functionDef->inputs)
            if (input.persistentId.IsValid()) script.RuntimeIdCache[RuntimeIdKey("port", input.persistentId)] = input.id;
        for (const BasicFunctionDef::Input& output : value->functionDef->outputs)
            if (output.persistentId.IsValid()) script.RuntimeIdCache[RuntimeIdKey("port", output.persistentId)] = output.id;
        for (const ScriptPropertyPtr& variable : value->variables) property(variable);
        graph(value->Graph);
    };
    function(script.main);
    for (const ScriptPropertyPtr& variable : script.variables) property(variable);
    for (const ScriptFunctionPtr& value : script.functions) function(value);
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (!scriptClass) continue;
        if (scriptClass->PersistentId.IsValid()) script.RuntimeIdCache[RuntimeIdKey("element", scriptClass->PersistentId)] = scriptClass->ID.id;
        for (const ScriptPropertyPtr& value : scriptClass->properties) property(value);
        for (const ScriptFunctionPtr& method : scriptClass->methods) function(method);
        function(scriptClass->constructor);
    }
}

void ResolveTypeReference(TypeRef& type, const Script& script)
{
    if (type.kind == PinType::Object && type.symbolId.IsValid() && (!type.moduleId.IsValid() || type.moduleId == script.ModuleIdentity))
    {
        const auto scriptClass = std::find_if(script.classes.begin(), script.classes.end(), [&](const ScriptClassPtr& candidate)
            { return candidate && candidate->PersistentId == type.symbolId; });
        if (scriptClass == script.classes.end())
            throw SerializationError("Object type references missing class UUID '" + type.symbolId.ToString() + "'.");
        type.classId = (*scriptClass)->ID.id;
        if (type.name.empty()) type.name = (*scriptClass)->Name;
    }
    for (TypeRef& parameter : type.parameters) ResolveTypeReference(parameter, script);
}

void ResolveDeclarationTypeReferences(Script& script)
{
    const auto property = [&](const ScriptPropertyPtr& value)
    {
        if (value) ResolveTypeReference(value->type, script);
    };
    const auto function = [&](const ScriptFunctionPtr& value)
    {
        if (!value) return;
        for (BasicFunctionDef::Input& input : value->functionDef->inputs) ResolveTypeReference(input.type, script);
        for (BasicFunctionDef::Input& output : value->functionDef->outputs) ResolveTypeReference(output.type, script);
        for (const ScriptPropertyPtr& variable : value->variables) property(variable);
    };
    function(script.main);
    for (const ScriptPropertyPtr& variable : script.variables) property(variable);
    for (const ScriptFunctionPtr& value : script.functions) function(value);
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        for (const ScriptPropertyPtr& value : scriptClass->properties) property(value);
        for (const ScriptFunctionPtr& method : scriptClass->methods) function(method);
        function(scriptClass->constructor);
    }
}

Json SerializeFunction(const ScriptFunction& function, const Script& script)
{
    Json result(Object{});
    result["id"] = function.PersistentId.ToString();
    result["name"] = function.functionDef->name;
    result["description"] = function.functionDef->description;
    result["pure"] =
        HasFlag(function.functionDef->flags, NodeDefinitionFlags::Pure);

    Json genericTypeProperties(Array{});
    for (const GenericTypeProperty& property :
         function.functionDef->genericTypeProperties)
    {
        genericTypeProperties.push_back(
            SerializeGenericTypeProperty(property));
    }
    result["generic_type_properties"] =
        std::move(genericTypeProperties);

    Json inputs(Array{});
    for (const BasicFunctionDef::Input& input : function.functionDef->inputs)
        inputs.push_back(SerializeDefinitionPort(input, script));
    result["inputs"] = std::move(inputs);

    Json outputs(Array{});
    for (const BasicFunctionDef::Input& output : function.functionDef->outputs)
        outputs.push_back(SerializeDefinitionPort(output, script));
    result["outputs"] = std::move(outputs);

    Json variables(Array{});
    for (const ScriptPropertyPtr& variable : function.variables)
        variables.push_back(SerializeProperty(*variable, script));
    result["variables"] = std::move(variables);
    result["graph"] = SerializeGraph(function.Graph, script);
    return result;
}

ScriptFunctionPtr DeserializeFunctionShell(const Json& json, IdSet& ids, Script& script, int formatVersion, IDGenerator* runtimeIds)
{
    const ScriptElementUuid persistentId = formatVersion >= 8 ? RequiredDurableId<ScriptElementUuid>(json, "id") : ScriptElementUuid{};
    const int id = formatVersion >= 8 ? CachedRuntimeId(script, RuntimeIdKey("element", persistentId), "Function", ids, *runtimeIds)
                                      : DeserializeRuntimeId(json, "Function", formatVersion, ids, runtimeIds);
    ScriptFunctionPtr function = std::make_shared<ScriptFunction>(id, StringField(json, "name").c_str());
    CaptureSerializedFields(json, function->SerializedExtensions);
    function->PersistentId = formatVersion >= 8 ? persistentId
        : DeserializePersistentId(json, formatVersion, ScriptElementUuid::FromLegacy(script.ModuleIdentity.Value(), "element:" + std::to_string(id)));
    function->functionDef->scriptId = function->PersistentId;
    function->functionDef->description = OptionalStringField(json, "description");
    if (const Json* pure = OptionalField(json, "pure"))
    {
        if (!pure->is_boolean())
            throw SerializationError("Function purity has the wrong type.");
        if (pure->get<crude_json::boolean>())
            function->functionDef->flags |= NodeDefinitionFlags::Pure;
    }
    if (const Json* properties =
            OptionalField(json, "generic_type_properties"))
    {
        if (!properties->is_array())
            throw SerializationError(
                "Function generic type properties have the wrong type.");
        for (const Json& property : properties->get<Array>())
        {
            function->functionDef->genericTypeProperties.push_back(
                DeserializeGenericTypeProperty(property));
        }
    }

    const Array& inputs = Field(json, "inputs", crude_json::type_t::array).get<Array>();
    for (const Json& input : inputs)
        function->functionDef->inputs.push_back(DeserializeDefinitionPort(input, ids, script, function->PersistentId, "input", formatVersion, runtimeIds));
    const Array& outputs = Field(json, "outputs", crude_json::type_t::array).get<Array>();
    for (const Json& output : outputs)
        function->functionDef->outputs.push_back(DeserializeDefinitionPort(output, ids, script, function->PersistentId, "output", formatVersion, runtimeIds));
    const Array& variables = Field(json, "variables", crude_json::type_t::array).get<Array>();
    for (const Json& variable : variables)
        function->variables.push_back(DeserializeProperty(variable, ids, script, formatVersion, runtimeIds));
    return function;
}

Json SerializeClass(const ScriptClass& scriptClass, const Script& script)
{
    Json result(Object{});
    result["id"] = scriptClass.PersistentId.ToString();
    result["name"] = scriptClass.Name;
    Json properties(Array{});
    for (const ScriptPropertyPtr& property : scriptClass.properties)
        properties.push_back(SerializeProperty(*property, script));
    result["properties"] = std::move(properties);
    Json methods(Array{});
    for (const ScriptFunctionPtr& method : scriptClass.methods)
        methods.push_back(SerializeFunction(*method, script));
    result["methods"] = std::move(methods);
    result["has_constructor"] = scriptClass.constructor != nullptr;
    result["constructor"] = scriptClass.constructor
        ? SerializeFunction(*scriptClass.constructor, script) : Json(Object{});
    return result;
}

ScriptClassPtr DeserializeClassShell(const Json& json, IdSet& ids, Script& script, int formatVersion, IDGenerator* runtimeIds)
{
    const ScriptElementUuid persistentId = formatVersion >= 8 ? RequiredDurableId<ScriptElementUuid>(json, "id") : ScriptElementUuid{};
    const int id = formatVersion >= 8 ? CachedRuntimeId(script, RuntimeIdKey("element", persistentId), "Class", ids, *runtimeIds)
                                      : DeserializeRuntimeId(json, "Class", formatVersion, ids, runtimeIds);
    ScriptClassPtr scriptClass = std::make_shared<ScriptClass>(id, StringField(json, "name").c_str());
    CaptureSerializedFields(json, scriptClass->SerializedExtensions);
    scriptClass->PersistentId = formatVersion >= 8 ? persistentId
        : DeserializePersistentId(json, formatVersion, ScriptElementUuid::FromLegacy(script.ModuleIdentity.Value(), "element:" + std::to_string(id)));
    for (const Json& property : Field(json, "properties", crude_json::type_t::array).get<Array>())
        scriptClass->properties.push_back(DeserializeProperty(property, ids, script, formatVersion, runtimeIds));
    for (const Json& method : Field(json, "methods", crude_json::type_t::array).get<Array>())
        scriptClass->methods.push_back(DeserializeFunctionShell(method, ids, script, formatVersion, runtimeIds));
    if (BoolField(json, "has_constructor"))
        scriptClass->constructor = DeserializeFunctionShell(
            Field(json, "constructor", crude_json::type_t::object), ids, script, formatVersion, runtimeIds);
    return scriptClass;
}

Json SerializeScript(const Script& script)
{
    if (!script.main)
        throw SerializationError("The script has no main function.");
    ValidateDurableIdentities(script);
    Json root(Object{});
    root["format"] = "visual-lox";
    root["format_version"] = static_cast<double>(ScriptSerializer::FormatVersion);
    root["module_id"] = script.ModuleIdentity.ToString();

    Json scriptJson(Object{});
    scriptJson["main"] = SerializeFunction(*script.main, script);
    Json functions(Array{});
    for (const ScriptFunctionPtr& function : script.functions)
        functions.push_back(SerializeFunction(*function, script));
    scriptJson["functions"] = std::move(functions);
    Json variables(Array{});
    for (const ScriptPropertyPtr& variable : script.variables)
        variables.push_back(SerializeProperty(*variable, script));
    scriptJson["variables"] = std::move(variables);
    Json classes(Array{});
    for (const ScriptClassPtr& scriptClass : script.classes)
        classes.push_back(SerializeClass(*scriptClass, script));
    scriptJson["classes"] = std::move(classes);
    root["script"] = std::move(scriptJson);
    return root;
}

using DocumentMigration = void(*)(Json&);

void MigrateV1ToV2(Json& root)
{
    Json& script = root["script"];
    if (script.contains("classes") && (!script["classes"].is_array() || !script["classes"].get<Array>().empty()))
        throw SerializationError("Version 1 documents cannot contain classes.");
    script["classes"] = Json(Array{});
}

void MigrateV2ToV3(Json&) {}
void MigrateV3ToV4(Json&) {}
void MigrateV4ToV5(Json&) {}
void MigrateV5ToV6(Json&) {}
void MigrateV6ToV7(Json&) {}

std::vector<SerializationDiagnostic> MigrateDocumentToV7(Json& root)
{
    static constexpr int legacyFormatVersion = 7;
    if (StringField(root, "format") != "visual-lox") throw SerializationError("This is not a Visual Lox document.");
    const int originalVersion = IntField(root, "format_version");
    if (originalVersion < 1 || originalVersion > ScriptSerializer::FormatVersion)
        throw SerializationError("Unsupported .vlox format version " + std::to_string(originalVersion) + ".");
    if (originalVersion == ScriptSerializer::FormatVersion) return {};

    static constexpr DocumentMigration migrations[] = {
        MigrateV1ToV2, MigrateV2ToV3, MigrateV3ToV4, MigrateV4ToV5, MigrateV5ToV6, MigrateV6ToV7,
    };
    int version = originalVersion;
    while (version < legacyFormatVersion)
    {
        migrations[version - 1](root);
        ++version;
        root["format_version"] = static_cast<double>(version);
    }

    if (originalVersion == legacyFormatVersion)
        return { { SerializationDiagnosticSeverity::Information, "document.resave_required", "$.format_version", {},
            "The legacy version 7 document loads through the compatibility decoder; saving writes version 8." } };
    return { { SerializationDiagnosticSeverity::Information, "document.migrated", "$.format_version", {},
        "Migrated legacy document format " + std::to_string(originalVersion) + " to the version 7 compatibility model; saving writes version 8." } };
}

void DeserializeScript(const Json& root, const NodeRegistry& registry, Script& script, int& nextId, std::vector<SerializationDiagnostic>& diagnostics)
{
    if (StringField(root, "format") != "visual-lox")
        throw SerializationError("This is not a Visual Lox document.");
    const int version = IntField(root, "format_version");
    if (version < 1 || version > ScriptSerializer::FormatVersion)
        throw SerializationError("Unsupported .vlox format version " + std::to_string(version) + ".");

    const Json& scriptJson = Field(root, "script", crude_json::type_t::object);
    IdSet ids;
    IDGenerator constructionIds;
    if (version >= 8)
    {
        int maximumCachedId = 0;
        for (const auto& [key, id] : script.RuntimeIdCache) maximumCachedId = (std::max)(maximumCachedId, id);
        if (maximumCachedId == std::numeric_limits<int>::max()) throw SerializationError("The runtime ID cache has exhausted the ID range.");
        constructionIds.Reset(maximumCachedId + 1);
    }
    CaptureSerializedFields(root, script.DocumentSerializedExtensions);
    CaptureSerializedFields(scriptJson, script.SerializedExtensions);
    script.ModuleIdentity = version >= 8 ? RequiredDurableId<ModuleId>(root, "module_id") : OptionalDurableId(root, "module_id", ModuleId::New());
    if (version >= 8) script.ID = CachedRuntimeId(script, "script", "Script", ids, constructionIds);
    else
    {
        script.ID = IntField(scriptJson, "id");
        ids.Add(script.ID, "Script");
    }

    const Array& classes = Field(scriptJson, "classes", crude_json::type_t::array).get<Array>();
    if (version == 1 && !classes.empty())
        throw SerializationError("Version 1 documents cannot contain classes.");

    const Array& variables = Field(scriptJson, "variables", crude_json::type_t::array).get<Array>();
    for (const Json& variable : variables)
        script.variables.push_back(DeserializeProperty(variable, ids, script, version, &constructionIds));

    const Json& mainJson = Field(scriptJson, "main", crude_json::type_t::object);
    script.main = DeserializeFunctionShell(mainJson, ids, script, version, &constructionIds);

    const Array& functionJsons = Field(scriptJson, "functions", crude_json::type_t::array).get<Array>();
    for (const Json& functionJson : functionJsons)
        script.functions.push_back(DeserializeFunctionShell(functionJson, ids, script, version, &constructionIds));
    if (version >= 2)
        for (const Json& classJson : classes)
            script.classes.push_back(DeserializeClassShell(classJson, ids, script, version, &constructionIds));

    ResolveDeclarationTypeReferences(script);
    if (version < 8)
    {
        const int maximumSerializedId = MaximumSerializedId(root);
        if (maximumSerializedId == std::numeric_limits<int>::max())
            throw SerializationError("The document has exhausted the ID range.");
        constructionIds.Reset(maximumSerializedId + 1);
    }
    DeserializeGraph(Field(mainJson, "graph", crude_json::type_t::object), registry, script,
                     script.main, script.main->Graph, ids, constructionIds, diagnostics, "$.script.main.graph", version);
    for (size_t i = 0; i < functionJsons.size(); ++i)
    {
        DeserializeGraph(Field(functionJsons[i], "graph", crude_json::type_t::object), registry, script,
                         script.functions[i], script.functions[i]->Graph, ids, constructionIds, diagnostics,
                         "$.script.functions[" + std::to_string(i) + "].graph", version);
    }
    for (size_t classIndex = 0; classIndex < classes.size(); ++classIndex)
    {
        const Json& classJson = classes[classIndex];
        ScriptClassPtr scriptClass = script.classes[classIndex];
        const Array& methods = Field(classJson, "methods", crude_json::type_t::array).get<Array>();
        for (size_t methodIndex = 0; methodIndex < methods.size(); ++methodIndex)
            DeserializeGraph(Field(methods[methodIndex], "graph", crude_json::type_t::object),
                registry, script, scriptClass->methods[methodIndex],
                scriptClass->methods[methodIndex]->Graph, ids, constructionIds, diagnostics,
                "$.script.classes[" + std::to_string(classIndex) + "].methods[" + std::to_string(methodIndex) + "].graph", version);
        if (scriptClass->constructor)
        {
            const Json& constructor = Field(classJson, "constructor", crude_json::type_t::object);
            DeserializeGraph(Field(constructor, "graph", crude_json::type_t::object), registry,
                script, scriptClass->constructor, scriptClass->constructor->Graph, ids, constructionIds, diagnostics,
                "$.script.classes[" + std::to_string(classIndex) + "].constructor.graph", version);
        }
    }

    ValidateDurableIdentities(script);
    nextId = (std::max)(ids.Next(), constructionIds.PeekNextId());
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
        auto loaded = Json::load(path);
        if (!loaded.second || loaded.first.is_discarded())
            return SerializationResult::Fail("Could not read valid JSON from '" + path + "'.");

        GarbageCollectionPause pause;
        Script staged;
        if (const Json* module = OptionalField(loaded.first, "module_id"); module && module->is_string() && outputScript.main &&
            outputScript.ModuleIdentity == ModuleId::Parse(module->get<crude_json::string>()))
        {
            CaptureRuntimeIdCache(outputScript);
            staged.RuntimeIdCache = outputScript.RuntimeIdCache;
        }
        int nextId = 1;
        std::vector<SerializationDiagnostic> diagnostics = MigrateDocumentToV7(loaded.first);
        DeserializeScript(loaded.first, registry, staged, nextId, diagnostics);
        outputScript = std::move(staged);
        idGenerator.Reset(nextId);
        return SerializationResult::Ok(std::move(diagnostics));
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
        Json document = Json::parse(data);
        if (document.is_discarded())
            return SerializationResult::Fail("Could not parse a Visual Lox document from memory.");

        GarbageCollectionPause pause;
        Script staged;
        if (const Json* module = OptionalField(document, "module_id"); module && module->is_string() && outputScript.main &&
            outputScript.ModuleIdentity == ModuleId::Parse(module->get<crude_json::string>()))
        {
            CaptureRuntimeIdCache(outputScript);
            staged.RuntimeIdCache = outputScript.RuntimeIdCache;
        }
        int nextId = 1;
        std::vector<SerializationDiagnostic> diagnostics = MigrateDocumentToV7(document);
        DeserializeScript(document, registry, staged, nextId, diagnostics);
        outputScript = std::move(staged);
        idGenerator.Reset(nextId);
        return SerializationResult::Ok(std::move(diagnostics));
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what());
    }
}

SerializationResult ScriptSerializer::MigrateToCurrentFormat(const std::string& data, std::string& output)
{
    try
    {
        Json document = Json::parse(data);
        if (document.is_discarded())
            return SerializationResult::Fail("Could not parse a Visual Lox document from memory.", "document.invalid_json", "$");

        std::vector<SerializationDiagnostic> diagnostics = MigrateDocumentToV7(document);
        output = document.dump(2);
        return SerializationResult::Ok(std::move(diagnostics));
    }
    catch (const std::exception& exception)
    {
        return SerializationResult::Fail(exception.what(), "document.migration_failed", "$");
    }
}

SerializationResult ScriptSerializer::MigrateToCurrentFormat(const std::string& data, const NodeRegistry& registry, std::string& output)
{
    Script script;
    IDGenerator ids;
    SerializationResult result = DeserializeFromString(data, registry, script, ids);
    if (!result) return result;
    SerializationResult serialized = SerializeToString(script, output);
    if (!serialized) return serialized;
    serialized.diagnostics = std::move(result.diagnostics);
    return serialized;
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

bool ReadNodeStateLocation(const std::string& state, double& x, double& y)
{
    if (state.empty())
        return false;
    Json parsed = Json::parse(state);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("location"))
        return false;
    Json& location = parsed["location"];
    if (location.is_array() && location.get<Array>().size() >= 2 &&
        location[0].is_number() && location[1].is_number())
    {
        x = location[0].get<crude_json::number>();
        y = location[1].get<crude_json::number>();
        return true;
    }
    if (location.is_object() && location.contains("x") && location.contains("y") &&
        location["x"].is_number() && location["y"].is_number())
    {
        x = location["x"].get<crude_json::number>();
        y = location["y"].get<crude_json::number>();
        return true;
    }
    return false;
}

std::string OffsetNodeState(const std::string& state, double offsetX, double offsetY)
{
    if (offsetX == 0.0 && offsetY == 0.0)
        return state;
    Json parsed = state.empty() ? Json(Object()) : Json::parse(state);
    if (parsed.is_discarded() || !parsed.is_object())
        return state;

    double x = 0.0;
    double y = 0.0;
    const bool hasLocation = ReadNodeStateLocation(state, x, y);
    if (hasLocation && parsed["location"].is_array())
    {
        parsed["location"][0] = x + offsetX;
        parsed["location"][1] = y + offsetY;
    }
    else
    {
        parsed["location"]["x"] = x + offsetX;
        parsed["location"]["y"] = y + offsetY;
    }
    return parsed.dump();
}

NodePtr CloneNode(const NodePtr& sourceNode, const NodeRegistry& registry,
                  Script& destination, const ScriptFunctionPtr& owner,
                  IDGenerator& ids, const std::map<int, int>& referenceMap,
                  std::map<int, int>& pinMap, double positionOffsetX,
                  double positionOffsetY)
{
    Json definition(Object{});
    definition["kind"] = sourceNode->SerializationType;
    definition["definition"] = sourceNode->DefinitionId;
    definition["definition_revision"] = static_cast<double>(sourceNode->DefinitionRevision);
    definition["display_name"] = sourceNode->Name;
    definition["reference_id"] = static_cast<double>(sourceNode->refId.id);
    if (sourceNode->Type == NodeType::CommentBox)
    {
        definition["text"] = sourceNode->Name;
        definition["comment_box_color"] = CommentBoxColorName(static_cast<const CommentBoxNode&>(*sourceNode).BoxColor);
    }
    if (sourceNode->refPersistentId.IsValid())
    {
        Json target(Object{});
        target["module"] = (sourceNode->refModuleId.IsValid() ? sourceNode->refModuleId : destination.ModuleIdentity).ToString();
        target["symbol"] = sourceNode->refPersistentId.ToString();
        definition["target"] = std::move(target);
    }
    const auto reference = referenceMap.find(sourceNode->refId.id);
    if (reference != referenceMap.end())
    {
        definition["reference_id"] = static_cast<double>(reference->second);
        definition.get<Object>().erase("target");
    }

    IDGenerator constructionIds;
    NodePtr clone = CreateNode(definition, registry, destination, owner, constructionIds);
    clone->ID = ed::NodeId(ids.GetNextId());
    clone->State = OffsetNodeState(sourceNode->State, positionOffsetX, positionOffsetY);
    clone->Description = sourceNode->Description;
    clone->TypeOverrides = sourceNode->TypeOverrides;
    clone->SerializedExtensions = sourceNode->SerializedExtensions;
    clone->Inputs.clear();
    clone->Outputs.clear();
    clone->UnresolvedInputs.clear();
    clone->UnresolvedInputValues.clear();
    clone->UnresolvedOutputs.clear();
    clone->InputValues.clear();

    std::map<DynamicSlotId, DynamicSlotId> dynamicSlots;
    const auto clonedIdentity = [&](const PortIdentity& identity)
    {
        if (identity.kind != PortIdentityKind::Dynamic) return identity;
        auto [slot, inserted] = dynamicSlots.emplace(identity.dynamicSlot, DynamicSlotId::New());
        PortIdentity result = identity;
        result.dynamicSlot = slot->second;
        return result;
    };

    for (size_t i = 0; i < sourceNode->Inputs.size(); ++i)
    {
        const Pin& sourcePin = sourceNode->Inputs[i];
        const int newId = ids.GetNextId();
        pinMap[sourcePin.ID.Get()] = newId;
        clone->Inputs.emplace_back(newId, sourcePin.Name.c_str(),
            sourcePin.DeclaredType, sourcePin.Description);
        clone->Inputs.back().Identity = clonedIdentity(sourcePin.Identity);
        clone->Inputs.back().SerializedExtensions = sourcePin.SerializedExtensions;
        clone->InputValues.push_back(CloneValue(sourceNode->InputValues[i]));
    }
    for (size_t i = 0; i < sourceNode->UnresolvedInputs.size(); ++i)
    {
        const Pin& sourcePin = sourceNode->UnresolvedInputs[i];
        const int newId = ids.GetNextId();
        pinMap[static_cast<int>(sourcePin.ID.Get())] = newId;
        clone->UnresolvedInputs.emplace_back(newId, sourcePin.Name.c_str(), sourcePin.DeclaredType, sourcePin.Description);
        clone->UnresolvedInputs.back().Identity = clonedIdentity(sourcePin.Identity);
        clone->UnresolvedInputs.back().SerializedExtensions = sourcePin.SerializedExtensions;
        clone->UnresolvedInputValues.push_back(CloneValue(sourceNode->UnresolvedInputValues[i]));
    }
    for (const Pin& sourcePin : sourceNode->Outputs)
    {
        const int newId = ids.GetNextId();
        pinMap[sourcePin.ID.Get()] = newId;
        clone->Outputs.emplace_back(newId, sourcePin.Name.c_str(),
            sourcePin.DeclaredType, sourcePin.Description);
        clone->Outputs.back().Identity = clonedIdentity(sourcePin.Identity);
        clone->Outputs.back().SerializedExtensions = sourcePin.SerializedExtensions;
    }
    for (const Pin& sourcePin : sourceNode->UnresolvedOutputs)
    {
        const int newId = ids.GetNextId();
        pinMap[static_cast<int>(sourcePin.ID.Get())] = newId;
        clone->UnresolvedOutputs.emplace_back(newId, sourcePin.Name.c_str(), sourcePin.DeclaredType, sourcePin.Description);
        clone->UnresolvedOutputs.back().Identity = clonedIdentity(sourcePin.Identity);
        clone->UnresolvedOutputs.back().SerializedExtensions = sourcePin.SerializedExtensions;
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
        link.SerializedExtensions = sourceLink.SerializedExtensions;
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
                                                   std::vector<int>& pastedNodeIds,
                                                   std::optional<std::pair<double, double>> pastePosition)
{
    try
    {
        GarbageCollectionPause pause;
        ScriptFunctionPtr sourceFunction = FindAnyFunction(source, sourceFunctionId);
        ScriptFunctionPtr destinationFunction = FindAnyFunction(destination, destinationFunctionId);
        if (!sourceFunction || !destinationFunction)
            return SerializationResult::Fail("The source or destination function no longer exists.");

        std::set<int> selected(nodeIds.begin(), nodeIds.end());
        if (sourceFunctionId != destinationFunctionId)
        {
            for (const NodePtr& node : sourceFunction->Graph.GetNodes())
            {
                if (selected.count(node->ID.Get()) == 0)
                    continue;
                if ((node->SerializationType == "variable.get" || node->SerializationType == "variable.set") &&
                    ScriptUtils::FindFunctionVariableById(sourceFunction, node->refId.id))
                    return SerializationResult::Fail("Nodes that reference local variables can only be pasted into their owning function.");
            }
        }

        std::map<int, int> pinMap;
        std::map<int, int> referenceMap;
        double positionOffsetX = 30.0;
        double positionOffsetY = 30.0;
        if (pastePosition)
        {
            struct PositionedNode
            {
                double x;
                double y;
            };
            std::vector<PositionedNode> positions;
            for (const NodePtr& node : sourceFunction->Graph.GetNodes())
            {
                if (selected.find(node->ID.Get()) == selected.end() ||
                    HasFlag(node->DefinitionFlags, NodeDefinitionFlags::Protected))
                    continue;
                double x = 0.0;
                double y = 0.0;
                ReadNodeStateLocation(node->State, x, y);
                positions.push_back({ x, y });
            }
            if (!positions.empty())
            {
                const double minX = std::min_element(positions.begin(), positions.end(),
                    [](const PositionedNode& left, const PositionedNode& right) { return left.x < right.x; })->x;
                const double minY = std::min_element(positions.begin(), positions.end(),
                    [](const PositionedNode& left, const PositionedNode& right) { return left.y < right.y; })->y;
                const PositionedNode& reference = *std::min_element(positions.begin(), positions.end(),
                    [=](const PositionedNode& left, const PositionedNode& right)
                    {
                        const double leftDistance = (left.x - minX) * (left.x - minX) + (left.y - minY) * (left.y - minY);
                        const double rightDistance = (right.x - minX) * (right.x - minX) + (right.y - minY) * (right.y - minY);
                        return leftDistance < rightDistance;
                    });
                positionOffsetX = pastePosition->first - reference.x;
                positionOffsetY = pastePosition->second - reference.y;
            }
        }
        pastedNodeIds.clear();
        for (const NodePtr& node : sourceFunction->Graph.GetNodes())
        {
            if (selected.find(node->ID.Get()) == selected.end() ||
                HasFlag(node->DefinitionFlags, NodeDefinitionFlags::Protected))
                continue;
            NodePtr clone = CloneNode(node, registry, destination, destinationFunction,
                                      ids, referenceMap, pinMap,
                                      positionOffsetX, positionOffsetY);
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
        clone->SerializedExtensions = sourceFunction->SerializedExtensions;
        clone->functionDef->description = sourceFunction->functionDef->description;
        clone->functionDef->flags = sourceFunction->functionDef->flags;
        clone->functionDef->genericTypeProperties =
            sourceFunction->functionDef->genericTypeProperties;
        for (const BasicFunctionDef::Input& input : sourceFunction->functionDef->inputs)
        {
            const int newId = ids.GetNextId();
            referenceMap[input.id] = newId;
            BasicFunctionDef::Input inputClone{ input.name, CloneValue(input.value), newId, input.type, input.description };
            inputClone.serializedExtensions = input.serializedExtensions;
            clone->functionDef->inputs.push_back(std::move(inputClone));
        }
        for (const BasicFunctionDef::Input& output : sourceFunction->functionDef->outputs)
        {
            const int newId = ids.GetNextId();
            referenceMap[output.id] = newId;
            BasicFunctionDef::Input outputClone{ output.name, CloneValue(output.value), newId, output.type, output.description };
            outputClone.serializedExtensions = output.serializedExtensions;
            clone->functionDef->outputs.push_back(std::move(outputClone));
        }
        for (const ScriptPropertyPtr& variable : sourceFunction->variables)
        {
            const int newId = ids.GetNextId();
            referenceMap[variable->ID.id] = newId;
            ScriptPropertyPtr variableClone = std::make_shared<ScriptProperty>(newId, variable->Name.c_str());
            variableClone->SerializedExtensions = variable->SerializedExtensions;
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
                                          referenceMap, pinMap, 0.0, 0.0);
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
        clone->SerializedExtensions = variable->SerializedExtensions;
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

SerializationResult ScriptSerializer::CloneFunctionVariable(const Script& source, int sourceFunctionId,
                                                              int variableId, Script& destination,
                                                              int destinationFunctionId, IDGenerator& ids,
                                                              int& pastedVariableId)
{
    ScriptFunctionPtr sourceFunction = FindAnyFunction(source, sourceFunctionId);
    ScriptFunctionPtr destinationFunction = FindAnyFunction(destination, destinationFunctionId);
    if (!sourceFunction || !destinationFunction)
        return SerializationResult::Fail("The source or destination function no longer exists.");
    ScriptPropertyPtr variable = ScriptUtils::FindFunctionVariableById(sourceFunction, variableId);
    if (!variable)
        return SerializationResult::Fail("The copied local variable no longer exists.");
    try
    {
        GarbageCollectionPause pause;
        pastedVariableId = ids.GetNextId();
        ScriptPropertyPtr clone = std::make_shared<ScriptProperty>(pastedVariableId, variable->Name.c_str());
        clone->SerializedExtensions = variable->SerializedExtensions;
        clone->Description = variable->Description;
        clone->type = variable->type;
        clone->defaultValue = CloneValue(variable->defaultValue);
        destinationFunction->variables.push_back(clone);
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
        clone.serializedExtensions = sourcePort->serializedExtensions;
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
