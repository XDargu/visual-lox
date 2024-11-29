# pragma once

#include "class.h"
#include "function.h"
#include "property.h"

#include <string>
#include <vector>
#include <memory>

struct Script
{
    ScriptElementID ID;
    
    std::vector<ScriptClassPtr> classes;
    std::vector<ScriptPropertyPtr> variables;
    std::vector<ScriptFunctionPtr> functions;

    ScriptFunctionPtr main = nullptr; // TODO: Should this just be a function?
};

struct ScriptUtils
{
    static ScriptPropertyPtr FindVariableById(Script& script, int varId);
    static ScriptFunctionPtr FindFunctionById(Script& script, int funId);

    static ScriptPropertyPtr FindVariableById(const Script& script, int varId);
    static ScriptFunctionPtr FindFunctionById(const Script& script, int funId);

    static std::vector<NodePtr> FindFunctionReferences(Script& script, int funId);

    static void RefreshFunctionRefs(Script& script, int funId, IDGenerator& IDGenerator);

    static void MarkScriptRoots(Script& script);
    static void MarkFunctionRoots(const ScriptFunctionPtr& pFunction);
    static void MarkVariableRoots(const ScriptPropertyPtr& pVariable);
};