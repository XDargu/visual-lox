#pragma once

#include "../script/script.h"

#include <Vm.h>

#include <vector>

struct ScriptCompileOptions
{
    const std::vector<Value>* constFoldingValues = nullptr;
    const std::vector<ed::NodeId>* constFoldingNodeIds = nullptr;
    bool disassemble = false;
};

struct ScriptCompileResult
{
    ObjFunction* function = nullptr;
    InterpretResult status = InterpretResult::INTERPRET_COMPILE_ERROR;

    explicit operator bool() const
    {
        return status == InterpretResult::INTERPRET_OK && function != nullptr;
    }
};

class ScriptRuntime
{
public:
    static ScriptCompileResult Compile(VM& vm, const Script& script,
                                       const ScriptCompileOptions& options = {});
    static InterpretResult Execute(VM& vm, ObjFunction* function);
    static InterpretResult Run(VM& vm, const Script& script,
                               const ScriptCompileOptions& options = {});

private:
    static void CompileGraph(const Graph& graph, Compiler& compiler,
                             const ScriptCompileOptions& options);
};
