#include "Value.h"

#include <iostream>

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

ObjString* valueAsString(const Value& value)
{
    switch (value.type)
    {
    case ValueType::BOOL: return (asBoolean(value) ? takeString("true", 4) : takeString("false", 5));
    case ValueType::NIL: return takeString("nil", 3);
    case ValueType::NUMBER: return takeString(std::to_string(asNumber(value)));
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
