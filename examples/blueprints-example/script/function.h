# pragma once

#include "scriptElement.h"
#include "property.h"
#include "../shared/functionShared.h"
#include "../graphs/graph.h"

#include <Value.h>

#include <string>
#include <vector>
#include <memory>

struct ScriptFunction : public IScriptElement
{
    ScriptFunction()
    {
        Type = ScriptElementType::Function;
        functionDef = std::make_shared<BasicFunctionDef>();
    }

    BasicFunctionDefPtr functionDef;
    Graph Graph;
    std::vector<ScriptPropertyPtr> variables;
};

using ScriptFunctionPtr = std::shared_ptr<ScriptFunction>;