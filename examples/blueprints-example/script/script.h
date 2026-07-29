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
    static ScriptPropertyPtr FindFunctionVariableById(const ScriptFunctionPtr& function, int varId);
    static ScriptPropertyPtr FindFunctionVariableById(Script& script, int functionId, int varId);
    static ScriptPropertyPtr FindAnyVariableById(Script& script, int varId);
    static ScriptPropertyPtr FindVisibleVariableById(Script& script, int functionId, int varId);
    static ScriptFunctionPtr FindVariableOwner(Script& script, int varId);
    static ScriptFunctionPtr FindFunctionById(Script& script, int funId);
    static ScriptFunctionPtr FindAnyFunctionById(Script& script, int funId);
    static ScriptClassPtr FindClassById(Script& script, int classId);
    static ScriptPropertyPtr FindClassPropertyById(Script& script, int propertyId);
    static ScriptClassPtr FindOwningClass(const Script& script, int elementId);

    static ScriptPropertyPtr FindVariableById(const Script& script, int varId);
    static ScriptPropertyPtr FindFunctionVariableById(const Script& script, int functionId, int varId);
    static ScriptPropertyPtr FindAnyVariableById(const Script& script, int varId);
    static ScriptPropertyPtr FindVisibleVariableById(const Script& script, int functionId, int varId);
    static ScriptFunctionPtr FindVariableOwner(const Script& script, int varId);
    static ScriptFunctionPtr FindFunctionById(const Script& script, int funId);
    static ScriptFunctionPtr FindAnyFunctionById(const Script& script, int funId);
    static ScriptClassPtr FindClassById(const Script& script, int classId);
    static ScriptPropertyPtr FindClassPropertyById(const Script& script, int propertyId);

    static std::vector<NodePtr> FindFunctionReferences(Script& script, int funId);
    static std::vector<NodePtr> FindVariableReferences(Script& script, int varId);

    static void RefreshFunctionRefs(Script& script, int funId, IDGenerator& IDGenerator);
    static void RefreshVariableRefs(Script& script, int varId, IDGenerator& IDGenerator);

    static void MarkScriptRoots(Script& script);
    static void MarkFunctionRoots(const ScriptFunctionPtr& pFunction);
    static void MarkVariableRoots(const ScriptPropertyPtr& pVariable);
};
