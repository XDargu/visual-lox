#include "typeSystem.h"

#include <Object.h>

#include <sstream>
#include <utility>

namespace
{
const TypeRef kAny(PinType::Any);

std::string JoinTypes(const std::vector<TypeRef>& types)
{
    std::ostringstream stream;
    for (size_t i = 0; i < types.size(); ++i)
    {
        if (i) stream << ", ";
        stream << types[i].ToString();
    }
    return stream.str();
}
}

TypeRef TypeRef::List(TypeRef element)
{
    TypeRef result(PinType::List);
    result.parameters.push_back(std::move(element));
    return result;
}

TypeRef TypeRef::Tuple(std::vector<TypeRef> elements)
{
    TypeRef result(PinType::Tuple);
    result.parameters = std::move(elements);
    return result;
}

TypeRef TypeRef::Iterable(TypeRef element)
{
    TypeRef result(PinType::Iterable);
    result.parameters.push_back(std::move(element));
    return result;
}

TypeRef TypeRef::Function(std::vector<TypeRef> inputs, std::vector<TypeRef> outputs)
{
    TypeRef result(PinType::Function);
    result.parameters.reserve(inputs.size() + outputs.size());
    result.parameters.insert(result.parameters.end(), inputs.begin(), inputs.end());
    result.parameters.insert(result.parameters.end(), outputs.begin(), outputs.end());
    result.functionInputCount =
        static_cast<int>(inputs.size()); // signature input/output boundary
    return result;
}

TypeRef TypeRef::Object(int scriptClassId, std::string className)
{
    TypeRef result(PinType::Object);
    result.classId = scriptClassId;
    result.name = std::move(className);
    return result;
}

TypeRef TypeRef::Object(std::string nativeClassName)
{
    TypeRef result(PinType::Object);
    result.name = std::move(nativeClassName);
    return result;
}

TypeRef TypeRef::Variable(std::string variableName)
{
    TypeRef result(PinType::TypeVariable);
    result.name = std::move(variableName);
    return result;
}

const TypeRef& TypeRef::ElementType() const
{
    return parameters.empty() ? kAny : parameters.front();
}

bool TypeRef::IsGeneric() const
{
    if (kind == PinType::TypeVariable)
        return true;
    for (const TypeRef& parameter : parameters)
        if (parameter.IsGeneric())
            return true;
    return false;
}

std::string TypeRef::ToString() const
{
    switch (kind)
    {
    case PinType::Flow: return "Flow";
    case PinType::Nil: return "Nil";
    case PinType::Bool: return "Bool";
    case PinType::Int: return "Int";
    case PinType::Float: return "Number";
    case PinType::String: return "String";
    case PinType::List: return "List<" + ElementType().ToString() + ">";
    case PinType::Range: return "Range";
    case PinType::Object:
        if (!name.empty()) return name;
        if (classId >= 0) return "Object<#" + std::to_string(classId) + ">";
        return "Object";
    case PinType::Function:
    {
        if (functionInputCount < 0) return "Function";
        const size_t inputCount = functionInputCount < 0
            ? 0 : static_cast<size_t>(functionInputCount);
        const size_t boundary = inputCount < parameters.size() ? inputCount : parameters.size();
        std::vector<TypeRef> inputs(parameters.begin(), parameters.begin() + boundary);
        std::vector<TypeRef> outputs(parameters.begin() + boundary, parameters.end());
        return "Function<(" + JoinTypes(inputs) + ") -> (" + JoinTypes(outputs) + ")>";
    }
    case PinType::Tuple: return "Tuple<" + JoinTypes(parameters) + ">";
    case PinType::Iterable: return "List, Range or String";
    case PinType::TypeVariable: return name.empty() ? "T" : name;
    case PinType::Any: return "Any";
    case PinType::Error: return "Error";
    }
    return "Error";
}

bool operator==(const TypeRef& lhs, const TypeRef& rhs)
{
    if (lhs.kind == PinType::Object && rhs.kind == PinType::Object)
    {
        if (lhs.classId >= 0 || rhs.classId >= 0)
            return lhs.classId == rhs.classId &&
                   lhs.parameters == rhs.parameters;
    }
    return lhs.kind == rhs.kind && lhs.parameters == rhs.parameters &&
           lhs.classId == rhs.classId &&
           lhs.functionInputCount == rhs.functionInputCount &&
           lhs.name == rhs.name;
}

TypeRef TypeOfValue(const Value& value)
{
    switch (value.type)
    {
    case ValueType::NIL: return TypeRef(PinType::Nil);
    case ValueType::BOOL: return TypeRef(PinType::Bool);
    case ValueType::NUMBER: return TypeRef(PinType::Float);
    case ValueType::OBJ:
        switch (asObject(value)->type)
        {
        case ObjType::STRING: return TypeRef(PinType::String);
        case ObjType::LIST:
        {
            const ObjList* list = asList(value);
            TypeRef element(PinType::Any);
            bool hasElement = false;
            for (const Value& item : list->items)
            {
                const TypeRef itemType = TypeOfValue(item);
                element = hasElement ? CommonType(element, itemType) : itemType;
                hasElement = true;
            }
            return TypeRef::List(element);
        }
        case ObjType::RANGE: return TypeRef(PinType::Range);
        case ObjType::CLASS:
            return TypeRef::Object(asClass(value)->name ? asClass(value)->name->chars : "");
        case ObjType::INSTANCE:
            return TypeRef::Object(asInstance(value)->klass && asInstance(value)->klass->name
                ? asInstance(value)->klass->name->chars : "");
        case ObjType::FUNCTION:
        case ObjType::CLOSURE: return TypeRef(PinType::Function);
        default: return TypeRef(PinType::Any);
        }
    }
    return TypeRef(PinType::Error);
}

Value MakeValueFromType(const TypeRef& type)
{
    switch (type.kind)
    {
    case PinType::Bool: return Value(false);
    case PinType::Int:
    case PinType::Float: return Value(0.0);
    case PinType::String: return Value(takeString("", 0));
    case PinType::List: return Value(newList());
    case PinType::Range: return Value(newRange(0.0, 0.0));
    case PinType::Function: return Value(newFunction());
    default: return Value();
    }
}

bool CanAssign(const TypeRef& source, const TypeRef& destination, bool allowDynamicCheck)
{
    if (source == destination)
        return true;
    if (source.kind == PinType::Error || destination.kind == PinType::Error)
        return false;
    if (source.kind == PinType::Flow || destination.kind == PinType::Flow)
        return false;
    if (destination.kind == PinType::Any)
        return true;
    if (source.kind == PinType::Any)
        return allowDynamicCheck;
    if (source.kind == PinType::TypeVariable || destination.kind == PinType::TypeVariable)
        return true;
    if (source.kind == PinType::Nil)
        return destination.kind != PinType::Flow &&
               destination.kind != PinType::Error &&
               destination.kind != PinType::TypeVariable &&
               destination.kind != PinType::Iterable;
    if (destination.kind == PinType::Iterable)
    {
        TypeRef element;
        if (source.kind == PinType::List)
            element = source.ElementType();
        else if (source.kind == PinType::Range)
            element = TypeRef(PinType::Float);
        else if (source.kind == PinType::String)
            element = TypeRef(PinType::String);
        else
            return false;
        return CanAssign(element, destination.ElementType(), allowDynamicCheck);
    }
    if (source.kind != destination.kind)
        return false;

    if (source.kind == PinType::List)
        return CanAssign(source.ElementType(), destination.ElementType(), false);
    if (source.kind == PinType::Tuple)
    {
        if (source.parameters.size() != destination.parameters.size())
            return false;
        for (size_t i = 0; i < source.parameters.size(); ++i)
            if (!CanAssign(source.parameters[i], destination.parameters[i], allowDynamicCheck))
                return false;
        return true;
    }
    if (source.kind == PinType::Object)
    {
        const bool destinationIsBase = destination.classId < 0 && destination.name.empty();
        return destinationIsBase ||
            (source.classId == destination.classId && source.name == destination.name);
    }
    if (source.kind == PinType::Function)
    {
        if (source.functionInputCount < 0 ||
            destination.functionInputCount < 0)
            return true;
        if (source.functionInputCount != destination.functionInputCount ||
            source.parameters.size() != destination.parameters.size())
            return false;

        const size_t inputCount =
            static_cast<size_t>(source.functionInputCount);
        // Function inputs are contravariant: a callable used by a node must
        // accept every value the node can pass to it. Outputs are covariant.
        for (size_t i = 0; i < inputCount; ++i)
            if (!CanAssign(destination.parameters[i], source.parameters[i],
                           allowDynamicCheck))
                return false;
        for (size_t i = inputCount; i < source.parameters.size(); ++i)
            if (!CanAssign(source.parameters[i], destination.parameters[i],
                           allowDynamicCheck))
                return false;
        return true;
    }
    return true;
}

TypeRef CommonType(const TypeRef& lhs, const TypeRef& rhs)
{
    if (lhs == rhs) return lhs;
    if (lhs.kind == PinType::Nil)
        return rhs.kind == PinType::Nil ? TypeRef(PinType::Any) : rhs;
    if (rhs.kind == PinType::Nil) return lhs;
    if (lhs.kind == PinType::List && rhs.kind == PinType::List)
        return TypeRef::List(CommonType(lhs.ElementType(), rhs.ElementType()));
    if (CanAssign(lhs, rhs, false)) return rhs;
    if (CanAssign(rhs, lhs, false)) return lhs;
    return TypeRef(PinType::Any);
}
