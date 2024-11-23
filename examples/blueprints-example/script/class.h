# pragma once

#include "function.h"
#include "property.h"

#include <string>
#include <vector>
#include <memory>

struct ScriptClass
{
    std::string Name;

    std::vector<ScriptProperty> properties;
    std::vector<ScriptFunction> methods;

    ScriptFunction constructor;
};