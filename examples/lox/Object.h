#ifndef loxcpp_object_h
#define loxcpp_object_h

#include <string>
#include <iostream>

#include "Common.h"
#include "Chunk.h"
#include "Value.h"
#include "HashTable.h"

class VM;

enum class ObjType
{
    STRING = 0,
    NATIVE,
    UPVALUE,
    FUNCTION,
    CLOSURE,
    BOUND_METHOD,
    CLASS,
    INSTANCE,
    RANGE,
    LIST,

    COUNT
};

inline const char* objTypeToString(const ObjType type)
{
    switch (type)
    {
    case ObjType::STRING: return "STRING";
    case ObjType::NATIVE: return "NATIVE";
    case ObjType::UPVALUE: return "UPVALUE";
    case ObjType::FUNCTION: return "FUNCTION";
    case ObjType::CLOSURE: return "CLOSURE";
    case ObjType::BOUND_METHOD: return "BOUND_METHOD";
    case ObjType::CLASS: return "CLASS";
    case ObjType::INSTANCE: return "INSTANCE";
    case ObjType::RANGE: return "RANGE";
    case ObjType::LIST: return "LIST";
    }
    return "UNKNOWN";
    static_assert(static_cast<int>(ObjType::COUNT) == 10, "Missing enum value");
}

struct Obj
{
    Obj(ObjType type)
        : type(type)
        , hash(0)
        , isMarked(false)
    {}

    virtual ~Obj() {}

    ObjType type;
    uint32_t hash;
    bool isMarked;
};

struct ObjString : Obj
{
    ObjString(const char* chars, int length)
        : Obj(ObjType::STRING)
        , length(length)
        , chars(chars, length)
    {
#ifdef DEBUG_OBJECT_LIFETIME
        std::cout << "STRING created: " << this->chars << std::endl;
#endif
    }
    ObjString(std::string&& str)
        : Obj(ObjType::STRING)
        , length(static_cast<int>(str.length()))
        , chars(std::move(str))
    {
#ifdef DEBUG_OBJECT_LIFETIME
        std::cout << "STRING created: " << this->chars << std::endl;
#endif
    }
    ~ObjString()
    {
#ifdef DEBUG_OBJECT_LIFETIME
        std::cout << "STRING destroyed: " << this->chars << std::endl;
#endif
    }
    int length;
    std::string chars;
};

struct ObjFunction : Obj
{
    ObjFunction(int arity, const Chunk& chunk, ObjString* name)
        : Obj(ObjType::FUNCTION)
        , arity(arity)
        , upvalueCount(0)
        , chunk(chunk)
        , name(name)
    {}

    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name;
};

struct ObjUpvalue : Obj
{
    ObjUpvalue(Value* location)
        : Obj(ObjType::UPVALUE)
        , location(location)
        , closed()
        , next(nullptr)
    {}

    Value* location;
    Value closed;
    ObjUpvalue* next;
};

struct ObjClosure : Obj
{
    ObjClosure(ObjFunction* function)
        : Obj(ObjType::CLOSURE)
        , function(function)
        , upvalues(function->upvalueCount, nullptr)
    {}

    ObjFunction* function;
    std::vector<ObjUpvalue*> upvalues;
};

struct ObjClass : Obj
{
    ObjClass(ObjString* name)
        : Obj(ObjType::CLASS)
        , name(name)
        , initializer()
    {}

    ObjString* name;
    Table methods;
    Value initializer;
};

struct ObjInstance : Obj
{
    ObjInstance(ObjClass* klass)
        : Obj(ObjType::INSTANCE)
        , klass(klass)
    {}

    ObjClass* klass;
    Table fields;
};

struct ObjBoundMethod  : Obj
{
    ObjBoundMethod(const Value& receiver, Value& method)
        : Obj(ObjType::BOUND_METHOD)
        , receiver(receiver)
        , method(method)
    {}

    Value receiver;
    Value method;
};

using NativeFn = Value(*)(int argCount, Value* args, VM* vm);

struct ObjNative : Obj
{
    ObjNative(uint8_t arity, NativeFn function, bool isMethod)
        : Obj(ObjType::NATIVE)
        , function(function)
        , arity(arity)
        , isMethod(isMethod)
    {}

    NativeFn function;
    uint8_t arity;
    bool isMethod;
};

struct ObjRange : Obj
{
    ObjRange(double min, double max)
        : Obj(ObjType::RANGE)
        , min(min)
        , max(max)
    {}

    bool contains(double value)
    {
        if (min < max) return value >= min && value<= max; // 1..5
        return value <= min && value >= max; // 5..1
    }

    bool isInBounds(int idx)
    {
        if (min < max) return idx >= 0 && idx <= max - min; // 1..5
        return idx >= 0 && idx <= min - max; // 5..1
    }

    double getValue(int idx)
    {
        if (min < max) return min + idx; // 1..5
        return min - idx; // 5..1
    }

    double min;
    double max;
};

struct ObjList : Obj
{
    ObjList()
        : Obj(ObjType::LIST)
    {}

    void append(Value value)
    {
        items.push_back(value);
    }

    void setValue(int index, Value value)
    {
        items[index] = value;
    }

    Value getValue(int index)
    {
        return items[index];
    }

    void deleteValue(int index)
    {
        items.erase(items.begin() + index);
    }

    bool isInBounds(int index)
    {
        return index >= 0 && index < items.size();
    }

    std::vector<Value> items;
};

inline ObjType getObjType(const Value& value) { return asObject(value)->type; }
inline bool isObjType(const Value& value, const ObjType type)
{
    return isObject(value) && asObject(value)->type == type;
}
inline bool isString(const Value& value) { return isObjType(value, ObjType::STRING); }
inline bool isInstance(const Value& value) { return isObjType(value, ObjType::INSTANCE); }
inline bool isBoundMethod(const Value& value) { return isObjType(value, ObjType::BOUND_METHOD); }
inline bool isClass(const Value& value) { return isObjType(value, ObjType::CLASS); }
inline bool isClosure(const Value& value) { return isObjType(value, ObjType::CLOSURE); }
inline bool isFunction(const Value& value) { return isObjType(value, ObjType::FUNCTION); }
inline bool isNative(const Value& value) { return isObjType(value, ObjType::NATIVE); }
inline bool isRange(const Value& value) { return isObjType(value, ObjType::RANGE); }
inline bool isList(const Value& value) { return isObjType(value, ObjType::LIST); }

inline const char* asCString(const Value& value) { return static_cast<ObjString*>(asObject(value))->chars.c_str(); }

inline ObjString* asString(const Value& value) { return static_cast<ObjString*>(asObject(value)); }
inline ObjInstance* asInstance(const Value& value) { return static_cast<ObjInstance*>(asObject(value)); }
inline ObjBoundMethod* asBoundMethod(const Value& value) { return static_cast<ObjBoundMethod*>(asObject(value)); }
inline ObjClass* asClass(const Value& value) { return static_cast<ObjClass*>(asObject(value)); }
inline ObjClosure* asClosure(const Value& value) { return static_cast<ObjClosure*>(asObject(value)); }
inline ObjFunction* asFunction(const Value& value) { return static_cast<ObjFunction*>(asObject(value)); }
inline ObjNative* asNative(const Value& value) { return static_cast<ObjNative*>(asObject(value)); }
inline ObjRange* asRange(const Value& value) { return static_cast<ObjRange*>(asObject(value)); }
inline ObjList* asList(const Value& value) { return static_cast<ObjList*>(asObject(value)); }

ObjString* copyString(const char* chars, int length);
ObjString* takeString(const char* chars, int length);
ObjString* takeString(std::string&& chars);

ObjUpvalue* newUpvalue(Value* slot);
ObjInstance* newInstance(ObjClass* klass);
ObjBoundMethod* newBoundMethod(const Value& receiver, Value& method);
ObjClass* newClass(ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjNative* newNative(uint8_t arity, NativeFn function, bool isMethod);

ObjRange* newRange(double min, double max);
ObjList* newList();

void printObject(const Value& value);
size_t sizeOfObject(const Value& value);

ObjString* objectAsString(const Value& value);
ObjString* concatenate(ObjString* a, ObjString* b);

#endif