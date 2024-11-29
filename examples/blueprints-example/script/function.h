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
    ScriptFunction(ScriptElementID id, const char* name)
    {
        ID = id;
        Type = ScriptElementType::Function;
        functionDef = std::make_shared<BasicFunctionDef>();
        functionDef->name = name;
    }

    BasicFunctionDefPtr functionDef;
    Graph Graph;
    std::vector<ScriptPropertyPtr> variables;
};

using ScriptFunctionPtr = std::shared_ptr<ScriptFunction>;