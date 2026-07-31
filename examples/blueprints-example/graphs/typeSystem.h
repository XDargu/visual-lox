#pragma once

#include <Value.h>

#include <string>
#include <vector>

struct ObjMap;

enum class PinType
{
    Flow,
    Nil,
    Bool,
    Int,
    Float,
    String,
    List,
    Map,
    Range,
    Object,
    Function,
    Tuple,
    // Internal constraint used by nodes that accept lists, ranges, or strings.
    // It is not exposed as a user-selectable declaration type.
    Iterable,
    TypeVariable,
    Any,
    Error
};

// A TypeRef describes values accepted by a pin or script declaration. It is
// deliberately independent from Value: nil can therefore be the default for
// any declared type, and changing a default never changes the declaration.
struct TypeRef
{
    PinType kind = PinType::Any;
    std::vector<TypeRef> parameters;
    int classId = -1;
    int functionInputCount = -1;
    std::string name;

    TypeRef() = default;
    TypeRef(PinType primitive) : kind(primitive) {}

    TypeRef& operator=(PinType primitive)
    {
        kind = primitive;
        parameters.clear();
        classId = -1;
        functionInputCount = -1;
        name.clear();
        return *this;
    }

    // Kept for source compatibility with rendering and compilation code that
    // only needs the outer shape of a type.
    operator PinType() const { return kind; }

    static TypeRef List(TypeRef element = TypeRef(PinType::Any));
    static TypeRef Map(TypeRef key = TypeRef(PinType::Any), TypeRef value = TypeRef(PinType::Any));
    static TypeRef Tuple(std::vector<TypeRef> elements);
    static TypeRef Iterable(TypeRef element = TypeRef(PinType::Any));
    static TypeRef Function(std::vector<TypeRef> inputs = {},
                            std::vector<TypeRef> outputs = {});
    static TypeRef Object(int scriptClassId, std::string className = {});
    static TypeRef Object(std::string nativeClassName);
    static TypeRef Variable(std::string variableName);

    const TypeRef& ElementType() const;
    const TypeRef& KeyType() const;
    const TypeRef& ValueType() const;
    bool ContainsVariable(const std::string& variableName) const;
    bool IsGeneric() const;
    std::string ToString() const;
};

bool operator==(const TypeRef& lhs, const TypeRef& rhs);
inline bool operator!=(const TypeRef& lhs, const TypeRef& rhs) { return !(lhs == rhs); }
inline bool operator==(const TypeRef& lhs, PinType rhs) { return lhs.kind == rhs; }
inline bool operator==(PinType lhs, const TypeRef& rhs) { return rhs == lhs; }
inline bool operator!=(const TypeRef& lhs, PinType rhs) { return !(lhs == rhs); }
inline bool operator!=(PinType lhs, const TypeRef& rhs) { return !(rhs == lhs); }

TypeRef TypeOfValue(const Value& value);
Value MakeValueFromType(const TypeRef& type);
bool MakeUniqueMapKey(const ObjMap& map, const TypeRef& keyType, Value& key);

// Assignment is directional. Any is the dynamic escape hatch, List<T> is
// covariant for reads, and nil is permitted by every runtime value type.
bool CanAssign(const TypeRef& source, const TypeRef& destination,
               bool allowDynamicCheck = true);

// Produces the most useful common type for inferred list literals and generic
// constraints. Any is returned when no safe common type exists.
TypeRef CommonType(const TypeRef& lhs, const TypeRef& rhs);
