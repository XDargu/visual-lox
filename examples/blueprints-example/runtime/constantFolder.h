#pragma once

#include "../graphs/node.h"

#include <Value.h>

#include <string>
#include <vector>

class VM;
struct Graph;
struct Script;

struct ConstantFoldingFailure
{
    ed::NodeId nodeId = 0;
    ScriptElementID functionId;
    std::string message;
};

struct ConstantFoldingResult
{
    std::vector<Value> values;
    std::vector<ed::NodeId> nodeIds;
    std::vector<ConstantFoldingFailure> failures;
};

class ConstantFolder
{
public:
    static ConstantFoldingResult Fold(VM& vm, const Script& script);
    static bool IsSafeConstant(const Value& value);

private:
    static bool Evaluate(VM& vm, const Graph& graph, const NodePtr& node, Value& result);
};
