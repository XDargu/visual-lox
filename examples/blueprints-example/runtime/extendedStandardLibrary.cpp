#include "extendedStandardLibrary.h"
#include "standardLibrary.h"

#include "../graphs/nodeRegistry.h"
#include "scriptRuntime.h"

#include <Object.h>
#include <VMUtils.h>
#include <Vm.h>
#include <crude_json.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <codecvt>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace
{
using Clock = std::chrono::steady_clock;

Value StringValue(std::string value)
{
    return Value(takeString(std::move(value)));
}

ObjList* BeginList(VM* vm)
{
    ObjList* list = newList();
    vm->push(Value(list));
    return list;
}

Value EndList(VM* vm, ObjList* list)
{
    vm->pop();
    return Value(list);
}

Value StatusResult(VM* vm, bool success, std::string error = {})
{
    ObjList* result = BeginList(vm);
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

bool IsInteger(const Value& value)
{
    return isNumber(value) && std::isfinite(asNumber(value)) && std::floor(asNumber(value)) == asNumber(value);
}

int ClampedInt(const Value& value, int fallback = 0)
{
    if (!isNumber(value) || !std::isfinite(asNumber(value)))
        return fallback;
    const double number = std::clamp(asNumber(value), static_cast<double>(std::numeric_limits<int>::min()), static_cast<double>(std::numeric_limits<int>::max()));
    return static_cast<int>(number);
}

bool StringList(const Value& value, std::vector<std::string>& result, std::string& error)
{
    if (!isList(value))
    {
        error = "Arguments must be a list of strings.";
        return false;
    }
    for (const Value& item : asList(value)->items)
    {
        if (!isString(item))
        {
            error = "Every argument must be a string.";
            return false;
        }
        result.push_back(asString(item)->chars);
    }
    return true;
}

std::mt19937_64& RandomGenerator()
{
    static std::mt19937_64 generator(std::random_device{}());
    return generator;
}

std::mutex& RandomMutex()
{
    static std::mutex mutex;
    return mutex;
}

bool DecodeUtf8(const std::string& text, std::vector<uint32_t>& codepoints)
{
    codepoints.clear();
    for (size_t index = 0; index < text.size();)
    {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        uint32_t codepoint = 0;
        size_t length = 0;
        if (first <= 0x7f) { codepoint = first; length = 1; }
        else if ((first & 0xe0) == 0xc0) { codepoint = first & 0x1f; length = 2; }
        else if ((first & 0xf0) == 0xe0) { codepoint = first & 0x0f; length = 3; }
        else if ((first & 0xf8) == 0xf0) { codepoint = first & 0x07; length = 4; }
        else return false;
        if (index + length > text.size())
            return false;
        for (size_t offset = 1; offset < length; ++offset)
        {
            const unsigned char next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
        codepoints.push_back(codepoint);
        index += length;
    }
    return true;
}

std::string EncodeUtf8(const std::vector<uint32_t>& codepoints)
{
    std::string result;
    for (uint32_t codepoint : codepoints)
    {
        if (codepoint <= 0x7f)
            result.push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7ff)
        {
            result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
        else if (codepoint <= 0xffff)
        {
            result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
        else
        {
            result.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }
    return result;
}

std::string LowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

bool ConvertTextForEncoding(const std::string& text, std::string encoding, bool writing, std::string& converted, std::string& error)
{
    encoding = LowerAscii(std::move(encoding));
    if (encoding == "utf-8" || encoding == "utf8")
    {
        std::vector<uint32_t> codepoints;
        if (!DecodeUtf8(text, codepoints))
        {
            error = "Text is not valid UTF-8.";
            return false;
        }
        converted = text;
        if (!writing && converted.size() >= 3 && converted.compare(0, 3, "\xef\xbb\xbf") == 0)
            converted.erase(0, 3);
        return true;
    }
    if (encoding == "utf-8-bom" || encoding == "utf8-bom")
    {
        std::vector<uint32_t> codepoints;
        std::string content = text;
        if (!writing && content.size() >= 3 && content.compare(0, 3, "\xef\xbb\xbf") == 0)
            content.erase(0, 3);
        if (!DecodeUtf8(content, codepoints))
        {
            error = "Text is not valid UTF-8.";
            return false;
        }
        converted = writing ? "\xef\xbb\xbf" + content : content;
        return true;
    }
    if (encoding == "ascii")
    {
        if (std::any_of(text.begin(), text.end(), [](unsigned char character) { return character > 0x7f; }))
        {
            error = "ASCII text cannot contain bytes above 127.";
            return false;
        }
        converted = text;
        return true;
    }
    error = "Unsupported encoding. Use utf-8, utf-8-bom, or ascii.";
    return false;
}

constexpr const char* JsonValueClassName = "JsonValue";
constexpr const char* JsonValuePayloadField = "__json_value";

bool JsonFromNativeValue(const Value& source, crude_json::value& destination, std::unordered_set<const Obj*>& active, std::string& error)
{
    if (isNil(source)) { destination = nullptr; return true; }
    if (isBoolean(source)) { destination = asBoolean(source); return true; }
    if (isNumber(source))
    {
        if (!std::isfinite(asNumber(source))) { error = "JSON numbers must be finite."; return false; }
        destination = asNumber(source);
        return true;
    }
    if (isString(source)) { destination = asString(source)->chars; return true; }
    if (!isList(source) && !isMap(source))
    {
        error = "Only nil, booleans, numbers, strings, lists, and maps can be stringified as JSON.";
        return false;
    }
    const Obj* object = asObject(source);
    if (!active.insert(object).second)
    {
        error = "Cyclic lists and maps cannot be stringified as JSON.";
        return false;
    }
    if (isList(source))
    {
        crude_json::array values;
        values.reserve(asList(source)->items.size());
        for (const Value& item : asList(source)->items)
        {
            crude_json::value converted;
            if (!JsonFromNativeValue(item, converted, active, error)) { active.erase(object); return false; }
            values.push_back(std::move(converted));
        }
        destination = std::move(values);
    }
    else
    {
        crude_json::object values;
        for (const MapEntry& entry : asMap(source)->entries)
        {
            if (!entry.active)
                continue;
            if (!isString(entry.key)) { error = "JSON object keys must be strings."; active.erase(object); return false; }
            crude_json::value converted;
            if (!JsonFromNativeValue(entry.value, converted, active, error)) { active.erase(object); return false; }
            values[asString(entry.key)->chars] = std::move(converted);
        }
        destination = std::move(values);
    }
    active.erase(object);
    return true;
}

Value NativeValueFromJson(const crude_json::value& source, VM* vm)
{
    using crude_json::type_t;
    switch (source.type())
    {
    case type_t::null: return Value();
    case type_t::boolean: return Value(source.get<crude_json::boolean>());
    case type_t::number: return Value(source.get<crude_json::number>());
    case type_t::string: return StringValue(source.get<crude_json::string>());
    case type_t::array:
    {
        ObjList* list = BeginList(vm);
        for (const crude_json::value& item : source.get<crude_json::array>())
            list->append(NativeValueFromJson(item, vm));
        return EndList(vm, list);
    }
    case type_t::object:
    {
        ObjMap* map = newMap();
        vm->push(Value(map));
        for (const auto& entry : source.get<crude_json::object>())
        {
            Value value = NativeValueFromJson(entry.second, vm);
            vm->push(value);
            Value key = StringValue(entry.first);
            map->set(key, value);
            vm->pop();
        }
        vm->pop();
        return Value(map);
    }
    default: return Value();
    }
}

bool GetJsonValuePayload(const Value& source, VM* vm, Value& payload)
{
    Value classValue;
    if (!isInstance(source) || !vm->globalTable().get(copyString(JsonValueClassName, static_cast<int>(std::strlen(JsonValueClassName))), &classValue) ||
        !isClass(classValue) || asInstance(source)->klass != asClass(classValue))
        return false;
    return asInstance(source)->fields.get(copyString(JsonValuePayloadField, static_cast<int>(std::strlen(JsonValuePayloadField))), &payload);
}

Value WrapJsonValue(const Value& payload, VM* vm)
{
    vm->push(payload);
    Value classValue;
    if (!vm->globalTable().get(copyString(JsonValueClassName, static_cast<int>(std::strlen(JsonValueClassName))), &classValue) || !isClass(classValue))
    {
        vm->pop();
        return Value();
    }

    ObjInstance* instance = newInstance(asClass(classValue));
    vm->push(Value(instance));
    instance->fields.set(copyString(JsonValuePayloadField, static_cast<int>(std::strlen(JsonValuePayloadField))), payload);
    vm->pop();
    vm->pop();
    return Value(instance);
}

Value JsonValueFromJson(const crude_json::value& source, VM* vm)
{
    return WrapJsonValue(NativeValueFromJson(source, vm), vm);
}

const char* JsonKindName(const Value& payload)
{
    if (isNil(payload)) return "Null";
    if (isBoolean(payload)) return "Boolean";
    if (isNumber(payload)) return "Number";
    if (isString(payload)) return "String";
    if (isList(payload)) return "Array";
    if (isMap(payload)) return "Object";
    return "Invalid";
}

bool JsonFromWrappedValue(const Value& source, VM* vm, crude_json::value& destination, std::string& error)
{
    Value payload;
    if (!GetJsonValuePayload(source, vm, payload))
    {
        error = "Value must be a JsonValue.";
        return false;
    }
    std::unordered_set<const Obj*> active;
    return JsonFromNativeValue(payload, destination, active, error);
}

Value JsonValueResult(VM* vm, const Value& value, bool success, std::string error = {})
{
    vm->push(value);
    ObjList* result = BeginList(vm);
    result->append(value);
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    Value packaged = EndList(vm, result);
    vm->pop();
    return packaged;
}

Value JsonValueInit(int, Value* args, VM*)
{
    ObjInstance* instance = asInstance(args[0]);
    instance->fields.set(copyString(JsonValuePayloadField, static_cast<int>(std::strlen(JsonValuePayloadField))), Value());
    return args[0];
}

Value JsonValueKindMethod(int, Value* args, VM* vm)
{
    Value payload;
    return StringValue(GetJsonValuePayload(args[0], vm, payload) ? JsonKindName(payload) : "Invalid");
}

Value JsonValueIsNullMethod(int, Value* args, VM* vm)
{
    Value payload;
    return Value(GetJsonValuePayload(args[0], vm, payload) && isNil(payload));
}

Value JsonValueToStringMethod(int, Value* args, VM* vm)
{
    crude_json::value json;
    std::string error;
    return JsonFromWrappedValue(args[0], vm, json, error) ? StringValue(json.dump()) : StringValue("<invalid JsonValue>");
}

Value JsonValueToNativeMethod(int, Value* args, VM* vm)
{
    crude_json::value json;
    std::string error;
    return JsonFromWrappedValue(args[0], vm, json, error) ? NativeValueFromJson(json, vm) : Value();
}

Value JsonParse(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    Value value;
    bool success = false;
    std::string error;
    if (!isString(args[0]))
        error = "JSON text must be a string.";
    else
    {
        const crude_json::value parsed = crude_json::value::parse(asString(args[0])->chars);
        if (parsed.is_discarded())
            error = "Invalid JSON.";
        else
        {
            value = JsonValueFromJson(parsed, vm);
            success = true;
        }
    }
    result->append(value);
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value JsonStringifyImpl(Value* args, VM* vm, bool pretty)
{
    ObjList* result = BeginList(vm);
    std::string text;
    std::string error;
    crude_json::value json;
    const bool success = JsonFromWrappedValue(args[0], vm, json, error);
    if (success)
    {
        const int indent = pretty ? std::clamp(ClampedInt(args[1], 2), 0, 16) : -1;
        text = json.dump(indent);
    }
    result->append(StringValue(std::move(text)));
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value JsonStringify(int, Value* args, VM* vm) { return JsonStringifyImpl(args, vm, false); }
Value JsonPrettyPrint(int, Value* args, VM* vm) { return JsonStringifyImpl(args, vm, true); }

Value JsonKind(int, Value* args, VM* vm) { return JsonValueKindMethod(0, args, vm); }
Value JsonIsNull(int, Value* args, VM* vm) { return JsonValueIsNullMethod(0, args, vm); }

Value JsonFromNative(int, Value* args, VM* vm)
{
    crude_json::value json;
    std::unordered_set<const Obj*> active;
    std::string error;
    if (!JsonFromNativeValue(args[0], json, active, error))
        return JsonValueResult(vm, Value(), false, std::move(error));
    return JsonValueResult(vm, JsonValueFromJson(json, vm), true);
}

Value JsonToNative(int, Value* args, VM* vm)
{
    crude_json::value json;
    std::string error;
    if (!JsonFromWrappedValue(args[0], vm, json, error))
        return JsonValueResult(vm, Value(), false, std::move(error));
    return JsonValueResult(vm, NativeValueFromJson(json, vm), true);
}

Value JsonAsString(int, Value* args, VM* vm)
{
    Value payload;
    if (!GetJsonValuePayload(args[0], vm, payload)) return JsonValueResult(vm, StringValue(""), false, "Value must be a JsonValue.");
    if (!isString(payload)) return JsonValueResult(vm, StringValue(""), false, std::string("JSON value is ") + JsonKindName(payload) + ", not String.");
    return JsonValueResult(vm, payload, true);
}

Value JsonAsNumber(int, Value* args, VM* vm)
{
    Value payload;
    if (!GetJsonValuePayload(args[0], vm, payload)) return JsonValueResult(vm, Value(0.0), false, "Value must be a JsonValue.");
    if (!isNumber(payload)) return JsonValueResult(vm, Value(0.0), false, std::string("JSON value is ") + JsonKindName(payload) + ", not Number.");
    return JsonValueResult(vm, payload, true);
}

Value JsonAsBoolean(int, Value* args, VM* vm)
{
    Value payload;
    if (!GetJsonValuePayload(args[0], vm, payload)) return JsonValueResult(vm, Value(false), false, "Value must be a JsonValue.");
    if (!isBoolean(payload)) return JsonValueResult(vm, Value(false), false, std::string("JSON value is ") + JsonKindName(payload) + ", not Boolean.");
    return JsonValueResult(vm, payload, true);
}

Value JsonAsArray(int, Value* args, VM* vm)
{
    Value payload;
    if (!GetJsonValuePayload(args[0], vm, payload)) return JsonValueResult(vm, Value(newList()), false, "Value must be a JsonValue.");
    if (!isList(payload)) return JsonValueResult(vm, Value(newList()), false, std::string("JSON value is ") + JsonKindName(payload) + ", not Array.");
    ObjList* values = BeginList(vm);
    for (const Value& item : asList(payload)->items)
        values->append(WrapJsonValue(item, vm));
    Value result = EndList(vm, values);
    return JsonValueResult(vm, result, true);
}

Value JsonAsObject(int, Value* args, VM* vm)
{
    Value payload;
    if (!GetJsonValuePayload(args[0], vm, payload)) return JsonValueResult(vm, Value(newMap()), false, "Value must be a JsonValue.");
    if (!isMap(payload)) return JsonValueResult(vm, Value(newMap()), false, std::string("JSON value is ") + JsonKindName(payload) + ", not Object.");
    ObjMap* values = newMap();
    vm->push(Value(values));
    for (const MapEntry& entry : asMap(payload)->entries)
    {
        if (!entry.active) continue;
        Value wrapped = WrapJsonValue(entry.value, vm);
        vm->push(wrapped);
        values->set(entry.key, wrapped);
        vm->pop();
    }
    vm->pop();
    return JsonValueResult(vm, Value(values), true);
}

Value JsonGet(int, Value* args, VM* vm)
{
    Value payload;
    if (!GetJsonValuePayload(args[0], vm, payload)) return JsonValueResult(vm, Value(), false, "Value must be a JsonValue.");
    if (!isString(args[1])) return JsonValueResult(vm, Value(), false, "Key must be a string.");
    if (!isMap(payload)) return JsonValueResult(vm, Value(), false, std::string("JSON value is ") + JsonKindName(payload) + ", not Object.");
    Value value;
    if (!asMap(payload)->get(args[1], &value)) return JsonValueResult(vm, Value(), false);
    return JsonValueResult(vm, WrapJsonValue(value, vm), true);
}

Value JsonObjectToEntries(int, Value* args, VM* vm)
{
    Value payload;
    if (!GetJsonValuePayload(args[0], vm, payload) || !isMap(payload))
        return Value();
    ObjList* result = BeginList(vm);
    for (const MapEntry& entry : asMap(payload)->entries)
    {
        if (!entry.active)
            continue;
        ObjList* pair = newList();
        pair->append(entry.key);
        result->append(Value(pair));
        pair->append(WrapJsonValue(entry.value, vm));
    }
    return EndList(vm, result);
}

Value JsonEntriesToObject(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    crude_json::object object;
    result->append(Value());
    bool success = isList(args[0]);
    std::string error = success ? "" : "Entries must be a list.";
    if (success)
    {
        for (const Value& entry : asList(args[0])->items)
        {
            if (!isList(entry) || asList(entry)->items.size() != 2 || !isString(asList(entry)->items[0]))
            {
                success = false;
                error = "Every entry must be a two-item list with a string key.";
                break;
            }
            crude_json::value value;
            std::string conversionError;
            if (!JsonFromWrappedValue(asList(entry)->items[1], vm, value, conversionError))
            {
                success = false;
                error = "Every entry value must be a JsonValue.";
                break;
            }
            object[asString(asList(entry)->items[0])->chars] = std::move(value);
        }
    }
    if (success)
        result->items[0] = JsonValueFromJson(crude_json::value(std::move(object)), vm);
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value JsonReadFile(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    Value value;
    bool success = false;
    std::string error;
    if (!isString(args[0]))
        error = "Path must be a string.";
    else
    {
        std::ifstream stream(asString(args[0])->chars, std::ios::binary);
        if (!stream)
            error = "Could not open JSON file for reading.";
        else
        {
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            if (!stream.good() && !stream.eof())
                error = "Could not read the complete JSON file.";
            else
            {
                const crude_json::value parsed = crude_json::value::parse(buffer.str());
                if (parsed.is_discarded())
                    error = "File does not contain valid JSON.";
                else
                {
                    value = JsonValueFromJson(parsed, vm);
                    success = true;
                }
            }
        }
    }
    result->append(value);
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value JsonWriteFile(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isBoolean(args[2]) || !isNumber(args[3]) || !isBoolean(args[4]))
        return StatusResult(vm, false, "Path must be text, Pretty and Overwrite must be boolean, and Indent must be a number.");
    const std::filesystem::path path(asString(args[0])->chars);
    if (!asBoolean(args[4]) && std::filesystem::exists(path))
        return StatusResult(vm, false, "The destination exists and Overwrite is false.");
    crude_json::value json;
    std::string error;
    if (!JsonFromWrappedValue(args[1], vm, json, error))
        return StatusResult(vm, false, std::move(error));
    const int indent = asBoolean(args[2]) ? std::clamp(ClampedInt(args[3], 2), 0, 16) : -1;
    const std::string text = json.dump(indent);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return StatusResult(vm, false, "Could not open JSON file for writing.");
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return StatusResult(vm, static_cast<bool>(stream), stream ? "" : "Could not write the complete JSON file.");
}

Value FileReadBytes(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    ObjList* bytes = newList();
    result->append(Value(bytes));
    bool success = false;
    std::string error;
    if (!isString(args[0]))
        error = "Path must be a string.";
    else
    {
        std::ifstream stream(asString(args[0])->chars, std::ios::binary);
        if (!stream)
            error = "Could not open file for reading.";
        else
        {
            char character = 0;
            while (stream.get(character))
                bytes->append(Value(static_cast<double>(static_cast<unsigned char>(character))));
            success = stream.eof();
            if (!success)
                error = "Could not read the complete file.";
        }
    }
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

bool BytesFromValue(const Value& value, std::string& bytes, std::string& error)
{
    if (!isList(value))
    {
        error = "Bytes must be a list of integers.";
        return false;
    }
    bytes.reserve(asList(value)->items.size());
    for (const Value& item : asList(value)->items)
    {
        if (!IsInteger(item) || asNumber(item) < 0.0 || asNumber(item) > 255.0)
        {
            error = "Every byte must be an integer from 0 through 255.";
            return false;
        }
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(asNumber(item))));
    }
    return true;
}

Value FileWriteBytes(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isBoolean(args[2]))
        return StatusResult(vm, false, "Path must be text and Overwrite must be boolean.");
    std::string bytes;
    std::string error;
    if (!BytesFromValue(args[1], bytes, error))
        return StatusResult(vm, false, std::move(error));
    const std::filesystem::path path(asString(args[0])->chars);
    if (!asBoolean(args[2]) && std::filesystem::exists(path))
        return StatusResult(vm, false, "The destination exists and Overwrite is false.");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return StatusResult(vm, false, "Could not open file for writing.");
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return StatusResult(vm, static_cast<bool>(stream), stream ? "" : "Could not write the complete file.");
}

Value FileReadTextEncoded(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::string text;
    bool success = false;
    std::string error;
    if (!isString(args[0]) || !isString(args[1]))
        error = "Path and encoding must be strings.";
    else
    {
        std::ifstream stream(asString(args[0])->chars, std::ios::binary);
        if (!stream)
            error = "Could not open file for reading.";
        else
        {
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            const std::string raw = buffer.str();
            success = (stream.good() || stream.eof()) && ConvertTextForEncoding(raw, asString(args[1])->chars, false, text, error);
        }
    }
    result->append(StringValue(std::move(text)));
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value FileWriteTextEncoded(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isString(args[1]) || !isString(args[2]) || !isBoolean(args[3]))
        return StatusResult(vm, false, "Path, text, and encoding must be strings and Overwrite must be boolean.");
    const std::filesystem::path path(asString(args[0])->chars);
    if (!asBoolean(args[3]) && std::filesystem::exists(path))
        return StatusResult(vm, false, "The destination exists and Overwrite is false.");
    std::string encoded;
    std::string error;
    if (!ConvertTextForEncoding(asString(args[1])->chars, asString(args[2])->chars, true, encoded, error))
        return StatusResult(vm, false, std::move(error));
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return StatusResult(vm, false, "Could not open file for writing.");
    stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    return StatusResult(vm, static_cast<bool>(stream), stream ? "" : "Could not write the complete file.");
}

Value FileCopy(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isString(args[1]) || !isBoolean(args[2]))
        return StatusResult(vm, false, "Source and destination must be strings and Overwrite must be boolean.");
    std::error_code error;
    const auto options = asBoolean(args[2]) ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none;
    std::filesystem::copy(asString(args[0])->chars, asString(args[1])->chars, options | std::filesystem::copy_options::recursive, error);
    return StatusResult(vm, !error, error.message());
}

Value FileMove(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isString(args[1]) || !isBoolean(args[2]))
        return StatusResult(vm, false, "Source and destination must be strings and Overwrite must be boolean.");
    const std::filesystem::path source(asString(args[0])->chars);
    const std::filesystem::path destination(asString(args[1])->chars);
    std::error_code error;
    if (std::filesystem::exists(destination, error))
    {
        if (!asBoolean(args[2]))
            return StatusResult(vm, false, "The destination exists and Overwrite is false.");
        std::filesystem::remove_all(destination, error);
        if (error)
            return StatusResult(vm, false, error.message());
    }
    std::filesystem::rename(source, destination, error);
    if (error)
    {
        error.clear();
        std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
        if (!error)
            std::filesystem::remove_all(source, error);
    }
    return StatusResult(vm, !error, error.message());
}

Value FileDelete(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isBoolean(args[1]))
        return StatusResult(vm, false, "Path must be a string and Recursive must be boolean.");
    std::error_code error;
    const std::filesystem::path path(asString(args[0])->chars);
    const bool existed = std::filesystem::exists(path, error);
    if (!error && existed)
    {
        if (asBoolean(args[1]))
            std::filesystem::remove_all(path, error);
        else
            std::filesystem::remove(path, error);
    }
    return StatusResult(vm, !error, error.message());
}

Value DirectoryCreate(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isBoolean(args[1]))
        return StatusResult(vm, false, "Path must be a string and Recursive must be boolean.");
    std::error_code error;
    if (asBoolean(args[1]))
        std::filesystem::create_directories(asString(args[0])->chars, error);
    else
        std::filesystem::create_directory(asString(args[0])->chars, error);
    return StatusResult(vm, !error, error.message());
}

Value DirectoryRemove(int, Value* args, VM* vm) { return FileDelete(2, args, vm); }

double FileTimeToUnixSeconds(std::filesystem::file_time_type time)
{
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration<double>(systemTime.time_since_epoch()).count();
}

Value FileMetadata(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    bool exists = false;
    bool regular = false;
    bool directory = false;
    double size = 0.0;
    double modified = 0.0;
    bool success = false;
    std::string message;
    if (!isString(args[0]))
        message = "Path must be a string.";
    else
    {
        std::error_code error;
        const std::filesystem::path path(asString(args[0])->chars);
        const std::filesystem::file_status status = std::filesystem::status(path, error);
        if (!error)
        {
            exists = std::filesystem::exists(status);
            regular = std::filesystem::is_regular_file(status);
            directory = std::filesystem::is_directory(status);
            if (regular)
                size = static_cast<double>(std::filesystem::file_size(path, error));
            if (!error && exists)
                modified = FileTimeToUnixSeconds(std::filesystem::last_write_time(path, error));
        }
        success = !error;
        message = error.message();
    }
    result->append(Value(exists));
    result->append(Value(regular));
    result->append(Value(directory));
    result->append(Value(size));
    result->append(Value(modified));
    result->append(Value(success));
    result->append(StringValue(std::move(message)));
    return EndList(vm, result);
}

Value PathCurrent(int, Value*, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::error_code error;
    const std::string path = std::filesystem::current_path(error).string();
    result->append(StringValue(path));
    result->append(Value(!error));
    result->append(StringValue(error.message()));
    return EndList(vm, result);
}

Value PathAbsolute(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::error_code error;
    std::string path;
    if (!isString(args[0]))
        error = std::make_error_code(std::errc::invalid_argument);
    else
        path = std::filesystem::absolute(asString(args[0])->chars, error).lexically_normal().string();
    result->append(StringValue(std::move(path)));
    result->append(Value(!error));
    result->append(StringValue(error ? (isString(args[0]) ? error.message() : "Path must be a string.") : ""));
    return EndList(vm, result);
}

Value PathCanonical(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::error_code error;
    std::string path;
    if (!isString(args[0]))
        error = std::make_error_code(std::errc::invalid_argument);
    else
        path = std::filesystem::canonical(asString(args[0])->chars, error).string();
    result->append(StringValue(std::move(path)));
    result->append(Value(!error));
    result->append(StringValue(error ? (isString(args[0]) ? error.message() : "Path must be a string.") : ""));
    return EndList(vm, result);
}

Value PathStem(int, Value* args, VM*)
{
    return isString(args[0]) ? StringValue(std::filesystem::path(asString(args[0])->chars).stem().string()) : Value();
}

Value PathReplaceFilename(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isString(args[1]))
        return Value();
    std::filesystem::path path(asString(args[0])->chars);
    path.replace_filename(asString(args[1])->chars);
    return StringValue(path.string());
}

Value PathRelative(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::error_code error;
    std::string path;
    if (!isString(args[0]) || !isString(args[1]))
        error = std::make_error_code(std::errc::invalid_argument);
    else
        path = std::filesystem::relative(asString(args[0])->chars, asString(args[1])->chars, error).string();
    result->append(StringValue(std::move(path)));
    result->append(Value(!error));
    result->append(StringValue(error ? ((!isString(args[0]) || !isString(args[1])) ? "Path and base must be strings." : error.message()) : ""));
    return EndList(vm, result);
}

Value EnvironmentGet(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::string value;
    bool found = false;
    if (isString(args[0]))
    {
#ifdef _WIN32
        char* buffer = nullptr;
        size_t size = 0;
        if (_dupenv_s(&buffer, &size, asString(args[0])->chars.c_str()) == 0 && buffer)
        {
            value.assign(buffer);
            free(buffer);
            found = true;
        }
#else
        if (const char* environmentValue = std::getenv(asString(args[0])->chars.c_str()))
        {
            value = environmentValue;
            found = true;
        }
#endif
    }
    result->append(StringValue(std::move(value)));
    result->append(Value(found));
    return EndList(vm, result);
}

Value EnvironmentSet(int, Value* args, VM* vm)
{
    if (!isString(args[0]) || !isString(args[1]))
        return StatusResult(vm, false, "Name and value must be strings.");
#ifdef _WIN32
    const int status = _putenv_s(asString(args[0])->chars.c_str(), asString(args[1])->chars.c_str());
#else
    const int status = setenv(asString(args[0])->chars.c_str(), asString(args[1])->chars.c_str(), 1);
#endif
    return StatusResult(vm, status == 0, status == 0 ? "" : "Could not set the environment variable.");
}

Value EnvironmentUnset(int, Value* args, VM* vm)
{
    if (!isString(args[0]))
        return StatusResult(vm, false, "Name must be a string.");
#ifdef _WIN32
    const int status = _putenv_s(asString(args[0])->chars.c_str(), "");
#else
    const int status = unsetenv(asString(args[0])->chars.c_str());
#endif
    return StatusResult(vm, status == 0, status == 0 ? "" : "Could not remove the environment variable.");
}

Value EnvironmentVariables(int, Value*, VM* vm)
{
    ObjMap* result = newMap();
    vm->push(Value(result));
#ifdef _WIN32
    LPCH block = GetEnvironmentStringsA();
    if (block)
    {
        for (const char* entry = block; *entry; entry += std::strlen(entry) + 1)
        {
            const char* equals = std::strchr(entry, '=');
            if (equals && equals != entry)
            {
                Value value = StringValue(equals + 1);
                vm->push(value);
                result->set(StringValue(std::string(entry, equals)), value);
                vm->pop();
            }
        }
        FreeEnvironmentStringsA(block);
    }
#else
    for (char** item = environ; item && *item; ++item)
    {
        const char* equals = std::strchr(*item, '=');
        if (equals)
        {
            Value value = StringValue(equals + 1);
            vm->push(value);
            result->set(StringValue(std::string(*item, equals)), value);
            vm->pop();
        }
    }
#endif
    vm->pop();
    return Value(result);
}

struct ProcessOptions
{
    std::string executable;
    std::vector<std::string> arguments;
    std::string workingDirectory;
    std::map<std::string, std::string> environment;
    double timeoutSeconds = 0.0;
};

struct ProcessResult
{
    int exitCode = -1;
    std::string standardOutput;
    std::string standardError;
    bool timedOut = false;
    bool cancelled = false;
    bool success = false;
    std::string error;
};

struct ProcessControl
{
    std::atomic<bool> cancelRequested{ false };
    std::mutex nativeMutex;
#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE job = nullptr;
#else
    pid_t process = -1;
#endif
};

struct AsyncProcess
{
    std::mutex mutex;
    ProcessResult result;
    std::shared_ptr<ProcessControl> control = std::make_shared<ProcessControl>();
    bool finished = false;
};

std::mutex& ProcessMapMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<uint64_t, std::shared_ptr<AsyncProcess>>& ProcessMap()
{
    static std::unordered_map<uint64_t, std::shared_ptr<AsyncProcess>> processes;
    return processes;
}

std::atomic<uint64_t>& NextProcessId()
{
    static std::atomic<uint64_t> next{ 1 };
    return next;
}

bool ProcessEnvironment(const Value& value, std::map<std::string, std::string>& environment, std::string& error)
{
    if (isNil(value))
        return true;
    if (!isMap(value))
    {
        error = "Environment must be a map of strings.";
        return false;
    }
    for (const MapEntry& entry : asMap(value)->entries)
    {
        if (!entry.active)
            continue;
        if (!isString(entry.key) || !isString(entry.value))
        {
            error = "Environment names and values must be strings.";
            return false;
        }
        environment[asString(entry.key)->chars] = asString(entry.value)->chars;
    }
    return true;
}

bool ParseProcessOptions(Value* args, bool withTimeout, ProcessOptions& options, std::string& error)
{
    if (!isString(args[0]) || !isString(args[2]))
    {
        error = "Executable and working directory must be strings.";
        return false;
    }
    options.executable = asString(args[0])->chars;
    options.workingDirectory = asString(args[2])->chars;
    if (options.executable.empty())
    {
        error = "Executable cannot be empty.";
        return false;
    }
    if (!StringList(args[1], options.arguments, error) || !ProcessEnvironment(args[3], options.environment, error))
        return false;
    if (withTimeout)
    {
        if (!isNumber(args[4]) || !std::isfinite(asNumber(args[4])) || asNumber(args[4]) < 0.0)
        {
            error = "Timeout must be a non-negative number of seconds.";
            return false;
        }
        options.timeoutSeconds = asNumber(args[4]);
    }
    return true;
}

#ifdef _WIN32
std::string QuoteWindowsArgument(const std::string& argument)
{
    if (argument.empty())
        return "\"\"";
    if (argument.find_first_of(" \t\n\v\"") == std::string::npos)
        return argument;
    std::string result = "\"";
    size_t slashes = 0;
    for (char character : argument)
    {
        if (character == '\\')
        {
            ++slashes;
            continue;
        }
        if (character == '"')
        {
            result.append(slashes * 2 + 1, '\\');
            result.push_back('"');
            slashes = 0;
            continue;
        }
        result.append(slashes, '\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, '\\');
    result.push_back('"');
    return result;
}

std::vector<char> WindowsEnvironmentBlock(const std::map<std::string, std::string>& overrides)
{
    std::map<std::string, std::string> values;
    LPCH current = GetEnvironmentStringsA();
    if (current)
    {
        for (const char* entry = current; *entry; entry += std::strlen(entry) + 1)
        {
            const char* equals = std::strchr(entry, '=');
            if (equals && equals != entry)
                values[std::string(entry, equals)] = equals + 1;
        }
        FreeEnvironmentStringsA(current);
    }
    for (const auto& entry : overrides)
        values[entry.first] = entry.second;
    std::vector<char> block;
    for (const auto& entry : values)
    {
        const std::string item = entry.first + "=" + entry.second;
        block.insert(block.end(), item.begin(), item.end());
        block.push_back('\0');
    }
    block.push_back('\0');
    return block;
}

void ReadAvailablePipe(HANDLE pipe, std::string& destination)
{
    while (true)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
            return;
        char buffer[4096];
        DWORD read = 0;
        if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) || read == 0)
            return;
        destination.append(buffer, read);
    }
}

ProcessResult RunProcessPlatform(const ProcessOptions& options, const std::shared_ptr<ProcessControl>& control)
{
    ProcessResult result;
    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &security, 0) || !CreatePipe(&stderrRead, &stderrWrite, &security, 0))
    {
        result.error = "Could not create process output pipes.";
        if (stdoutRead) CloseHandle(stdoutRead);
        if (stdoutWrite) CloseHandle(stdoutWrite);
        if (stderrRead) CloseHandle(stderrRead);
        if (stderrWrite) CloseHandle(stderrWrite);
        return result;
    }
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);

    std::string command = QuoteWindowsArgument(options.executable);
    for (const std::string& argument : options.arguments)
        command += " " + QuoteWindowsArgument(argument);
    std::vector<char> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back('\0');
    std::vector<char> environment = WindowsEnvironmentBlock(options.environment);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stderrWrite;
    PROCESS_INFORMATION process{};
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        {
            CloseHandle(job);
            job = nullptr;
        }
    }
    const BOOL created = CreateProcessA(nullptr, commandBuffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                        environment.data(), options.workingDirectory.empty() ? nullptr : options.workingDirectory.c_str(), &startup, &process);
    const DWORD creationError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);
    if (!created)
    {
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        if (job) CloseHandle(job);
        result.error = "Could not start executable (Windows error " + std::to_string(creationError) + ").";
        return result;
    }
    if (job && !AssignProcessToJobObject(job, process.hProcess))
    {
        CloseHandle(job);
        job = nullptr;
    }
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1))
    {
        const DWORD resumeError = GetLastError();
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (job) CloseHandle(job);
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        result.error = "Could not resume executable (Windows error " + std::to_string(resumeError) + ").";
        return result;
    }
    CloseHandle(process.hThread);
    {
        std::lock_guard<std::mutex> lock(control->nativeMutex);
        control->process = process.hProcess;
        control->job = job;
    }
    const auto started = Clock::now();
    while (true)
    {
        ReadAvailablePipe(stdoutRead, result.standardOutput);
        ReadAvailablePipe(stderrRead, result.standardError);
        const DWORD wait = WaitForSingleObject(process.hProcess, 10);
        if (wait == WAIT_OBJECT_0)
        {
            if (control->cancelRequested.load())
            {
                result.cancelled = true;
                result.error = "Process was cancelled.";
            }
            break;
        }
        if (control->cancelRequested.load())
        {
            if (job) TerminateJobObject(job, 1);
            else TerminateProcess(process.hProcess, 1);
            result.cancelled = true;
            result.error = "Process was cancelled.";
            WaitForSingleObject(process.hProcess, INFINITE);
            break;
        }
        if (options.timeoutSeconds > 0.0 && std::chrono::duration<double>(Clock::now() - started).count() >= options.timeoutSeconds)
        {
            if (job) TerminateJobObject(job, 1);
            else TerminateProcess(process.hProcess, 1);
            result.timedOut = true;
            result.error = "Process timed out.";
            WaitForSingleObject(process.hProcess, INFINITE);
            break;
        }
    }
    ReadAvailablePipe(stdoutRead, result.standardOutput);
    ReadAvailablePipe(stderrRead, result.standardError);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);
    result.success = !result.timedOut && !result.cancelled;
    {
        std::lock_guard<std::mutex> lock(control->nativeMutex);
        control->process = nullptr;
        control->job = nullptr;
    }
    if (job) CloseHandle(job);
    CloseHandle(process.hProcess);
    CloseHandle(stdoutRead);
    CloseHandle(stderrRead);
    return result;
}
#else
void ReadAvailableFd(int descriptor, std::string& destination)
{
    char buffer[4096];
    while (true)
    {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count > 0) destination.append(buffer, static_cast<size_t>(count));
        else return;
    }
}

ProcessResult RunProcessPlatform(const ProcessOptions& options, const std::shared_ptr<ProcessControl>& control)
{
    ProcessResult result;
    int outputPipe[2]{};
    int errorPipe[2]{};
    if (pipe(outputPipe) != 0 || pipe(errorPipe) != 0)
    {
        result.error = "Could not create process output pipes.";
        return result;
    }
    const pid_t child = fork();
    if (child == 0)
    {
        dup2(outputPipe[1], STDOUT_FILENO);
        dup2(errorPipe[1], STDERR_FILENO);
        close(outputPipe[0]); close(outputPipe[1]); close(errorPipe[0]); close(errorPipe[1]);
        if (!options.workingDirectory.empty())
            chdir(options.workingDirectory.c_str());
        for (const auto& entry : options.environment)
            setenv(entry.first.c_str(), entry.second.c_str(), 1);
        std::vector<char*> arguments;
        arguments.push_back(const_cast<char*>(options.executable.c_str()));
        for (const std::string& argument : options.arguments)
            arguments.push_back(const_cast<char*>(argument.c_str()));
        arguments.push_back(nullptr);
        execvp(options.executable.c_str(), arguments.data());
        _exit(127);
    }
    close(outputPipe[1]); close(errorPipe[1]);
    if (child < 0)
    {
        close(outputPipe[0]); close(errorPipe[0]);
        result.error = "Could not start executable.";
        return result;
    }
    fcntl(outputPipe[0], F_SETFL, O_NONBLOCK);
    fcntl(errorPipe[0], F_SETFL, O_NONBLOCK);
    {
        std::lock_guard<std::mutex> lock(control->nativeMutex);
        control->process = child;
    }
    const auto started = Clock::now();
    int status = 0;
    while (waitpid(child, &status, WNOHANG) == 0)
    {
        ReadAvailableFd(outputPipe[0], result.standardOutput);
        ReadAvailableFd(errorPipe[0], result.standardError);
        if (control->cancelRequested.load())
        {
            kill(child, SIGKILL);
            result.cancelled = true;
            result.error = "Process was cancelled.";
        }
        else if (options.timeoutSeconds > 0.0 && std::chrono::duration<double>(Clock::now() - started).count() >= options.timeoutSeconds)
        {
            kill(child, SIGKILL);
            result.timedOut = true;
            result.error = "Process timed out.";
        }
        if (result.cancelled || result.timedOut)
        {
            waitpid(child, &status, 0);
            break;
        }
        poll(nullptr, 0, 10);
    }
    if (control->cancelRequested.load() && !result.timedOut)
    {
        result.cancelled = true;
        result.error = "Process was cancelled.";
    }
    ReadAvailableFd(outputPipe[0], result.standardOutput);
    ReadAvailableFd(errorPipe[0], result.standardError);
    close(outputPipe[0]); close(errorPipe[0]);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
    result.success = !result.timedOut && !result.cancelled;
    {
        std::lock_guard<std::mutex> lock(control->nativeMutex);
        control->process = -1;
    }
    return result;
}
#endif

Value ProcessResultValue(VM* vm, const ProcessResult& process)
{
    ObjList* result = BeginList(vm);
    result->append(Value(static_cast<double>(process.exitCode)));
    result->append(StringValue(process.standardOutput));
    result->append(StringValue(process.standardError));
    result->append(Value(process.timedOut));
    result->append(Value(process.cancelled));
    result->append(Value(process.success));
    result->append(StringValue(process.error));
    return EndList(vm, result);
}

Value ProcessRun(int, Value* args, VM* vm)
{
    ProcessOptions options;
    std::string error;
    if (!ParseProcessOptions(args, true, options, error))
    {
        ProcessResult result;
        result.error = std::move(error);
        return ProcessResultValue(vm, result);
    }
    return ProcessResultValue(vm, RunProcessPlatform(options, std::make_shared<ProcessControl>()));
}

Value ProcessStart(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    ProcessOptions options;
    std::string error;
    const bool valid = ParseProcessOptions(args, true, options, error);
    uint64_t handle = 0;
    if (valid)
    {
        handle = NextProcessId().fetch_add(1);
        auto state = std::make_shared<AsyncProcess>();
        {
            std::lock_guard<std::mutex> lock(ProcessMapMutex());
            ProcessMap()[handle] = state;
        }
        std::thread([state, options = std::move(options)]()
        {
            ProcessResult processResult = RunProcessPlatform(options, state->control);
            std::lock_guard<std::mutex> lock(state->mutex);
            state->result = std::move(processResult);
            state->finished = true;
        }).detach();
    }
    result->append(Value(static_cast<double>(handle)));
    result->append(Value(valid));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value ProcessPoll(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::shared_ptr<AsyncProcess> state;
    const uint64_t handle = IsInteger(args[0]) && asNumber(args[0]) >= 0.0 ? static_cast<uint64_t>(asNumber(args[0])) : 0;
    {
        std::lock_guard<std::mutex> lock(ProcessMapMutex());
        const auto found = ProcessMap().find(handle);
        if (found != ProcessMap().end())
            state = found->second;
    }
    bool finished = false;
    ProcessResult process;
    std::string error;
    if (!state)
        error = "Unknown process handle.";
    else
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        finished = state->finished;
        process = state->result;
    }
    result->append(Value(finished));
    result->append(Value(static_cast<double>(process.exitCode)));
    result->append(StringValue(std::move(process.standardOutput)));
    result->append(StringValue(std::move(process.standardError)));
    result->append(Value(process.timedOut));
    result->append(Value(process.cancelled));
    result->append(Value(state != nullptr && (!finished || process.success)));
    result->append(StringValue(error.empty() ? process.error : error));
    if (state && finished)
    {
        std::lock_guard<std::mutex> lock(ProcessMapMutex());
        ProcessMap().erase(handle);
    }
    return EndList(vm, result);
}

Value ProcessCancel(int, Value* args, VM* vm)
{
    if (!IsInteger(args[0]) || asNumber(args[0]) < 0.0)
        return StatusResult(vm, false, "Handle must be a non-negative integer.");
    const uint64_t handle = static_cast<uint64_t>(asNumber(args[0]));
    std::shared_ptr<AsyncProcess> state;
    {
        std::lock_guard<std::mutex> lock(ProcessMapMutex());
        const auto found = ProcessMap().find(handle);
        if (found != ProcessMap().end())
            state = found->second;
    }
    if (!state)
        return StatusResult(vm, false, "Unknown process handle.");
    state->control->cancelRequested.store(true);
#ifdef _WIN32
    {
        std::lock_guard<std::mutex> lock(state->control->nativeMutex);
        if (state->control->job)
            TerminateJobObject(state->control->job, 1);
        else if (state->control->process)
            TerminateProcess(state->control->process, 1);
    }
#else
    {
        std::lock_guard<std::mutex> lock(state->control->nativeMutex);
        if (state->control->process > 0)
            kill(state->control->process, SIGKILL);
    }
#endif
    return StatusResult(vm, true);
}

Value RegexMatch(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    bool matched = false;
    std::string error;
    if (!isString(args[0]) || !isString(args[1]))
        error = "Text and pattern must be strings.";
    else
    {
        try { matched = std::regex_match(asString(args[0])->chars, std::regex(asString(args[1])->chars)); }
        catch (const std::regex_error& exception) { error = exception.what(); }
    }
    result->append(Value(matched));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value RegexSearch(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    bool found = false;
    std::string match;
    double index = -1.0;
    ObjList* captures = newList();
    vm->push(Value(captures));
    std::string error;
    if (!isString(args[0]) || !isString(args[1]))
        error = "Text and pattern must be strings.";
    else
    {
        try
        {
            std::smatch matches;
            found = std::regex_search(asString(args[0])->chars, matches, std::regex(asString(args[1])->chars));
            if (found)
            {
                match = matches.str(0);
                index = static_cast<double>(matches.position(0));
                for (size_t capture = 1; capture < matches.size(); ++capture)
                    captures->append(StringValue(matches.str(capture)));
            }
        }
        catch (const std::regex_error& exception) { error = exception.what(); }
    }
    result->append(Value(found));
    result->append(StringValue(std::move(match)));
    result->append(Value(index));
    result->append(Value(captures));
    result->append(StringValue(std::move(error)));
    vm->pop();
    return EndList(vm, result);
}

Value RegexReplace(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::string text;
    bool success = false;
    std::string error;
    if (!isString(args[0]) || !isString(args[1]) || !isString(args[2]))
        error = "Text, pattern, and replacement must be strings.";
    else
    {
        try
        {
            text = std::regex_replace(asString(args[0])->chars, std::regex(asString(args[1])->chars), asString(args[2])->chars);
            success = true;
        }
        catch (const std::regex_error& exception) { error = exception.what(); }
    }
    result->append(StringValue(std::move(text)));
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value RegexSplit(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    ObjList* parts = newList();
    result->append(Value(parts));
    bool success = false;
    std::string error;
    if (!isString(args[0]) || !isString(args[1]))
        error = "Text and pattern must be strings.";
    else
    {
        try
        {
            const std::regex pattern(asString(args[1])->chars);
            std::sregex_token_iterator iterator(asString(args[0])->chars.begin(), asString(args[0])->chars.end(), pattern, -1);
            const std::sregex_token_iterator end;
            for (; iterator != end; ++iterator)
                parts->append(StringValue(iterator->str()));
            success = true;
        }
        catch (const std::regex_error& exception) { error = exception.what(); }
    }
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value StringUnicodeLength(int, Value* args, VM*)
{
    if (!isString(args[0]))
        return Value();
    std::vector<uint32_t> codepoints;
    return DecodeUtf8(asString(args[0])->chars, codepoints) ? Value(static_cast<double>(codepoints.size())) : Value();
}

Value StringUnicodeSubstring(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isNumber(args[1]) || !isNumber(args[2]))
        return Value();
    std::vector<uint32_t> codepoints;
    if (!DecodeUtf8(asString(args[0])->chars, codepoints))
        return Value();
    const int start = std::clamp(ClampedInt(args[1]), 0, static_cast<int>(codepoints.size()));
    const int count = std::max(0, ClampedInt(args[2]));
    const int end = std::min(static_cast<int>(codepoints.size()), start + count);
    return StringValue(EncodeUtf8(std::vector<uint32_t>(codepoints.begin() + start, codepoints.begin() + end)));
}

#ifdef _WIN32
bool Utf8ToWide(const std::string& text, std::wstring& result)
{
    if (text.empty()) { result.clear(); return true; }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return false;
    result.resize(length);
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length) == length;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}
#endif

Value StringUnicodeCase(Value* args, bool upper)
{
    if (!isString(args[0]))
        return Value();
#ifdef _WIN32
    std::wstring input;
    if (!Utf8ToWide(asString(args[0])->chars, input))
        return Value();
    if (input.empty())
        return StringValue("");
    const DWORD flags = upper ? LCMAP_UPPERCASE : LCMAP_LOWERCASE;
    const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, flags, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr, 0);
    if (length <= 0)
        return Value();
    std::wstring mapped(static_cast<size_t>(length), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, flags, input.data(), static_cast<int>(input.size()), mapped.data(), length, nullptr, nullptr, 0) <= 0)
        return Value();
    return StringValue(WideToUtf8(mapped));
#else
    std::vector<uint32_t> codepoints;
    if (!DecodeUtf8(asString(args[0])->chars, codepoints))
        return Value();
    for (uint32_t& codepoint : codepoints)
    {
        if (codepoint <= static_cast<uint32_t>(std::numeric_limits<wchar_t>::max()))
            codepoint = static_cast<uint32_t>(upper ? std::towupper(static_cast<wchar_t>(codepoint)) : std::towlower(static_cast<wchar_t>(codepoint)));
    }
    return StringValue(EncodeUtf8(codepoints));
#endif
}

Value StringUnicodeLower(int, Value* args, VM*) { return StringUnicodeCase(args, false); }
Value StringUnicodeUpper(int, Value* args, VM*) { return StringUnicodeCase(args, true); }

Value StringPad(Value* args, bool left)
{
    if (!isString(args[0]) || !isNumber(args[1]) || !isString(args[2]))
        return Value();
    std::vector<uint32_t> text;
    std::vector<uint32_t> padding;
    if (!DecodeUtf8(asString(args[0])->chars, text) || !DecodeUtf8(asString(args[2])->chars, padding) || padding.empty())
        return Value();
    const int target = std::max(0, ClampedInt(args[1]));
    if (target <= static_cast<int>(text.size()))
        return args[0];
    std::vector<uint32_t> fill;
    fill.reserve(static_cast<size_t>(target) - text.size());
    for (size_t index = 0; fill.size() + text.size() < static_cast<size_t>(target); ++index)
        fill.push_back(padding[index % padding.size()]);
    if (left)
        fill.insert(fill.end(), text.begin(), text.end());
    else
    {
        text.insert(text.end(), fill.begin(), fill.end());
        fill.swap(text);
    }
    return StringValue(EncodeUtf8(fill));
}

Value StringPadLeft(int, Value* args, VM*) { return StringPad(args, true); }
Value StringPadRight(int, Value* args, VM*) { return StringPad(args, false); }

Value StringRepeat(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isNumber(args[1]))
        return Value();
    const int count = std::max(0, ClampedInt(args[1]));
    const std::string& text = asString(args[0])->chars;
    if (text.size() > 0 && static_cast<size_t>(count) > 16 * 1024 * 1024 / text.size())
        return Value();
    std::string result;
    result.reserve(text.size() * static_cast<size_t>(count));
    for (int index = 0; index < count; ++index)
        result += text;
    return StringValue(std::move(result));
}

Value StringCount(int, Value* args, VM*)
{
    if (!isString(args[0]) || !isString(args[1]))
        return Value();
    const std::string& text = asString(args[0])->chars;
    const std::string& needle = asString(args[1])->chars;
    if (needle.empty())
        return Value(0.0);
    size_t count = 0;
    size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos)
    {
        ++count;
        position += needle.size();
    }
    return Value(static_cast<double>(count));
}

Value StringLines(int, Value* args, VM* vm)
{
    if (!isString(args[0]))
        return Value();
    ObjList* result = BeginList(vm);
    const std::string& text = asString(args[0])->chars;
    size_t start = 0;
    while (start < text.size())
    {
        const size_t end = text.find_first_of("\r\n", start);
        result->append(StringValue(text.substr(start, end == std::string::npos ? std::string::npos : end - start)));
        if (end == std::string::npos)
            break;
        start = end + ((text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n') ? 2 : 1);
    }
    if (text.empty() || text.back() == '\r' || text.back() == '\n')
        result->append(StringValue(""));
    return EndList(vm, result);
}

Value StringCharacterClassification(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::vector<uint32_t> codepoints;
    const bool valid = isString(args[0]) && DecodeUtf8(asString(args[0])->chars, codepoints) && codepoints.size() == 1;
#ifdef _WIN32
    WORD classification = 0;
    std::wstring wide;
    if (valid && Utf8ToWide(asString(args[0])->chars, wide) && !wide.empty())
    {
        std::vector<WORD> classifications(wide.size());
        if (GetStringTypeW(CT_CTYPE1, wide.data(), static_cast<int>(wide.size()), classifications.data()))
            classification = classifications[0];
    }
    result->append(Value(valid && (classification & C1_ALPHA) != 0));
    result->append(Value(valid && (classification & C1_DIGIT) != 0));
    result->append(Value(valid && (classification & C1_SPACE) != 0));
    result->append(Value(valid && (classification & C1_UPPER) != 0));
    result->append(Value(valid && (classification & C1_LOWER) != 0));
#else
    wchar_t character = valid && codepoints[0] <= static_cast<uint32_t>(std::numeric_limits<wchar_t>::max()) ? static_cast<wchar_t>(codepoints[0]) : 0;
    result->append(Value(valid && std::iswalpha(character) != 0));
    result->append(Value(valid && std::iswdigit(character) != 0));
    result->append(Value(valid && std::iswspace(character) != 0));
    result->append(Value(valid && std::iswupper(character) != 0));
    result->append(Value(valid && std::iswlower(character) != 0));
#endif
    result->append(Value(valid));
    return EndList(vm, result);
}

const char* Base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const std::string& input)
{
    std::string result;
    result.reserve((input.size() + 2) / 3 * 4);
    for (size_t index = 0; index < input.size(); index += 3)
    {
        const uint32_t first = static_cast<unsigned char>(input[index]);
        const uint32_t second = index + 1 < input.size() ? static_cast<unsigned char>(input[index + 1]) : 0;
        const uint32_t third = index + 2 < input.size() ? static_cast<unsigned char>(input[index + 2]) : 0;
        const uint32_t block = (first << 16) | (second << 8) | third;
        result.push_back(Base64Alphabet[(block >> 18) & 63]);
        result.push_back(Base64Alphabet[(block >> 12) & 63]);
        result.push_back(index + 1 < input.size() ? Base64Alphabet[(block >> 6) & 63] : '=');
        result.push_back(index + 2 < input.size() ? Base64Alphabet[block & 63] : '=');
    }
    return result;
}

bool Base64Decode(const std::string& input, std::string& output)
{
    if (input.size() % 4 != 0)
        return false;
    std::array<int, 256> lookup{};
    lookup.fill(-1);
    for (int index = 0; index < 64; ++index)
        lookup[static_cast<unsigned char>(Base64Alphabet[index])] = index;
    for (size_t index = 0; index < input.size(); index += 4)
    {
        int values[4]{};
        for (int offset = 0; offset < 4; ++offset)
        {
            const unsigned char character = static_cast<unsigned char>(input[index + offset]);
            values[offset] = character == '=' ? 0 : lookup[character];
            if (values[offset] < 0 || (character == '=' && (offset < 2 || index + 4 != input.size())))
                return false;
        }
        const uint32_t block = (values[0] << 18) | (values[1] << 12) | (values[2] << 6) | values[3];
        output.push_back(static_cast<char>((block >> 16) & 0xff));
        if (input[index + 2] != '=') output.push_back(static_cast<char>((block >> 8) & 0xff));
        if (input[index + 3] != '=') output.push_back(static_cast<char>(block & 0xff));
    }
    return true;
}

Value EncodingBase64Encode(int, Value* args, VM*)
{
    return isString(args[0]) ? StringValue(Base64Encode(asString(args[0])->chars)) : Value();
}

Value EncodingBase64Decode(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::string decoded;
    const bool success = isString(args[0]) && Base64Decode(asString(args[0])->chars, decoded);
    result->append(StringValue(std::move(decoded)));
    result->append(Value(success));
    result->append(StringValue(success ? "" : "Input is not valid base64."));
    return EndList(vm, result);
}

Value EncodingTextToBytes(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    ObjList* bytes = newList();
    result->append(Value(bytes));
    std::string encoded;
    std::string error;
    const bool success = isString(args[0]) && isString(args[1]) && ConvertTextForEncoding(asString(args[0])->chars, asString(args[1])->chars, true, encoded, error);
    if (success)
        for (unsigned char byte : encoded)
            bytes->append(Value(static_cast<double>(byte)));
    if (!isString(args[0]) || !isString(args[1])) error = "Text and encoding must be strings.";
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value EncodingBytesToText(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::string bytes;
    std::string text;
    std::string error;
    bool success = isString(args[1]) && BytesFromValue(args[0], bytes, error);
    if (success)
        success = ConvertTextForEncoding(bytes, asString(args[1])->chars, false, text, error);
    if (!isString(args[1])) error = "Encoding must be a string.";
    result->append(StringValue(std::move(text)));
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

std::tm SafeLocalTime(std::time_t time)
{
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

std::tm SafeUtcTime(std::time_t time)
{
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, &time);
#else
    gmtime_r(&time, &result);
#endif
    return result;
}

std::time_t UtcTime(std::tm& value)
{
#ifdef _WIN32
    return _mkgmtime(&value);
#else
    return timegm(&value);
#endif
}

Value TimeNow(int, Value*, VM*)
{
    return Value(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());
}

Value TimeMonotonicNow(int, Value*, VM*)
{
    return Value(std::chrono::duration<double>(Clock::now().time_since_epoch()).count());
}

Value TimeParse(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    double timestamp = 0.0;
    bool success = false;
    std::string error;
    if (!isString(args[0]) || !isString(args[1]) || !isBoolean(args[2]))
        error = "Text and format must be strings and UTC must be boolean.";
    else
    {
        std::tm parsed{};
        parsed.tm_isdst = -1;
        std::istringstream stream(asString(args[0])->chars);
        stream >> std::get_time(&parsed, asString(args[1])->chars.c_str());
        const bool formatMatched = !stream.fail();
        if (formatMatched)
        {
            stream.clear();
            stream >> std::ws;
        }
        if (!formatMatched || stream.peek() != std::char_traits<char>::eof())
            error = "Text does not match the requested date/time format.";
        else
        {
            const std::time_t time = asBoolean(args[2]) ? UtcTime(parsed) : std::mktime(&parsed);
            if (time == static_cast<std::time_t>(-1))
                error = "The parsed date/time is outside the supported range.";
            else
            {
                timestamp = static_cast<double>(time);
                success = true;
            }
        }
    }
    result->append(Value(timestamp));
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value TimeFormat(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    std::string text;
    bool success = false;
    std::string error;
    if (!isNumber(args[0]) || !isString(args[1]) || !isBoolean(args[2]) || !std::isfinite(asNumber(args[0])))
        error = "Timestamp must be a finite number, format must be text, and UTC must be boolean.";
    else
    {
        const std::time_t time = static_cast<std::time_t>(asNumber(args[0]));
        const std::tm value = asBoolean(args[2]) ? SafeUtcTime(time) : SafeLocalTime(time);
        std::ostringstream stream;
        stream << std::put_time(&value, asString(args[1])->chars.c_str());
        success = static_cast<bool>(stream);
        text = stream.str();
        if (!success) error = "Could not format the date/time.";
    }
    result->append(StringValue(std::move(text)));
    result->append(Value(success));
    result->append(StringValue(std::move(error)));
    return EndList(vm, result);
}

Value DurationFromMilliseconds(int, Value* args, VM*) { return isNumber(args[0]) ? Value(asNumber(args[0]) / 1000.0) : Value(); }
Value DurationToMilliseconds(int, Value* args, VM*) { return isNumber(args[0]) ? Value(asNumber(args[0]) * 1000.0) : Value(); }
Value DurationFromParts(int, Value* args, VM*)
{
    if (!isNumber(args[0]) || !isNumber(args[1]) || !isNumber(args[2]) || !isNumber(args[3]))
        return Value();
    return Value(asNumber(args[0]) * 86400.0 + asNumber(args[1]) * 3600.0 + asNumber(args[2]) * 60.0 + asNumber(args[3]));
}

struct TimerState
{
    VM* owner = nullptr;
    Value callback;
    Clock::time_point deadline;
    double interval = 0.0;
    bool repeating = false;
};

std::mutex& TimerMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<uint64_t, std::shared_ptr<TimerState>>& Timers()
{
    static std::unordered_map<uint64_t, std::shared_ptr<TimerState>> timers;
    return timers;
}

std::atomic<uint64_t>& NextTimerId()
{
    static std::atomic<uint64_t> next{ 1 };
    return next;
}

Value AddTimer(Value* args, VM* vm, bool repeating)
{
    if (!isNumber(args[0]) || !std::isfinite(asNumber(args[0])) || asNumber(args[0]) < 0.0 ||
        (repeating && asNumber(args[0]) == 0.0) || !isCallable(args[1]) || getCallableArity(args[1]) != 0)
        return Value();

    const uint64_t handle = NextTimerId().fetch_add(1);
    auto timer = std::make_shared<TimerState>();
    timer->owner = vm;
    timer->callback = args[1];
    timer->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(asNumber(args[0])));
    timer->interval = asNumber(args[0]);
    timer->repeating = repeating;

    {
        std::lock_guard<std::mutex> lock(TimerMutex());
        Timers()[handle] = std::move(timer);
    }
    return Value(static_cast<double>(handle));
}

Value TimerAfter(int, Value* args, VM* vm) { return AddTimer(args, vm, false); }
Value TimerEvery(int, Value* args, VM* vm) { return AddTimer(args, vm, true); }

Value TimerCancel(int, Value* args, VM* vm)
{
    if (!IsInteger(args[0]) || asNumber(args[0]) < 0.0)
        return StatusResult(vm, false, "Handle must be a non-negative integer.");

    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(TimerMutex());
        const auto timer = Timers().find(static_cast<uint64_t>(asNumber(args[0])));
        if (timer != Timers().end() && timer->second->owner == vm)
        {
            Timers().erase(timer);
            removed = true;
        }
    }
    if (!removed)
        return StatusResult(vm, false, "Unknown timer handle.");
    return StatusResult(vm, true);
}

Value MathPi(int, Value*, VM*) { return Value(3.141592653589793238462643383279502884); }
Value MathE(int, Value*, VM*) { return Value(2.718281828459045235360287471352662498); }
Value MathTau(int, Value*, VM*) { return Value(6.283185307179586476925286766559005768); }
Value MathSin(int, Value* args, VM*) { return isNumber(args[0]) ? Value(std::sin(asNumber(args[0]))) : Value(); }
Value MathCos(int, Value* args, VM*) { return isNumber(args[0]) ? Value(std::cos(asNumber(args[0]))) : Value(); }
Value MathTan(int, Value* args, VM*) { return isNumber(args[0]) ? Value(std::tan(asNumber(args[0]))) : Value(); }
Value MathAsin(int, Value* args, VM*) { return isNumber(args[0]) && std::fabs(asNumber(args[0])) <= 1.0 ? Value(std::asin(asNumber(args[0]))) : Value(); }
Value MathAcos(int, Value* args, VM*) { return isNumber(args[0]) && std::fabs(asNumber(args[0])) <= 1.0 ? Value(std::acos(asNumber(args[0]))) : Value(); }
Value MathAtan(int, Value* args, VM*) { return isNumber(args[0]) ? Value(std::atan(asNumber(args[0]))) : Value(); }
Value MathAtan2(int, Value* args, VM*) { return isNumber(args[0]) && isNumber(args[1]) ? Value(std::atan2(asNumber(args[0]), asNumber(args[1]))) : Value(); }
Value MathLog(int, Value* args, VM*) { return isNumber(args[0]) && asNumber(args[0]) > 0.0 ? Value(std::log(asNumber(args[0]))) : Value(); }
Value MathLog10(int, Value* args, VM*) { return isNumber(args[0]) && asNumber(args[0]) > 0.0 ? Value(std::log10(asNumber(args[0]))) : Value(); }
Value MathExp(int, Value* args, VM*) { return isNumber(args[0]) ? Value(std::exp(asNumber(args[0]))) : Value(); }

Value RandomSeed(int, Value* args, VM*)
{
    if (!IsInteger(args[0]))
        return Value(false);
    std::lock_guard<std::mutex> lock(RandomMutex());
    RandomGenerator().seed(static_cast<uint64_t>(asNumber(args[0])));
    return Value(true);
}

Value RandomInteger(int, Value* args, VM*)
{
    if (!IsInteger(args[0]) || !IsInteger(args[1]))
        return Value();
    int64_t minimum = static_cast<int64_t>(asNumber(args[0]));
    int64_t maximum = static_cast<int64_t>(asNumber(args[1]));
    if (minimum > maximum) std::swap(minimum, maximum);
    std::lock_guard<std::mutex> lock(RandomMutex());
    return Value(static_cast<double>(std::uniform_int_distribution<int64_t>(minimum, maximum)(RandomGenerator())));
}

Value RandomChoice(int, Value* args, VM*)
{
    if (!isList(args[0]) || asList(args[0])->items.empty())
        return Value();
    std::lock_guard<std::mutex> lock(RandomMutex());
    const size_t index = std::uniform_int_distribution<size_t>(0, asList(args[0])->items.size() - 1)(RandomGenerator());
    return asList(args[0])->items[index];
}

Value RandomShuffle(int, Value* args, VM* vm)
{
    if (!isList(args[0]))
        return Value();
    ObjList* result = BeginList(vm);
    result->items = asList(args[0])->items;
    {
        std::lock_guard<std::mutex> lock(RandomMutex());
        std::shuffle(result->items.begin(), result->items.end(), RandomGenerator());
    }
    return EndList(vm, result);
}

Value FunctionalAny(int, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]) || getCallableArity(args[1]) != 1)
        return Value();
    bool result = false;
    forEachIterable(args[0], [&](const Value& value, int)
    {
        result = !isFalsey(callFunction(vm, args[1], value));
        return !result;
    });
    return Value(result);
}

Value FunctionalAll(int, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]) || getCallableArity(args[1]) != 1)
        return Value();
    bool result = true;
    forEachIterable(args[0], [&](const Value& value, int)
    {
        result = !isFalsey(callFunction(vm, args[1], value));
        return result;
    });
    return Value(result);
}

Value FunctionalCount(int, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]) || getCallableArity(args[1]) != 1)
        return Value();
    size_t count = 0;
    forEachIterable(args[0], [&](const Value& value, int)
    {
        if (!isFalsey(callFunction(vm, args[1], value))) ++count;
        return true;
    });
    return Value(static_cast<double>(count));
}

Value FunctionalRemove(int, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]) || getCallableArity(args[1]) != 1)
        return Value();
    ObjList* result = BeginList(vm);
    forEachIterable(args[0], [&](const Value& value, int)
    {
        if (isFalsey(callFunction(vm, args[1], value))) result->append(value);
        return true;
    });
    return EndList(vm, result);
}

Value FunctionalFind(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    Value foundValue;
    bool found = false;
    if (isIterable(args[0]) && isCallable(args[1]) && getCallableArity(args[1]) == 1)
    {
        forEachIterable(args[0], [&](const Value& value, int)
        {
            found = !isFalsey(callFunction(vm, args[1], value));
            if (found) foundValue = value;
            return !found;
        });
    }
    result->append(foundValue);
    result->append(Value(found));
    return EndList(vm, result);
}

Value FunctionalGroupBy(int, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]) || getCallableArity(args[1]) != 1)
        return Value();
    ObjMap* groups = newMap();
    vm->push(Value(groups));
    forEachIterable(args[0], [&](const Value& value, int)
    {
        Value key = callFunction(vm, args[1], value);
        Value groupValue;
        ObjList* group = nullptr;
        if (groups->get(key, &groupValue) && isList(groupValue))
            group = asList(groupValue);
        else
        {
            vm->push(key);
            group = newList();
            groups->set(key, Value(group));
            vm->pop();
        }
        group->append(value);
        return true;
    });
    vm->pop();
    return Value(groups);
}

Value ListFlatten(int, Value* args, VM* vm)
{
    if (!isList(args[0]))
        return Value();
    ObjList* result = BeginList(vm);
    for (const Value& item : asList(args[0])->items)
    {
        if (!isList(item))
            return EndList(vm, result);
        result->items.insert(result->items.end(), asList(item)->items.begin(), asList(item)->items.end());
    }
    return EndList(vm, result);
}

Value ListChunk(int, Value* args, VM* vm)
{
    if (!isList(args[0]) || !IsInteger(args[1]) || asNumber(args[1]) <= 0.0)
        return Value();
    const size_t size = static_cast<size_t>(asNumber(args[1]));
    const auto& source = asList(args[0])->items;
    ObjList* result = BeginList(vm);
    for (size_t start = 0; start < source.size(); start += size)
    {
        ObjList* chunk = newList();
        chunk->items.insert(chunk->items.end(), source.begin() + start, source.begin() + std::min(source.size(), start + size));
        result->append(Value(chunk));
    }
    return EndList(vm, result);
}

Value ListTake(int, Value* args, VM* vm)
{
    if (!isList(args[0]) || !isNumber(args[1]))
        return Value();
    ObjList* result = BeginList(vm);
    const size_t count = static_cast<size_t>(std::max(0, ClampedInt(args[1])));
    const auto& source = asList(args[0])->items;
    result->items.insert(result->items.end(), source.begin(), source.begin() + std::min(count, source.size()));
    return EndList(vm, result);
}

Value ListSkip(int, Value* args, VM* vm)
{
    if (!isList(args[0]) || !isNumber(args[1]))
        return Value();
    ObjList* result = BeginList(vm);
    const auto& source = asList(args[0])->items;
    const size_t count = std::min(static_cast<size_t>(std::max(0, ClampedInt(args[1]))), source.size());
    result->items.insert(result->items.end(), source.begin() + count, source.end());
    return EndList(vm, result);
}

Value ListCountValue(int, Value* args, VM*)
{
    if (!isList(args[0]))
        return Value();
    return Value(static_cast<double>(std::count(asList(args[0])->items.begin(), asList(args[0])->items.end(), args[1])));
}

Value ListRemoveValue(int, Value* args, VM* vm)
{
    ObjList* result = BeginList(vm);
    ObjList* values = newList();
    result->append(Value(values));
    size_t removed = 0;
    if (isList(args[0]))
    {
        for (const Value& item : asList(args[0])->items)
        {
            if (item == args[1]) ++removed;
            else values->append(item);
        }
    }
    result->append(Value(static_cast<double>(removed)));
    return EndList(vm, result);
}

void RegisterNode(NodeRegistry& registry, const char* name, std::vector<BasicFunctionDef::Input> inputs, std::vector<BasicFunctionDef::Input> outputs,
                  NativeFn function, NodeDefinitionFlags flags, const char* description)
{
    std::vector<std::string> inputDescriptions;
    std::vector<std::string> outputDescriptions;
    inputDescriptions.reserve(inputs.size());
    outputDescriptions.reserve(outputs.size());
    for (const BasicFunctionDef::Input& input : inputs)
        inputDescriptions.push_back("The " + input.name + " input");
    for (const BasicFunctionDef::Input& output : outputs)
        outputDescriptions.push_back("The " + output.name + " result");
    std::vector<const char*> inputPointers;
    std::vector<const char*> outputPointers;
    for (const std::string& input : inputDescriptions) inputPointers.push_back(input.c_str());
    for (const std::string& output : outputDescriptions) outputPointers.push_back(output.c_str());
    registry.RegisterNativeFunc(name, std::move(inputs), std::move(outputs), function, flags, { description, std::move(inputPointers), std::move(outputPointers) });
}
}

void MarkStandardLibraryTimerRoots(VM& vm)
{
    std::lock_guard<std::mutex> lock(TimerMutex());
    for (auto& [handle, timer] : Timers())
        if (timer->owner == &vm)
            vm.markValue(timer->callback);
}

bool HasPendingStandardLibraryTimers(VM& vm)
{
    std::lock_guard<std::mutex> lock(TimerMutex());
    return std::any_of(Timers().begin(), Timers().end(), [&vm](const auto& entry) { return entry.second->owner == &vm; });
}

double SecondsUntilNextStandardLibraryTimer(VM& vm)
{
    std::lock_guard<std::mutex> lock(TimerMutex());
    const Clock::time_point now = Clock::now();
    double remaining = std::numeric_limits<double>::infinity();
    for (const auto& [handle, timer] : Timers())
        if (timer->owner == &vm)
            remaining = std::min(remaining, std::chrono::duration<double>(timer->deadline - now).count());
    return std::isfinite(remaining) ? std::max(0.0, remaining) : -1.0;
}

bool PumpStandardLibraryTimers(VM& vm)
{
    struct DueTimer
    {
        uint64_t handle;
        std::shared_ptr<TimerState> state;
        Clock::time_point scheduledDeadline;
    };

    const Clock::time_point now = Clock::now();
    std::vector<DueTimer> due;
    {
        std::lock_guard<std::mutex> lock(TimerMutex());
        for (auto& [handle, timer] : Timers())
        {
            if (timer->owner != &vm || timer->deadline > now)
                continue;

            due.push_back({ handle, timer, timer->deadline });
            if (timer->repeating)
            {
                Clock::duration interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(timer->interval));
                if (interval <= Clock::duration::zero()) interval = Clock::duration(1);
                do timer->deadline += interval;
                while (timer->deadline <= now);
            }
        }
    }

    std::sort(due.begin(), due.end(), [](const DueTimer& left, const DueTimer& right)
    {
        return left.scheduledDeadline != right.scheduledDeadline ? left.scheduledDeadline < right.scheduledDeadline : left.handle < right.handle;
    });

    for (const DueTimer& timer : due)
    {
        bool active = false;
        {
            std::lock_guard<std::mutex> lock(TimerMutex());
            const auto current = Timers().find(timer.handle);
            active = current != Timers().end() && current->second == timer.state;
            if (active && !timer.state->repeating)
                Timers().erase(current);
        }
        if (!active)
            continue;

        const InterpretResult result = ScriptRuntime::Call(vm, timer.state->callback);
        if (result != InterpretResult::INTERPRET_OK)
        {
            std::lock_guard<std::mutex> lock(TimerMutex());
            const auto current = Timers().find(timer.handle);
            if (current != Timers().end() && current->second == timer.state)
                Timers().erase(current);
            return false;
        }
    }
    return true;
}

bool RunStandardLibraryTimers(VM& vm, const std::function<bool()>& shouldStop)
{
    while (HasPendingStandardLibraryTimers(vm))
    {
        if (shouldStop && shouldStop())
        {
            ClearStandardLibraryTimers(vm);
            return true;
        }

        const double delay = SecondsUntilNextStandardLibraryTimer(vm);
        if (delay > 0.0)
            std::this_thread::sleep_for(std::chrono::duration<double>(std::min(delay, 0.01)));
        if (!PumpStandardLibraryTimers(vm))
            return false;
    }
    return true;
}

void ClearStandardLibraryTimers(VM& vm)
{
    std::lock_guard<std::mutex> lock(TimerMutex());
    for (auto timer = Timers().begin(); timer != Timers().end();)
    {
        if (timer->second->owner == &vm)
            timer = Timers().erase(timer);
        else
            ++timer;
    }
}

double StandardLibraryRandomReal(double minimum, double maximum)
{
    if (minimum > maximum)
        std::swap(minimum, maximum);
    std::lock_guard<std::mutex> lock(RandomMutex());
    return std::uniform_real_distribution<double>(minimum, maximum)(RandomGenerator());
}

void RegisterExtendedStandardLibrary(NodeRegistry& registry)
{
    const NodeDefinitionFlags pure = NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure;
    const NodeDefinitionFlags query = NodeDefinitionFlags::ReadOnly;
    const NodeDefinitionFlags effect = NodeDefinitionFlags::None;
    const Value emptyString(copyString("", 0));
    const TypeRef anyList = TypeRef::List(PinType::Any);
    const TypeRef stringList = TypeRef::List(PinType::String);
    const TypeRef byteList = TypeRef::List(PinType::Int);
    const TypeRef timerCallback = TypeRef::Function({}, {});
    const TypeRef jsonValue = TypeRef::Object(JsonValueClassName);
    const TypeRef jsonArray = TypeRef::List(jsonValue);
    const TypeRef jsonObject = TypeRef::Map(PinType::String, jsonValue);

    registry.RegisterNativeClass(JsonValueClassName,
    {
        { "init", 0, &JsonValueInit },
        { "kind", 0, &JsonValueKindMethod },
        { "isNull", 0, &JsonValueIsNullMethod },
        { "toNative", 0, &JsonValueToNativeMethod },
        { "toString", 0, &JsonValueToStringMethod }
    });

    RegisterNode(registry, "JSON::Parse", { { "Text", emptyString } },
        { { "Value", Value(), -1, jsonValue }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonParse, pure, "Parses text into a typed JsonValue");
    RegisterNode(registry, "JSON::Stringify", { { "Value", Value(), -1, jsonValue } },
        { { "Text", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonStringify, pure, "Serializes a JsonValue as compact JSON text");
    RegisterNode(registry, "JSON::Pretty Print", { { "Value", Value(), -1, jsonValue }, { "Indent", Value(2.0) } },
        { { "Text", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonPrettyPrint, pure, "Serializes a JsonValue as indented JSON text");
    RegisterNode(registry, "JSON::Kind", { { "Value", Value(), -1, jsonValue } }, { { "Kind", emptyString } }, &JsonKind, pure,
        "Returns Object, Array, String, Number, Boolean, Null, or Invalid");
    RegisterNode(registry, "JSON::Is Null", { { "Value", Value(), -1, jsonValue } }, { { "Is Null", Value(false) } }, &JsonIsNull, pure,
        "Checks whether a JsonValue contains JSON null");
    RegisterNode(registry, "JSON::From Native", { { "Value", Value() } },
        { { "JSON", Value(), -1, jsonValue }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonFromNative, pure,
        "Copies nil, a boolean, number, string, list, or string-keyed map into a JsonValue");
    RegisterNode(registry, "JSON::To Native", { { "JSON", Value(), -1, jsonValue } },
        { { "Value", Value() }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonToNative, pure,
        "Copies a JsonValue into ordinary Vlox values");
    RegisterNode(registry, "JSON::As String", { { "Value", Value(), -1, jsonValue } },
        { { "String", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonAsString, pure, "Reads a JSON string without returning Any");
    RegisterNode(registry, "JSON::As Number", { { "Value", Value(), -1, jsonValue } },
        { { "Number", Value(0.0) }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonAsNumber, pure, "Reads a JSON number without returning Any");
    RegisterNode(registry, "JSON::As Boolean", { { "Value", Value(), -1, jsonValue } },
        { { "Boolean", Value(false) }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonAsBoolean, pure, "Reads a JSON boolean without returning Any");
    RegisterNode(registry, "JSON::As Array", { { "Value", Value(), -1, jsonValue } },
        { { "Array", Value(newList()), -1, jsonArray }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonAsArray, pure,
        "Reads a JSON array as a typed list of JsonValue elements");
    RegisterNode(registry, "JSON::As Object", { { "Value", Value(), -1, jsonValue } },
        { { "Object", Value(newMap()), -1, jsonObject }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonAsObject, pure,
        "Reads a JSON object as a typed string-to-JsonValue map");
    RegisterNode(registry, "JSON::Get", { { "Object", Value(), -1, jsonValue }, { "Key", emptyString } },
        { { "Value", Value(), -1, jsonValue }, { "Found", Value(false) }, { "Error", emptyString } }, &JsonGet, pure,
        "Reads a named member from a JSON object");
    RegisterNode(registry, "JSON::Object To Entries", { { "Object", Value(), -1, jsonValue } }, { { "Entries", Value(newList()) } },
        &JsonObjectToEntries, pure, "Maps a JsonValue object to string and JsonValue pairs");
    RegisterNode(registry, "JSON::Entries To Object", { { "Entries", Value(newList()) } },
        { { "Object", Value(), -1, jsonValue }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonEntriesToObject, pure,
        "Builds a JsonValue object from string and JsonValue pairs");
    RegisterNode(registry, "JSON::Read File", { { "Path", emptyString } },
        { { "Value", Value(), -1, jsonValue }, { "Success", Value(false) }, { "Error", emptyString } }, &JsonReadFile, effect, "Reads and parses a JsonValue from a file");
    RegisterNode(registry, "JSON::Write File",
        { { "Path", emptyString }, { "Value", Value(), -1, jsonValue }, { "Pretty", Value(true) }, { "Indent", Value(2.0) }, { "Overwrite", Value(false) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &JsonWriteFile, effect, "Serializes a JsonValue directly to a JSON file");

    RegisterNode(registry, "File::Read Bytes", { { "Path", emptyString } },
        { { "Bytes", Value(newList()), -1, byteList }, { "Success", Value(false) }, { "Error", emptyString } }, &FileReadBytes, effect, "Reads every byte from a binary file");
    RegisterNode(registry, "File::Write Bytes", { { "Path", emptyString }, { "Bytes", Value(newList()), -1, byteList }, { "Overwrite", Value(false) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &FileWriteBytes, effect, "Writes bytes to a file with explicit overwrite behavior");
    RegisterNode(registry, "File::Read Text Encoded", { { "Path", emptyString }, { "Encoding", Value(copyString("utf-8", 5)) } },
        { { "Text", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &FileReadTextEncoded, effect, "Reads a text file using an explicit encoding");
    RegisterNode(registry, "File::Write Text Encoded",
        { { "Path", emptyString }, { "Text", emptyString }, { "Encoding", Value(copyString("utf-8", 5)) }, { "Overwrite", Value(false) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &FileWriteTextEncoded, effect, "Writes a text file using explicit encoding and overwrite behavior");
    RegisterNode(registry, "File::Copy", { { "Source", emptyString }, { "Destination", emptyString }, { "Overwrite", Value(false) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &FileCopy, effect, "Copies a file or directory to another path");
    RegisterNode(registry, "File::Move", { { "Source", emptyString }, { "Destination", emptyString }, { "Overwrite", Value(false) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &FileMove, effect, "Moves a file or directory to another path");
    RegisterNode(registry, "File::Rename", { { "Source", emptyString }, { "Destination", emptyString }, { "Overwrite", Value(false) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &FileMove, effect, "Renames a file or directory with explicit overwrite behavior");
    RegisterNode(registry, "File::Delete", { { "Path", emptyString }, { "Recursive", Value(false) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &FileDelete, effect, "Deletes a file or optionally a directory tree");
    RegisterNode(registry, "Directory::Create", { { "Path", emptyString }, { "Recursive", Value(true) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &DirectoryCreate, effect, "Creates a directory and optionally missing parents");
    RegisterNode(registry, "Directory::Remove", { { "Path", emptyString }, { "Recursive", Value(false) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &DirectoryRemove, effect, "Removes an empty directory or an entire directory tree");
    RegisterNode(registry, "File::Metadata", { { "Path", emptyString } },
        { { "Exists", Value(false) }, { "Is File", Value(false) }, { "Is Directory", Value(false) }, { "Size", Value(0.0) }, { "Modified", Value(0.0) },
          { "Success", Value(false) }, { "Error", emptyString } }, &FileMetadata, effect, "Reads file type, byte size, and modified time metadata");

    RegisterNode(registry, "Path::Current", {}, { { "Path", emptyString }, { "Success", Value(false) }, { "Error", emptyString } },
        &PathCurrent, effect, "Returns the process current working directory");
    RegisterNode(registry, "Path::Absolute", { { "Path", emptyString } },
        { { "Path", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &PathAbsolute, effect, "Resolves a path against the current working directory");
    RegisterNode(registry, "Path::Canonical", { { "Path", emptyString } },
        { { "Path", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &PathCanonical, effect, "Resolves a path through existing symbolic links and dot segments");
    RegisterNode(registry, "Path::Stem", { { "Path", emptyString } }, { { "Stem", emptyString } }, &PathStem, pure, "Returns a filename without its final extension");
    RegisterNode(registry, "Path::Replace Filename", { { "Path", emptyString }, { "Filename", emptyString } }, { { "Path", emptyString } },
        &PathReplaceFilename, pure, "Replaces the filename portion of a path");
    RegisterNode(registry, "Path::Relative", { { "Path", emptyString }, { "Base", emptyString } },
        { { "Path", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &PathRelative, effect, "Computes a path relative to a base directory");

    RegisterNode(registry, "Environment::Get", { { "Name", emptyString } }, { { "Value", emptyString }, { "Found", Value(false) } },
        &EnvironmentGet, effect, "Reads an environment variable without changing the process");
    RegisterNode(registry, "Environment::Set", { { "Name", emptyString }, { "Value", emptyString } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &EnvironmentSet, effect, "Sets an environment variable for the current process");
    RegisterNode(registry, "Environment::Unset", { { "Name", emptyString } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &EnvironmentUnset, effect, "Removes an environment variable from the current process");
    RegisterNode(registry, "Environment::Variables", {}, { { "Variables", Value(newMap()), -1, TypeRef::Map(PinType::String, PinType::String) } },
        &EnvironmentVariables, effect, "Returns a snapshot of the current process environment");

    const std::vector<BasicFunctionDef::Input> processInputs{
        { "Executable", emptyString }, { "Arguments", Value(newList()), -1, stringList }, { "Working Directory", emptyString },
        { "Environment", Value(newMap()), -1, TypeRef::Map(PinType::String, PinType::String) }, { "Timeout", Value(0.0) }
    };
    const std::vector<BasicFunctionDef::Input> processOutputs{
        { "Exit Code", Value(-1.0) }, { "Stdout", emptyString }, { "Stderr", emptyString }, { "Timed Out", Value(false) },
        { "Cancelled", Value(false) }, { "Success", Value(false) }, { "Error", emptyString }
    };
    RegisterNode(registry, "Process::Run", processInputs, processOutputs, &ProcessRun, effect, "Runs an executable synchronously with captured output and a timeout");
    RegisterNode(registry, "System::RunCommand", processInputs, processOutputs, &ProcessRun, effect, "Runs an executable with explicit arguments, environment, output capture, and timeout");
    registry.nativeDefinitions.back().functionDef->revision = 2;
    RegisterNode(registry, "Process::Start", processInputs,
        { { "Handle", Value(0.0) }, { "Success", Value(false) }, { "Error", emptyString } }, &ProcessStart, effect, "Starts an executable asynchronously and returns a process handle");
    RegisterNode(registry, "Process::Poll", { { "Handle", Value(0.0) } },
        { { "Finished", Value(false) }, { "Exit Code", Value(-1.0) }, { "Stdout", emptyString }, { "Stderr", emptyString }, { "Timed Out", Value(false) },
          { "Cancelled", Value(false) }, { "Success", Value(false) }, { "Error", emptyString } }, &ProcessPoll, effect, "Polls an asynchronous process without blocking");
    RegisterNode(registry, "Process::Cancel", { { "Handle", Value(0.0) } },
        { { "Success", Value(false) }, { "Error", emptyString } }, &ProcessCancel, effect, "Requests cancellation of an asynchronous process");

    RegisterNode(registry, "Regex::Match", { { "Text", emptyString }, { "Pattern", emptyString } },
        { { "Matched", Value(false) }, { "Error", emptyString } }, &RegexMatch, pure, "Tests whether an entire string matches a regular expression");
    RegisterNode(registry, "Regex::Search", { { "Text", emptyString }, { "Pattern", emptyString } },
        { { "Found", Value(false) }, { "Match", emptyString }, { "Index", Value(-1.0) }, { "Captures", Value(newList()), -1, stringList }, { "Error", emptyString } },
        &RegexSearch, pure, "Finds the first regular expression match and capture groups");
    RegisterNode(registry, "Regex::Replace", { { "Text", emptyString }, { "Pattern", emptyString }, { "Replacement", emptyString } },
        { { "Text", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &RegexReplace, pure, "Replaces regular expression matches in text");
    RegisterNode(registry, "Regex::Split", { { "Text", emptyString }, { "Pattern", emptyString } },
        { { "Parts", Value(newList()), -1, stringList }, { "Success", Value(false) }, { "Error", emptyString } }, &RegexSplit, pure, "Splits text around regular expression matches");

    RegisterNode(registry, "String::Unicode Length", { { "Text", emptyString } }, { { "Length", Value(0.0) } }, &StringUnicodeLength, pure, "Counts Unicode code points in UTF-8 text");
    RegisterNode(registry, "String::Unicode Substring", { { "Text", emptyString }, { "Start", Value(0.0) }, { "Count", Value(0.0) } },
        { { "Text", emptyString } }, &StringUnicodeSubstring, pure, "Extracts UTF-8 text by Unicode code point position");
    RegisterNode(registry, "String::Unicode Lower", { { "Text", emptyString } }, { { "Text", emptyString } }, &StringUnicodeLower, pure,
        "Converts Unicode code points to lowercase where supported by the platform");
    RegisterNode(registry, "String::Unicode Upper", { { "Text", emptyString } }, { { "Text", emptyString } }, &StringUnicodeUpper, pure,
        "Converts Unicode code points to uppercase where supported by the platform");
    RegisterNode(registry, "String::Pad Left", { { "Text", emptyString }, { "Length", Value(0.0) }, { "Padding", Value(copyString(" ", 1)) } },
        { { "Text", emptyString } }, &StringPadLeft, pure, "Pads the left side of text to a Unicode length");
    RegisterNode(registry, "String::Pad Right", { { "Text", emptyString }, { "Length", Value(0.0) }, { "Padding", Value(copyString(" ", 1)) } },
        { { "Text", emptyString } }, &StringPadRight, pure, "Pads the right side of text to a Unicode length");
    RegisterNode(registry, "String::Repeat", { { "Text", emptyString }, { "Count", Value(0.0) } }, { { "Text", emptyString } },
        &StringRepeat, pure, "Repeats text a requested number of times");
    RegisterNode(registry, "String::Count", { { "Text", emptyString }, { "Search", emptyString } }, { { "Count", Value(0.0) } },
        &StringCount, pure, "Counts non-overlapping occurrences of text");
    RegisterNode(registry, "String::Lines", { { "Text", emptyString } }, { { "Lines", Value(newList()), -1, stringList } },
        &StringLines, pure, "Splits text using Unix, Windows, or classic line endings");
    RegisterNode(registry, "String::Classify Character", { { "Character", emptyString } },
        { { "Is Letter", Value(false) }, { "Is Digit", Value(false) }, { "Is Space", Value(false) }, { "Is Upper", Value(false) }, { "Is Lower", Value(false) }, { "Valid", Value(false) } },
        &StringCharacterClassification, pure, "Classifies one Unicode character using platform Unicode tables");

    RegisterNode(registry, "Encoding::Base64 Encode", { { "Text", emptyString } }, { { "Base64", emptyString } }, &EncodingBase64Encode, pure,
        "Encodes UTF-8 text bytes as base64");
    RegisterNode(registry, "Encoding::Base64 Decode", { { "Base64", emptyString } },
        { { "Text", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &EncodingBase64Decode, pure, "Decodes base64 into its original bytes held in a string");
    RegisterNode(registry, "Encoding::Text To Bytes", { { "Text", emptyString }, { "Encoding", Value(copyString("utf-8", 5)) } },
        { { "Bytes", Value(newList()), -1, byteList }, { "Success", Value(false) }, { "Error", emptyString } }, &EncodingTextToBytes, pure, "Encodes text as a list of bytes");
    RegisterNode(registry, "Encoding::Bytes To Text", { { "Bytes", Value(newList()), -1, byteList }, { "Encoding", Value(copyString("utf-8", 5)) } },
        { { "Text", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &EncodingBytesToText, pure, "Decodes a list of bytes as text");

    RegisterNode(registry, "Time::Now", {}, { { "Unix Seconds", Value(0.0) } }, &TimeNow, effect, "Returns wall-clock time as Unix seconds");
    RegisterNode(registry, "Time::Monotonic Now", {}, { { "Seconds", Value(0.0) } }, &TimeMonotonicNow, effect, "Returns a monotonic clock suitable for measuring elapsed time");
    RegisterNode(registry, "Time::Parse", { { "Text", emptyString }, { "Format", Value(copyString("%Y-%m-%d %H:%M:%S", 17)) }, { "UTC", Value(false) } },
        { { "Unix Seconds", Value(0.0) }, { "Success", Value(false) }, { "Error", emptyString } }, &TimeParse, effect, "Parses a date and time using a strftime-compatible format");
    RegisterNode(registry, "Time::Format", { { "Unix Seconds", Value(0.0) }, { "Format", Value(copyString("%Y-%m-%d %H:%M:%S", 17)) }, { "UTC", Value(false) } },
        { { "Text", emptyString }, { "Success", Value(false) }, { "Error", emptyString } }, &TimeFormat, effect, "Formats a Unix timestamp in local time or UTC");
    RegisterNode(registry, "Duration::From Milliseconds", { { "Milliseconds", Value(0.0) } }, { { "Seconds", Value(0.0) } },
        &DurationFromMilliseconds, pure, "Converts milliseconds to the duration representation in seconds");
    RegisterNode(registry, "Duration::To Milliseconds", { { "Seconds", Value(0.0) } }, { { "Milliseconds", Value(0.0) } },
        &DurationToMilliseconds, pure, "Converts a duration in seconds to milliseconds");
    RegisterNode(registry, "Duration::From Parts", { { "Days", Value(0.0) }, { "Hours", Value(0.0) }, { "Minutes", Value(0.0) }, { "Seconds", Value(0.0) } },
        { { "Duration", Value(0.0) } }, &DurationFromParts, pure, "Combines day, hour, minute, and second components into seconds");
    RegisterNode(registry, "Timer::After", { { "Duration", Value(0.0) }, { "Callback", Value(newFunction()), -1, timerCallback } }, { { "Handle", Value(0.0) } },
        &TimerAfter, effect, "Calls a function once after a monotonic-clock duration without blocking the VM thread");
    RegisterNode(registry, "Timer::Every", { { "Interval", Value(1.0) }, { "Callback", Value(newFunction()), -1, timerCallback } }, { { "Handle", Value(0.0) } },
        &TimerEvery, effect, "Calls a function repeatedly at a monotonic-clock interval without blocking the VM thread");
    RegisterNode(registry, "Timer::Cancel", { { "Handle", Value(0.0) } }, { { "Success", Value(false) }, { "Error", emptyString } },
        &TimerCancel, effect, "Cancels a pending or repeating callback timer");

    RegisterNode(registry, "Math::Pi", {}, { { "Value", Value(0.0) } }, &MathPi, pure, "Returns the mathematical constant pi");
    RegisterNode(registry, "Math::E", {}, { { "Value", Value(0.0) } }, &MathE, pure, "Returns Euler's mathematical constant");
    RegisterNode(registry, "Math::Tau", {}, { { "Value", Value(0.0) } }, &MathTau, pure, "Returns the circle constant equal to two pi");
    RegisterNode(registry, "Math::Sin", { { "Radians", Value(0.0) } }, { { "Value", Value(0.0) } }, &MathSin, pure, "Returns the sine of an angle in radians");
    RegisterNode(registry, "Math::Cos", { { "Radians", Value(0.0) } }, { { "Value", Value(0.0) } }, &MathCos, pure, "Returns the cosine of an angle in radians");
    RegisterNode(registry, "Math::Tan", { { "Radians", Value(0.0) } }, { { "Value", Value(0.0) } }, &MathTan, pure, "Returns the tangent of an angle in radians");
    RegisterNode(registry, "Math::Asin", { { "Value", Value(0.0) } }, { { "Radians", Value(0.0) } }, &MathAsin, pure, "Returns the inverse sine in radians");
    RegisterNode(registry, "Math::Acos", { { "Value", Value(0.0) } }, { { "Radians", Value(0.0) } }, &MathAcos, pure, "Returns the inverse cosine in radians");
    RegisterNode(registry, "Math::Atan", { { "Value", Value(0.0) } }, { { "Radians", Value(0.0) } }, &MathAtan, pure, "Returns the inverse tangent in radians");
    RegisterNode(registry, "Math::Atan2", { { "Y", Value(0.0) }, { "X", Value(0.0) } }, { { "Radians", Value(0.0) } },
        &MathAtan2, pure, "Returns the quadrant-aware inverse tangent in radians");
    RegisterNode(registry, "Math::Log", { { "Value", Value(1.0) } }, { { "Result", Value(0.0) } }, &MathLog, pure, "Returns the natural logarithm of a positive number");
    RegisterNode(registry, "Math::Log10", { { "Value", Value(1.0) } }, { { "Result", Value(0.0) } }, &MathLog10, pure, "Returns the base-ten logarithm of a positive number");
    RegisterNode(registry, "Math::Exp", { { "Value", Value(0.0) } }, { { "Result", Value(1.0) } }, &MathExp, pure, "Raises Euler's constant to a power");
    RegisterNode(registry, "Math::Random Seed", { { "Seed", Value(0.0) } }, { { "Success", Value(false) } }, &RandomSeed, effect, "Seeds the standard-library pseudo-random generator");
    RegisterNode(registry, "Math::Random Integer", { { "Min", Value(0.0) }, { "Max", Value(1.0) } }, { { "Value", Value(0.0) } },
        &RandomInteger, effect, "Returns a uniformly distributed integer within inclusive bounds");
    RegisterNode(registry, "List::Random Choice", { { "List", Value(newList()), -1, anyList } }, { { "Value", Value() } },
        &RandomChoice, effect, "Returns one uniformly selected list value");
    RegisterNode(registry, "List::Shuffle", { { "List", Value(newList()), -1, anyList } }, { { "List", Value(newList()), -1, anyList } },
        &RandomShuffle, effect, "Returns a shuffled copy of a list");

    const TypeRef predicate = TypeRef::Function({ TypeRef::Variable("T") }, { PinType::Bool });
    const std::vector<BasicFunctionDef::Input> predicateInputs{
        { "Iterable", Value(newList()), -1, TypeRef::Iterable(TypeRef::Variable("T")) }, { "Predicate", Value(newFunction()), -1, predicate }
    };
    RegisterNode(registry, "Functional::Any", predicateInputs, { { "Result", Value(false) } }, &FunctionalAny, query, "Returns true when a predicate accepts any iterable value");
    RegisterNode(registry, "Functional::All", predicateInputs, { { "Result", Value(false) } }, &FunctionalAll, query,
        "Returns true when a predicate accepts every iterable value");
    RegisterNode(registry, "Functional::Count", predicateInputs, { { "Count", Value(0.0) } }, &FunctionalCount, query, "Counts iterable values accepted by a predicate");
    RegisterNode(registry, "Functional::Remove", predicateInputs, { { "Result", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) } },
        &FunctionalRemove, query, "Returns values not accepted by a removal predicate");
    RegisterNode(registry, "Functional::Find", predicateInputs,
        { { "Value", Value(), -1, TypeRef::Variable("T") }, { "Found", Value(false) } }, &FunctionalFind, query, "Finds the first iterable value accepted by a predicate");
    RegisterNode(registry, "Functional::Group By",
        { { "Iterable", Value(newList()), -1, TypeRef::Iterable(TypeRef::Variable("T")) },
          { "Selector", Value(newFunction()), -1, TypeRef::Function({ TypeRef::Variable("T") }, { TypeRef::Variable("K") }) } },
        { { "Groups", Value(newMap()), -1, TypeRef::Map(TypeRef::Variable("K"), TypeRef::List(TypeRef::Variable("T"))) } },
        &FunctionalGroupBy, query, "Groups iterable values by keys returned from a selector");
    RegisterNode(registry, "List::Flatten", { { "Lists", Value(newList()), -1, TypeRef::List(TypeRef::List(TypeRef::Variable("T"))) } },
        { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) } }, &ListFlatten, pure, "Flattens one level of nested lists");
    RegisterNode(registry, "List::Chunk", { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) }, { "Size", Value(1.0) } },
        { { "Chunks", Value(newList()), -1, TypeRef::List(TypeRef::List(TypeRef::Variable("T"))) } }, &ListChunk, pure, "Partitions a list into fixed-size chunks");
    RegisterNode(registry, "List::Take", { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) }, { "Count", Value(0.0) } },
        { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) } }, &ListTake, pure, "Returns up to the first requested values from a list");
    RegisterNode(registry, "List::Skip", { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) }, { "Count", Value(0.0) } },
        { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) } }, &ListSkip, pure, "Returns list values after skipping a requested prefix");
    RegisterNode(registry, "List::Count Value",
        { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) }, { "Value", Value(), -1, TypeRef::Variable("T") } },
        { { "Count", Value(0.0) } }, &ListCountValue, pure, "Counts list values equal to a requested value");
    RegisterNode(registry, "List::Remove Value",
        { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) }, { "Value", Value(), -1, TypeRef::Variable("T") } },
        { { "List", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) }, { "Removed", Value(0.0) } },
        &ListRemoveValue, pure, "Returns a copy without values equal to a requested value");
}
