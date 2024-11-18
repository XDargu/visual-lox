
# pragma once

#include "node.h"

#include <vector>

class Compiler;
class VM;
struct Graph;
struct Value;

struct GraphCompiler
{
    void CompileGraph(Compiler& compiler, Graph& graph, NodePtr startNode, int outputIdx);
    void CompileBackwardsRecursive(Compiler& compiler, Graph& graph, NodePtr startNode, int inputIdx, int outputIdx, std::vector<NodePtr>& processedNodes);
    void CompileRecursive(Compiler& compiler, Graph& graph, NodePtr startNode, int inputIdx, int outputIdx, std::vector<NodePtr>& processedNodes);

    static void RegisterNatives(VM& vm);

    static constexpr const char* tempVarPrefix = "__lv__";

    static void CompileInput(Compiler& compiler, const Graph& graph, const Pin& input, const Value& value);
    static void CompileOutput(Compiler& compiler, const Pin& output);
};