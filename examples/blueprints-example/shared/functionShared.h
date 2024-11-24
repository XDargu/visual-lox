
# pragma once

#include "../graphs/node.h"
#include "../graphs/idgeneration.h"

#include <Value.h>
#include <Object.h>

#include <string>
#include <vector>
#include <memory>

// TODO: Move somewhere else
inline PinType TypeOfValue(const Value& value)
{
    switch (value.type)
    {
    case ValueType::NIL: return PinType::Any;
    case ValueType::BOOL: return PinType::Bool;
    case ValueType::NUMBER: return PinType::Float;
    case ValueType::OBJ:
    {
        switch (asObject(value)->type)
        {
        case ObjType::STRING: return PinType::String;
        case ObjType::LIST: return PinType::List;
        }
    }
    }

    return PinType::Error;
}

struct BasicFunctionDef : public std::enable_shared_from_this<BasicFunctionDef>
{
    struct Input
    {
        std::string name;
        Value value;
    };

    struct DynamicInputProps
    {
        int minInputs = 1;
        int maxInputs = 16;
        PinType type = PinType::Any;
        Value defaultValue;
    };

    std::vector<Input> inputs;
    std::vector<Input> outputs;

    NodeFlags flags;

    DynamicInputProps dynamicInputProps;

    std::string name;

    NodePtr MakeNode(IDGenerator& IDGenerator);
};

using BasicFunctionDefPtr = std::shared_ptr< BasicFunctionDef>;