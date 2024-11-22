
# pragma once

#include "node.h"

#include <Scanner.h>

#include <vector>
#include <list>
#include <string>
#include <functional>

class Compiler;
class VM;
struct Graph;
struct Value;

struct GraphCompiler
{
    using Callback = std::function<void(const NodePtr& node, const Graph& graph, CompilationStage stage, int portIdx)>;

    void CompileGraph(Graph& graph, const NodePtr& startNode, int outputIdx, const Callback& callback);
    void CompileBackwardsRecursive(Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback);
    void CompileRecursive(Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback);
    void CompileSingle(Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback);

    static void RegisterNatives(VM& vm);

    static constexpr const char* tempVarPrefix = "__lv__";
    static std::list<std::string> tempVarStorage; // List so pointers are preserved, we should improve this

    std::vector<Value>        m_constFoldingValues;
    std::vector<ed::NodeId>   m_constFoldingIDs;

    static void CompileInput(Compiler& compiler, const Graph& graph, const Pin& input, const Value& value);
    static void CompileOutput(Compiler& compiler, const Graph& graph, const Pin& output);
    static Token StoreTempVariable(const std::string& name);
};