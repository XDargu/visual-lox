
# pragma once

#include "../script/scriptElement.h"

#include "../graphs/node.h"
#include "../graphs/idgeneration.h"

#include <Value.h>
#include <Object.h>

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct BasicFunctionDef : public std::enable_shared_from_this<BasicFunctionDef>
{
    struct Input
    {
        std::string key;
        std::string name;
        Value value;
        int id = -1;
        TypeRef type;
        std::string description;
        ScriptPortId persistentId{ Uuid::NewV4() };

        Input() = default;
        Input(std::string portName, Value defaultValue, int portId = -1,
              std::string portDescription = {})
            : name(std::move(portName)), value(defaultValue), id(portId),
              type(TypeOfValue(defaultValue)),
              description(std::move(portDescription))
        {
            if (type == PinType::Nil)
                type = PinType::Any;
        }
        Input(std::string portName, Value defaultValue, int portId, TypeRef declaredType,
              std::string portDescription = {})
            : name(std::move(portName)), value(defaultValue), id(portId),
              type(std::move(declaredType)),
              description(std::move(portDescription))
        {}
    };

    using DynamicInputProps = ::DynamicInputProps;

    std::vector<Input> inputs;
    std::vector<Input> outputs;
    std::vector<GenericTypeProperty> genericTypeProperties;

    NodeDefinitionFlags flags = NodeDefinitionFlags::None;

    DynamicInputProps dynamicInputProps;

    std::string id;
    uint32_t revision = 1;
    std::string compatibilityFingerprint;
    ScriptElementUuid scriptId;
    std::string name;
    std::string description;
    std::string displayName;

    bool ShowInputNames = true;
    bool ShowOutputNames = true;

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
