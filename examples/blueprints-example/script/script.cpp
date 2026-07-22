#include "script.h"

#include <Vm.h>

ScriptPropertyPtr ScriptUtils::FindVariableById(Script& script, int varId)
{
    auto it = std::find_if(script.variables.begin(), script.variables.end(), [varId](const ScriptPropertyPtr& v) { return v->ID == varId; });
    if (it != script.variables.end())
    {
        return (*it);
    }

    return nullptr;
}

ScriptFunctionPtr ScriptUtils::FindFunctionById(Script& script, int funId)
{
    auto it = std::find_if(script.functions.begin(), script.functions.end(), [funId](const ScriptFunctionPtr& f) { return f->ID == funId; });
    if (it != script.functions.end())
    {
        return (*it);
    }

    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (scriptClass->constructor && scriptClass->constructor->ID == funId)
            return scriptClass->constructor;
        auto method = std::find_if(scriptClass->methods.begin(), scriptClass->methods.end(),
            [funId](const ScriptFunctionPtr& value) { return value->ID == funId; });
        if (method != scriptClass->methods.end())
            return *method;
    }
    return nullptr;
}

ScriptClassPtr ScriptUtils::FindClassById(Script& script, int classId)
{
    auto it = std::find_if(script.classes.begin(), script.classes.end(),
        [classId](const ScriptClassPtr& value) { return value->ID == classId; });
    return it == script.classes.end() ? nullptr : *it;
}

ScriptPropertyPtr ScriptUtils::FindClassPropertyById(Script& script, int propertyId)
{
    for (const ScriptClassPtr& scriptClass : script.classes)
        for (const ScriptPropertyPtr& property : scriptClass->properties)
            if (property->ID == propertyId)
                return property;
    return nullptr;
}

ScriptPropertyPtr ScriptUtils::FindVariableById(const Script& script, int varId)
{
    auto it = std::find_if(script.variables.begin(), script.variables.end(), [varId](const ScriptPropertyPtr& v) { return v->ID == varId; });
    if (it != script.variables.end())
    {
        return (*it);
    }

    return nullptr;
}

ScriptFunctionPtr ScriptUtils::FindFunctionById(const Script& script, int funId)
{
    auto it = std::find_if(script.functions.begin(), script.functions.end(), [funId](const ScriptFunctionPtr& f) { return f->ID == funId; });
    if (it != script.functions.end())
    {
        return (*it);
    }

    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (scriptClass->constructor && scriptClass->constructor->ID == funId)
            return scriptClass->constructor;
        auto method = std::find_if(scriptClass->methods.begin(), scriptClass->methods.end(),
            [funId](const ScriptFunctionPtr& value) { return value->ID == funId; });
        if (method != scriptClass->methods.end())
            return *method;
    }
    return nullptr;
}

ScriptClassPtr ScriptUtils::FindClassById(const Script& script, int classId)
{
    auto it = std::find_if(script.classes.begin(), script.classes.end(),
        [classId](const ScriptClassPtr& value) { return value->ID == classId; });
    return it == script.classes.end() ? nullptr : *it;
}

ScriptPropertyPtr ScriptUtils::FindClassPropertyById(const Script& script, int propertyId)
{
    for (const ScriptClassPtr& scriptClass : script.classes)
        for (const ScriptPropertyPtr& property : scriptClass->properties)
            if (property->ID == propertyId)
                return property;
    return nullptr;
}

ScriptClassPtr ScriptUtils::FindOwningClass(const Script& script, int elementId)
{
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (scriptClass->ID == elementId ||
            (scriptClass->constructor && scriptClass->constructor->ID == elementId))
            return scriptClass;
        for (const ScriptFunctionPtr& method : scriptClass->methods)
            if (method->ID == elementId)
                return scriptClass;
        for (const ScriptPropertyPtr& property : scriptClass->properties)
            if (property->ID == elementId)
                return scriptClass;
    }
    return nullptr;
}

std::vector<NodePtr> ScriptUtils::FindFunctionReferences(Script& script, int funId)
{
    std::vector<NodePtr> nodeRefs;

    for (auto& function : script.functions)
    {
        for (auto& node : function->Graph.GetNodes())
        {
            if (node->refId == funId)
            {
                nodeRefs.push_back(node);
            }
        }
    }

    for (auto& scriptClass : script.classes)
    {
        if (scriptClass->constructor)
        {
            for (auto& node : scriptClass->constructor->Graph.GetNodes())
                if (node->refId == funId)
                    nodeRefs.push_back(node);
        }
        for (auto& method : scriptClass->methods)
        {
            for (auto& node : method->Graph.GetNodes())
            {
                if (node->refId == funId)
                {
                    nodeRefs.push_back(node);
                }
            }
        }
    }

    if (script.main) for (auto& node : script.main->Graph.GetNodes())
    {
        if (node->refId == funId)
        {
            nodeRefs.push_back(node);
        }
    }

    return nodeRefs;
}

std::vector<NodePtr> ScriptUtils::FindVariableReferences(Script& script, int varId)
{
    std::vector<NodePtr> nodeRefs;

    for (auto& function : script.functions)
    {
        for (auto& node : function->Graph.GetNodes())
        {
            if (node->refId == varId)
            {
                nodeRefs.push_back(node);
            }
        }
    }

    for (auto& scriptClass : script.classes)
    {
        if (scriptClass->constructor)
        {
            for (auto& node : scriptClass->constructor->Graph.GetNodes())
                if (node->refId == varId)
                    nodeRefs.push_back(node);
        }
        for (auto& method : scriptClass->methods)
        {
            for (auto& node : method->Graph.GetNodes())
            {
                if (node->refId == varId)
                {
                    nodeRefs.push_back(node);
                }
            }
        }
    }

    if (script.main) for (auto& node : script.main->Graph.GetNodes())
    {
        if (node->refId == varId)
        {
            nodeRefs.push_back(node);
        }
    }

    return nodeRefs;
}

void ScriptUtils::RefreshFunctionRefs(Script& script, int funId, IDGenerator& IDGenerator)
{
    if (ScriptFunctionPtr pFunction = ScriptUtils::FindFunctionById(script, funId))
    {
        NodePtr begin = pFunction->Graph.FindNodeIf([](const NodePtr& node) { return node->Category == NodeCategory::Begin; });
        if (begin)
        {
            begin->Refresh(script, IDGenerator);
            NodeUtils::BuildNode(begin);
        }

        for (auto& node : pFunction->Graph.GetNodes())
        {
            if (node->Category == NodeCategory::Return)
            {
                node->Refresh(script, IDGenerator);
                NodeUtils::BuildNode(node);
            }
        }
    }

    std::vector<NodePtr> nodeRefs = ScriptUtils::FindFunctionReferences(script, funId);
    for (auto& node : nodeRefs)
    {
        node->Refresh(script, IDGenerator);
        NodeUtils::BuildNode(node);
    }

    ScriptClassPtr owner = ScriptUtils::FindOwningClass(script, funId);
    if (owner && owner->constructor && owner->constructor->ID == funId)
    {
        for (NodePtr& node : ScriptUtils::FindFunctionReferences(script, owner->ID))
        {
            node->Refresh(script, IDGenerator);
            NodeUtils::BuildNode(node);
        }
    }
}

void ScriptUtils::RefreshVariableRefs(Script& script, int varId, IDGenerator& IDGenerator)
{
    std::vector<NodePtr> nodeRefs = ScriptUtils::FindVariableReferences(script, varId);
    for (auto& node : nodeRefs)
    {
        node->Refresh(script, IDGenerator);
        NodeUtils::BuildNode(node);
    }
}

void ScriptUtils::MarkScriptRoots(Script& script)
{
    VM& vm = VM::getInstance();

    MarkFunctionRoots(script.main);

    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        MarkFunctionRoots(scriptClass->constructor);
        for (const ScriptFunctionPtr& scriptFunction : scriptClass->methods)
        {
            MarkFunctionRoots(scriptFunction);
        }

        for (const ScriptPropertyPtr& scriptProperty : scriptClass->properties)
        {
            MarkVariableRoots(scriptProperty);
        }
    }

    for (const ScriptFunctionPtr& scriptFunction : script.functions)
    {
        MarkFunctionRoots(scriptFunction);
    }

    for (const ScriptPropertyPtr& scriptProperty : script.variables)
    {
        MarkVariableRoots(scriptProperty);
    }
}

void ScriptUtils::MarkFunctionRoots(const ScriptFunctionPtr& pFunction)
{
    if (!pFunction) return;

    VM& vm = VM::getInstance();

    for (auto& input : pFunction->functionDef->inputs)
    {
        vm.markValue(input.value);
    }

    for (auto& output : pFunction->functionDef->outputs)
    {
        vm.markValue(output.value);
    }

    for (auto& scriptProperty : pFunction->variables)
    {
        vm.markValue(scriptProperty->defaultValue);
    }

    for (NodePtr& node : pFunction->Graph.GetNodes())
    {
        for (Value& value : node->InputValues)
        {
            vm.markValue(value);
        }
    }
}

void ScriptUtils::MarkVariableRoots(const ScriptPropertyPtr& pVariable)
{
    if (!pVariable) return;

    VM& vm = VM::getInstance();

    vm.markValue(pVariable->defaultValue);
}
