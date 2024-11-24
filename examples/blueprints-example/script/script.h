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