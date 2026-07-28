#include "Value.h"

#include <iostream>
#include <charconv>
#include <array>
#include <string_view>

#include "Object.h"

void printValue(const Value& value)
{
    switch (value.type)
    {
    case ValueType::BOOL:
        std::cout << (asBoolean(value) ? "true" : "false");
        break;
    case ValueType::NIL: std::cout << "nil"; break;
    case ValueType::NUMBER: std::cout << asNumber(value); break;
    case ValueType::OBJ: printObject(value); break;
    }
}

// Fast double to string conversion
std::string_view doubleToString(double value, std::array<char, 24>& buffer)
{
    auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general);

    if (ec == std::errc())
    {
        return std::string_view(buffer.data(), ptr - buffer.data());
    }
    return "Error";
}

ObjString* valueAsString(const Value& value)
{
    switch (value.type)
    {
    case ValueType::BOOL: return (asBoolean(value) ? takeString("true", 4) : takeString("false", 5));
    case ValueType::NIL: return takeString("nil", 3);
    case ValueType::NUMBER:
    {
        std::array<char, 24> buffer;
        return takeString(doubleToString(asNumber(value), buffer));
    }
    case ValueType::OBJ: return objectAsString(value); break;
    }

    return takeString("<Unknown>", 9);
}

std::string valueAsStr(const Value& value)
{
    switch (value.type)
    {
    case ValueType::BOOL: return (asBoolean(value) ? "true" : "false");
    case ValueType::NIL: return "nil";
    case ValueType::NUMBER: return std::to_string(asNumber(value));
    case ValueType::OBJ: return objectAsStr(value); break;
    }

    return "<Unknown>";
}

size_t sizeOf(const Value& value)
{
    switch (value.type)
    {
    case ValueType::BOOL:
    case ValueType::NIL:
    case ValueType::NUMBER:
        return sizeof(Value);
    case ValueType::OBJ:
        return sizeof(Value) + sizeOfObject(value);
    }
    return 0;
}

bool Value::operator==(const Value& other) const
{
    if (type != other.type) return false;
    switch (type)
    {
        case ValueType::BOOL:   return asBoolean(*this) == asBoolean(other);
        case ValueType::NIL:    return true;
        case ValueType::NUMBER: return asNumber(*this) == asNumber(other);
        case ValueType::OBJ:
        {
            return asObject(*this) == asObject(other);
        }
        default:                return false; // Unreachable.
    }
}
