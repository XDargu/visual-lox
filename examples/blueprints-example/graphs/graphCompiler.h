
# pragma once

#include "node.h"

#include <Scanner.h>

#include <vector>
#include <list>
#include <string>

class Compiler;
class VM;
struct Graph;
struct Value;

struct GraphCompiler
{
    std::vector<NodePtr> CompileGraph(Compiler& compiler, Graph& graph, NodePtr startNode, int outputIdx);
    void CompileBackwardsRecursive(Compiler& compiler, Graph& graph, NodePtr startNode, int inputIdx, int outputIdx, std::vector<NodePtr>& processedNodes);
    void CompileRecursive(Compiler& compiler, Graph& graph, NodePtr startNode, int inputIdx, int outputIdx, std::vector<NodePtr>& processedNodes);

    static void RegisterNatives(VM& vm);

    static constexpr const char* tempVarPrefix = "__lv__";
    static std::list<std::string> tempVarStorage; // List so pointers are preserved, we should improve this

    static void CompileInput(Compiler& compiler, const Graph& graph, const Pin& input, const Value& value);
    static void CompileOutput(Compiler& compiler, const Graph& graph, const Pin& output);
    static Token StoreTempVariable(const std::string& name);
};