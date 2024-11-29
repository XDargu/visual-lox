
# pragma once

#include "../script/scriptElement.h"

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
        case ObjType::FUNCTION: return PinType::Function;
        case ObjType::CLOSURE: return PinType::Function;
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
        int id = -1;
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

    NodePtr MakeNode(IDGenerator& IDGenerator, ScriptElementID funcID);

    Input* FindOutputByName(const std::string& name);
    Input* FindInputByName(const std::string& name);

    Input* FindOutputByID(const int inputId);
    Input* FindInputByID(const int inputId);
};

using BasicFunctionDefPtr = std::shared_ptr< BasicFunctionDef>;