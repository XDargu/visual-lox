
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

struct CompilerContext
{
    CompilerContext(Compiler& compiler)
        : compiler(compiler)
    {}

    constexpr static const char* tempVarPrefix = "__lv__";
    static std::list<std::string> tempVarStorage; // List so pointers are preserved, we should improve this

    std::vector<Value>        constFoldingValues;
    std::vector<ed::NodeId>   constFoldingIDs;

    Compiler& compiler;

    Token StoreTempVariable(const std::string& name)
    {
        tempVarStorage.push_back(name);
        return Token(TokenType::VAR, tempVarStorage.back().c_str(), name.length(), 0);
    }
};

struct GraphCompiler
{
    GraphCompiler(Compiler& compiler)
        : context(compiler)
    {}

    using Callback = std::function<void(const NodePtr& node, const Graph& graph, CompilationStage stage, int portIdx)>;

    void CompileGraph(Graph& graph, const NodePtr& startNode, int outputIdx, const Callback& callback);
    void CompileBackwardsRecursive(Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback);
    void CompileRecursive(Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback);
    void CompileSingle(Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback);

    static void RegisterNatives(VM& vm);

    CompilerContext context;

    static void CompileInput(CompilerContext& compilerCtx, const Graph& graph, const Pin& input, const Value& value);
    static void CompileOutput(CompilerContext& compilerCtx, const Graph& graph, const Pin& output);
};