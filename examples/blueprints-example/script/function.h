# pragma once

#include "property.h"
#include "../shared/functionShared.h"

#include <Value.h>

#include <string>
#include <vector>
#include <memory>

struct ScriptFunction
{
    ScriptFunction()
    {
        functionDef = std::make_shared<BasicFunctionDef>();
    }

    BasicFunctionDefPtr functionDef;
    Graph Graph;
    std::vector<ScriptProperty> variables;
};
