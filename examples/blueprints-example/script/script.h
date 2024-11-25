# pragma once

#include "class.h"
#include "function.h"
#include "property.h"

#include <string>
#include <vector>
#include <memory>

struct Script
{
    Script()
    {
        main.functionDef->name = "Main";
    }
    
    int Id = -1;

    std::vector<ScriptClass> classes;
    std::vector<ScriptProperty> variables;
    std::vector<ScriptFunction> functions;

    ScriptFunction main;
};

namespace ScriptUtils
{
    static ScriptProperty* FindVariableById(Script& script, int varId)
    {
        auto it = std::find_if(script.variables.begin(), script.variables.end(), [varId](const ScriptProperty& v) { return v.Id == varId; });
        if (it != script.variables.end())
        {
            ScriptProperty& var = *it;
            return &var;
        }

        return nullptr;
    }

    static ScriptFunction* FindFunctionById(Script& script, int funId)
    {
        auto it = std::find_if(script.functions.begin(), script.functions.end(), [funId](const ScriptFunction& f) { return f.Id == funId; });
        if (it != script.functions.end())
        {
            ScriptFunction& fun = *it;
            return &fun;
        }

        return nullptr;
    }
}