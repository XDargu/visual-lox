#include "script.h"

ScriptProperty* ScriptUtils::FindVariableById(Script& script, int varId)
{
    auto it = std::find_if(script.variables.begin(), script.variables.end(), [varId](const ScriptProperty& v) { return v.Id == varId; });
    if (it != script.variables.end())
    {
        ScriptProperty& var = *it;
        return &var;
    }

    return nullptr;
}

ScriptFunction* ScriptUtils::FindFunctionById(Script& script, int funId)
{
    auto it = std::find_if(script.functions.begin(), script.functions.end(), [funId](const ScriptFunction& f) { return f.Id == funId; });
    if (it != script.functions.end())
    {
        ScriptFunction& fun = *it;
        return &fun;
    }

    return nullptr;
}

std::vector<NodePtr> ScriptUtils::FindFunctionReferences(Script& script, int funId)
{
    std::vector<NodePtr> nodeRefs;

    for (auto& function : script.functions)
    {
        for (auto& node : function.Graph.GetNodes())
        {
            if (node->refId == funId)
            {
                nodeRefs.push_back(node);
            }
        }
    }

    for (auto& node : script.main.Graph.GetNodes())
    {
        if (node->refId == funId)
        {
            nodeRefs.push_back(node);
        }
    }

    return nodeRefs;
}

void ScriptUtils::RefreshFunctionRefs(Script& script, int funId, IDGenerator& IDGenerator)
{
    if (ScriptFunction* pFun = ScriptUtils::FindFunctionById(script, funId))
    {
        NodePtr begin = pFun->Graph.FindNodeIf([](const NodePtr& node) { return node->Category == NodeCategory::Begin; });
        if (begin)
        {
            begin->Refresh(IDGenerator);
            NodeUtils::BuildNode(begin);
        }

        for (auto& node : pFun->Graph.GetNodes())
        {
            if (node->Category == NodeCategory::Return)
            {
                node->Refresh(IDGenerator);
                NodeUtils::BuildNode(node);
            }
        }

        std::vector<NodePtr> nodeRefs = ScriptUtils::FindFunctionReferences(script, funId);
        for (auto& node : nodeRefs)
        {
            node->Refresh(IDGenerator);
            NodeUtils::BuildNode(node);
        }
    }
}
