#ifndef loxcpp_value_h
#define loxcpp_value_h

#include <vector>
#include <string>

#include "Common.h"

enum class ValueType : uint8_t
{
    BOOL,
    NIL,
    NUMBER,
    OBJ
};

struct Obj;
struct ObjString;

union TypeUnion
{
    TypeUnion()
        : boolean(false)
    {}

    TypeUnion(bool value)
        : boolean(value)
    {}

    TypeUnion(double value)
        : number(value)
    {}

    TypeUnion(Obj* obj)
        : obj(obj)
    {}

    bool boolean;
    double number;
    Obj* obj;
};

struct Value
{
    Value()
        : type(ValueType::NIL)
    {}

    explicit Value(bool value)
        : type(ValueType::BOOL)
        , as(value)
    {}

    explicit Value(double value)
        : type(ValueType::NUMBER)
        , as(value)
    {}

    explicit Value(Obj* obj)
        : type(ValueType::OBJ)
        , as(obj)
    {}

    TypeUnion as;
    ValueType type;

    bool operator==(const Value& other) const;
};

inline bool asBoolean(const Value& value) { return value.as.boolean; }
inline double asNumber(const Value& value) { return value.as.number; }
inline Obj* asObject(const Value& value) { return value.as.obj; }

inline bool isBoolean(const Value& value) { return value.type == ValueType::BOOL; }
inline bool isNumber(const Value& value) { return value.type == ValueType::NUMBER; }
inline bool isObject(const Value& value) { return value.type == ValueType::OBJ; }
inline bool isNil(const Value& value) { return value.type == ValueType::NIL; }

struct ValueArray 
{
    std::vector<Value> values;
};

void printValue(const Value& value);
ObjString* valueAsString(const Value& value);
size_t sizeOf(const Value& value);

#endif
