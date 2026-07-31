#include "Object.h"

#include <iostream>
#include <string_view>
#include <functional>

#include "Memory.h"
#include "VM.h"

template<class T, class... Args>
T* allocate(Args&&... args)
{
    T* obj = new T(std::forward<Args>(args)...);
#ifdef DEBUG_LOG_GC
    std::cout << obj << " allocate " << sizeof(*obj) << " for " << objTypeToString(obj->type) << std::endl;
#endif
#ifdef DEBUG_STRESS_GC
    VM::getInstance().collectGarbage();
#endif
    VM::getInstance().addObject(obj, sizeof(T));
    return obj;
}


uint32_t hashString(const char* key, int length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++)
    {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* allocateString(const char* chars, int length, uint32_t hash)
{
    // TODO: the std::string allocates memory in the heap out of our control, it can be improved!
    ObjString* string = allocate<ObjString>(chars, length);
    string->hash = hash;
    VM::getInstance().push(Value(string));
    VM::getInstance().stringTable().set(string, Value());
    VM::getInstance().pop();
    return string;
}

ObjString* copyString(const char* chars, int length)
{
    const uint32_t hash = hashString(chars, length);
    ObjString* interned = VM::getInstance().stringTable().findString(chars, length, hash);
    if (interned != nullptr) return interned;

    return allocateString(chars, length, hash);
}

ObjString* takeString(const char* chars, int length)
{
    const uint32_t hash = hashString(chars, length);
    ObjString* interned = VM::getInstance().stringTable().findString(chars, length, hash);
    if (interned != nullptr) return interned;

    return allocateString(chars, length, hash);
}

ObjString* takeString(const char* chars)
{
    return takeString(chars, static_cast<int>(std::char_traits<char>::length(chars)));
}

ObjString* takeString(std::string_view chars)
{
    const uint32_t hash = hashString(chars.data(), chars.length());
    ObjString* interned = VM::getInstance().stringTable().findString(chars.data(), chars.length(), hash);
    if (interned != nullptr) return interned;

    return allocateString(chars.data(), static_cast<int>(chars.length()), hash);
}

ObjString* takeString(std::string&& chars)
{
    const uint32_t hash = hashString(chars.c_str(), chars.length());
    ObjString* interned = VM::getInstance().stringTable().findString(chars.c_str(), chars.length(), hash);
    if (interned != nullptr) return interned;

    ObjString* string = allocate<ObjString>(std::move(chars));
    string->hash = hash;

    VM::getInstance().stringTable().set(string, Value());
    return string;
}

ObjUpvalue* newUpvalue(Value* slot)
{
    return allocate<ObjUpvalue>(slot);
}

ObjInstance* newInstance(ObjClass* klass)
{
    return allocate<ObjInstance>(klass);
}

ObjBoundMethod* newBoundMethod(const Value& receiver, Value& method)
{
    return allocate<ObjBoundMethod>(receiver, method);
}

ObjClass* newClass(ObjString* name)
{
    return allocate<ObjClass>(name);
}

ObjClosure* newClosure(ObjFunction* function)
{
    return allocate<ObjClosure>(function);
}

ObjFunction* newFunction()
{
    return allocate<ObjFunction>(0, Chunk(), nullptr);
}

ObjNative* newNative(uint8_t arity, NativeFn function, bool isMethod)
{
    return allocate<ObjNative>(arity, function, isMethod);
}

ObjRange* newRange(double min, double max, double step,
                   bool includeStart, bool includeEnd)
{
    return allocate<ObjRange>(min, max, step, includeStart, includeEnd);
}

ObjList* newList()
{
    return allocate<ObjList>();
}

ObjMap* newMap()
{
    return allocate<ObjMap>();
}

size_t ValueHasher::operator()(const Value& value) const
{
    const size_t typeHash = std::hash<unsigned int>{}(static_cast<unsigned int>(value.type));
    size_t valueHash = 0;
    switch (value.type)
    {
    case ValueType::NIL: break;
    case ValueType::BOOL: valueHash = std::hash<bool>{}(asBoolean(value)); break;
    case ValueType::NUMBER: valueHash = std::hash<double>{}(asNumber(value)); break;
    case ValueType::OBJ: valueHash = std::hash<const void*>{}(asObject(value)); break;
    }
    return typeHash ^ (valueHash + 0x9e3779b9u + (typeHash << 6) + (typeHash >> 2));
}

bool ObjMap::set(const Value& key, const Value& value)
{
    const auto found = indices.find(key);
    if (found != indices.end())
    {
        entries[found->second].value = value;
        return false;
    }

    const size_t index = entries.size();
    entries.push_back({ key, value, true });
    indices.emplace(key, index);
    return true;
}

bool ObjMap::get(const Value& key, Value* value) const
{
    const auto found = indices.find(key);
    if (found == indices.end())
        return false;
    if (value)
        *value = entries[found->second].value;
    return true;
}

bool ObjMap::remove(const Value& key, Value* value)
{
    const auto found = indices.find(key);
    if (found == indices.end())
        return false;
    MapEntry& entry = entries[found->second];
    if (value)
        *value = entry.value;
    entry.active = false;
    indices.erase(found);
    return true;
}

bool ObjMap::replaceKey(const Value& oldKey, const Value& newKey)
{
    const auto oldEntry = indices.find(oldKey);
    if (oldEntry == indices.end())
        return false;
    if (oldKey == newKey)
        return true;
    if (indices.find(newKey) != indices.end())
        return false;

    const size_t index = oldEntry->second;
    indices.erase(oldEntry);
    entries[index].key = newKey;
    indices.emplace(newKey, index);
    return true;
}

void ObjMap::clear()
{
    entries.clear();
    indices.clear();
}

const MapEntry* ObjMap::entryAt(size_t index) const
{
    size_t activeIndex = 0;
    for (const MapEntry& entry : entries)
    {
        if (!entry.active)
            continue;
        if (activeIndex == index)
            return &entry;
        ++activeIndex;
    }
    return nullptr;
}

void printFunction(ObjFunction* function)
{
    if (function->name == nullptr)
    {
        std::cout << "<script>";
        return;
    }
    std::cout << "<fn " << function->name->chars << ">";
}

void printRange(ObjRange* range)
{
    std::cout << range->min << ".." << range->max;
}

void printList(ObjList* list)
{
    std::cout << "[";
    const std::vector<Value>& items = list->items;
    for (auto current = items.begin(); current != items.end();)
    {
        printValue(*current);

        if (++current != items.end())
            std::cout << ", ";
    }
    std::cout << "]";
}

void printMap(ObjMap* map)
{
    std::cout << "{";
    bool first = true;
    for (const MapEntry& entry : map->entries)
    {
        if (!entry.active)
            continue;
        if (!first)
            std::cout << ", ";
        printValue(entry.key);
        std::cout << ": ";
        printValue(entry.value);
        first = false;
    }
    std::cout << "}";
}

void printObject(const Value& value)
{
    switch (getObjType(value))
    {
    case ObjType::STRING:
        std::cout << asCString(value);
        break;
    case ObjType::NATIVE:
        std::cout << "<native fn>";
        break;
    case ObjType::UPVALUE:
        printf("upvalue");
        break;
    case ObjType::FUNCTION:
        printFunction(asFunction(value));
        break;
    case ObjType::CLOSURE:
        printFunction(asClosure(value)->function);
        break;
    case ObjType::BOUND_METHOD:
        printObject(asBoundMethod(value)->method);
        break;
    case ObjType::RANGE:
        printRange(asRange(value));
        break;
    case ObjType::LIST:
        printList(asList(value));
        break;
    case ObjType::MAP:
        printMap(asMap(value));
        break;
    case ObjType::CLASS:
        std::cout << asClass(value)->name->chars;
        break;
    case ObjType::INSTANCE:
        std::cout << asInstance(value)->klass->name->chars << " instance";
        break;
    }
    static_assert(static_cast<int>(ObjType::COUNT) == 11, "Missing enum value");
}

size_t sizeOfObject(const Value& value)
{
    switch (getObjType(value))
    {
    case ObjType::STRING: return sizeof(ObjString) + asString(value)->length;
    case ObjType::NATIVE: return sizeof(ObjNative);
    case ObjType::UPVALUE: return sizeof(ObjUpvalue) + sizeOf(static_cast<ObjUpvalue*>(asObject(value))->closed);
    case ObjType::FUNCTION:
    {
        //const ObjFunction* function = asFunction(value);
        return sizeof(ObjFunction);
            /*+sizeOfObject(function->name)
            + function->chunk.code.size() * sizeof(ChunkInstructions::value_type)
            + function->chunk.lines.size() * sizeof(int)
            + function->chunk.constants.values.size() * sizeof(Value);*/
    }
    case ObjType::CLOSURE:
    {
        size_t upValuesSize = 0;
        for (ObjUpvalue* upvalue : asClosure(value)->upvalues)
        {
            Value test(upvalue);
            upValuesSize += sizeOfObject(test);
        }
        return sizeof(ObjClosure) + upValuesSize;
    }
    case ObjType::BOUND_METHOD: return sizeof(ObjBoundMethod);
    case ObjType::RANGE: return sizeof(ObjRange);
    case ObjType::LIST:
    {
        size_t listElemsSize = 0;
        for (const Value& listValue : asList(value)->items)
        {
            listElemsSize += sizeOf(listValue);
        }
        return sizeof(ObjList) + listElemsSize;
    }
    case ObjType::MAP:
    {
        size_t entriesSize = 0;
        for (const MapEntry& entry : asMap(value)->entries)
        {
            if (!entry.active)
                continue;
            entriesSize += sizeOf(entry.key) + sizeOf(entry.value);
        }
        return sizeof(ObjMap) + entriesSize;
    }
    case ObjType::CLASS: 
        return sizeof(ObjClass)
            + asClass(value)->methods.getSize()
            + sizeOf(asClass(value)->initializer) - sizeof(Value);
    case ObjType::INSTANCE: return sizeof(ObjInstance) + asInstance(value)->fields.getSize();
    }

    static_assert(static_cast<int>(ObjType::COUNT) == 11, "Missing enum value");
    return 0;
}

std::string objectAsStr(const Value& value)
{
    switch (getObjType(value))
    {
    case ObjType::STRING: return asString(value)->chars;
    case ObjType::NATIVE: return "<native fn>";
    case ObjType::FUNCTION:
    {
        const ObjFunction* function = asFunction(value);
        return function && function->name
            ? "<" + function->name->chars + ">" : "<function>";
    }
    case ObjType::CLOSURE:
    {
        const ObjClosure* closure = asClosure(value);
        const ObjFunction* function = closure ? closure->function : nullptr;
        return function && function->name
            ? "<" + function->name->chars + ">" : "<function>";
    }
    case ObjType::BOUND_METHOD: return objectAsStr(asBoundMethod(value)->method);
    case ObjType::RANGE: return std::to_string(asRange(value)->min) + ".." + std::to_string(asRange(value)->max);
    case ObjType::LIST:
    {
        std::string list = "";
        const std::vector<Value>& items = asList(value)->items;
        for (auto current = items.begin(); current != items.end();)
        {
            list += valueAsStr(*current);

            if (++current != items.end())
                list += ",";
        }
        return list;
    }
    case ObjType::MAP:
    {
        std::string result = "{";
        bool first = true;
        for (const MapEntry& entry : asMap(value)->entries)
        {
            if (!entry.active)
                continue;
            if (!first)
                result += ", ";
            result += valueAsStr(entry.key) + ": " + valueAsStr(entry.value);
            first = false;
        }
        return result + "}";
    }
    case ObjType::CLASS: return "" + asClass(value)->name->chars;
    case ObjType::INSTANCE: return asInstance(value)->klass->name->chars + " instance";
    }

    static_assert(static_cast<int>(ObjType::COUNT) == 11, "Missing enum value");
    return "<Unknown>";
}

ObjString* objectAsString(const Value& value)
{
    return takeString(objectAsStr(value));
}

ObjString* concatenate(ObjString* a, ObjString* b)
{
    std::string concat = a->chars + b->chars; // TODO: This allocates memory! We are moving it to the ObjString

    ObjString* result = takeString(std::move(concat));
    return result;
}
