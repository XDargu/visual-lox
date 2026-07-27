
# pragma once

#include "../script/scriptElement.h"

#include "../graphs/node.h"
#include "../graphs/idgeneration.h"

#include <Value.h>
#include <Object.h>

#include <string>
#include <vector>
#include <memory>

struct BasicFunctionDef : public std::enable_shared_from_this<BasicFunctionDef>
{
    struct Input
    {
        std::string name;
        Value value;
        int id = -1;
        TypeRef type;
        std::string description;

        Input() = default;
        Input(std::string portName, Value defaultValue, int portId = -1,
              std::string portDescription = {})
            : name(std::move(portName)), value(defaultValue), id(portId),
              type(TypeOfValue(defaultValue)),
              description(std::move(portDescription))
        {
            if (type == PinType::Nil) type = PinType::Any;
        }
        Input(std::string portName, Value defaultValue, int portId, TypeRef declaredType,
              std::string portDescription = {})
            : name(std::move(portName)), value(defaultValue), id(portId),
              type(std::move(declaredType)),
              description(std::move(portDescription))
        {}
    };

    struct DynamicInputProps
    {
        int minInputs = 1;
        int maxInputs = 16;
        TypeRef type = PinType::Any;
        Value defaultValue;
        std::string description;
    };

    std::vector<Input> inputs;
    std::vector<Input> outputs;

    NodeDefinitionFlags flags = NodeDefinitionFlags::None;

    DynamicInputProps dynamicInputProps;

    std::string name;
    std::string description;

    NodePtr MakeNode(IDGenerator& IDGenerator, ScriptElementID funcID);

    Input* FindOutputByName(const std::string& name);
    Input* FindInputByName(const std::string& name);

    Input* FindOutputByID(const int inputId);
    Input* FindInputByID(const int inputId);
};

using BasicFunctionDefPtr = std::shared_ptr< BasicFunctionDef>;

// Constructs the standard function-call node. A null definition is permitted while
// deserializing a dangling script reference; FunctionNode::Refresh owns that error.
NodePtr BuildFunctionNode(IDGenerator& IDGenerator, const BasicFunctionDefPtr& pFunctionDef,
                          ScriptElementID funcID);
