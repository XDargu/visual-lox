# pragma once

#include "property.h"

#include <Value.h>

#include <string>
#include <vector>
#include <memory>

struct ScriptFunction
{
    struct Input
    {
        std::string name;
        Value value;
    };

    std::string Name;

    Graph Graph;

    std::vector<Input> Inputs;
    std::vector<Input> Outputs;

    std::vector<ScriptProperty> variables;
};
