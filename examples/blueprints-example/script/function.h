# pragma once

#include "property.h"
#include "../shared/functionShared.h"
#include "../graphs/graph.h"

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

    int Id = -1;

    BasicFunctionDefPtr functionDef;
    Graph Graph;
    std::vector<ScriptProperty> variables;
};
