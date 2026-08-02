# pragma once

#include "class.h"
#include "function.h"
#include "property.h"

#include <string>
#include <vector>
#include <memory>
#include <map>

struct Script
{
    ScriptElementID ID;
    ModuleId ModuleIdentity{ Uuid::NewV4() };
    std::map<std::string, int> RuntimeIdCache;
    
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
    static ScriptPropertyPtr FindVariableByPersistentId(const Script& script, ScriptElementUuid persistentId);
    static ScriptPropertyPtr FindFunctionVariableByPersistentId(const ScriptFunctionPtr& function, ScriptElementUuid persistentId);
    static ScriptPropertyPtr FindVisibleVariableByPersistentId(const Script& script, int functionId, ScriptElementUuid persistentId);
    static ScriptFunctionPtr FindVariableOwner(const Script& script, int varId);
    static ScriptFunctionPtr FindFunctionById(const Script& script, int funId);
    static ScriptFunctionPtr FindAnyFunctionById(const Script& script, int funId);
    static ScriptFunctionPtr FindFunctionByPersistentId(const Script& script, ScriptElementUuid persistentId);
    static ScriptClassPtr FindClassById(const Script& script, int classId);
    static ScriptClassPtr FindClassByPersistentId(const Script& script, ScriptElementUuid persistentId);
    static ScriptPropertyPtr FindClassPropertyById(const Script& script, int propertyId);
    static ScriptPropertyPtr FindClassPropertyByPersistentId(const Script& script, ScriptElementUuid persistentId);

    static std::vector<NodePtr> FindFunctionReferences(Script& script, int funId);
    static std::vector<NodePtr> FindVariableReferences(Script& script, int varId);

    static void RefreshFunctionRefs(Script& script, int funId, IDGenerator& IDGenerator);
    static void RefreshVariableRefs(Script& script, int varId, IDGenerator& IDGenerator);

    static void MarkScriptRoots(Script& script);
    static void MarkFunctionRoots(const ScriptFunctionPtr& pFunction);
    static void MarkVariableRoots(const ScriptPropertyPtr& pVariable);
};
