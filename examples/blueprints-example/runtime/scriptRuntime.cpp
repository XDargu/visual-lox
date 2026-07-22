#include "scriptRuntime.h"

#include "../graphs/graphCompiler.h"

#include <Debug.h>

void ScriptRuntime::CompileGraph(const Graph& graph, Compiler& compiler,
                                 const ScriptCompileOptions& options)
{
    const NodePtr begin = graph.FindNodeIf([](const NodePtr& node)
    {
        return node->Category == NodeCategory::Begin;
    });
    if (!begin)
        return;

    GraphCompiler graphCompiler(compiler);
    if (options.constFoldingValues)
        graphCompiler.context.constFoldingValues = *options.constFoldingValues;
    if (options.constFoldingNodeIds)
        graphCompiler.context.constFoldingIDs = *options.constFoldingNodeIds;

    graphCompiler.CompileGraph(graph, begin, 0,
        [&](const NodePtr& node, const Graph& currentGraph, CompilationStage stage, int portIdx)
        {
            if (stage == CompilationStage::ConstFoldedInputs && options.constFoldingValues)
            {
                compiler.emitConstant((*options.constFoldingValues)[portIdx]);
                const int outputIdx = GraphUtils::IsNodeImplicit(node) ? 0 : 1;
                GraphCompiler::CompileOutput(graphCompiler.context, currentGraph, node->Outputs[outputIdx]);
            }
            else
            {
                node->Compile(graphCompiler.context, currentGraph, stage, portIdx);
            }
        });
}

ScriptCompileResult ScriptRuntime::Compile(VM& vm, const Script& script,
                                           const ScriptCompileOptions& options)
{
    if (!script.main)
        return {};

    const NodePtr mainBegin = script.main->Graph.FindNodeIf([](const NodePtr& node)
    {
        return node->Category == NodeCategory::Begin;
    });
    if (!mainBegin)
        return {};

    vm.resetStack();
    Compiler& compiler = vm.getCompiler();
    compiler.beginCompile();
    compiler.parser.hadError = false;
    compiler.parser.panicMode = false;

    for (const ScriptPropertyPtr& property : script.variables)
    {
        compiler.emitConstant(property->defaultValue);
        const Token token(TokenType::VAR, property->Name.c_str(), property->Name.length(), 0);
        compiler.defineVariable(compiler.identifierConstant(token));
    }

    for (const ScriptFunctionPtr& scriptFunction : script.functions)
    {
        Token functionToken(TokenType::IDENTIFIER, scriptFunction->functionDef->name.c_str(),
                            scriptFunction->functionDef->name.length(), 0);
        const uint32_t global = compiler.parseVariableDirectly(false, functionToken);
        compiler.markInitialized();

        CompilerScope functionScope(FunctionType::FUNCTION, compiler.current, &functionToken);
        compiler.current = &functionScope;
        compiler.beginScope();

        for (const BasicFunctionDef::Input& input : scriptFunction->functionDef->inputs)
        {
            const Token inputToken(TokenType::IDENTIFIER, input.name.c_str(), input.name.length(), 0);
            ++compiler.current->function->arity;
            if (compiler.current->function->arity > 255)
                compiler.errorAtCurrent("Can't have more than 255 parameters.");
            compiler.defineVariable(compiler.parseVariableDirectly(false, inputToken));
        }

        CompileGraph(scriptFunction->Graph, compiler, options);
        ObjFunction* function = compiler.endCompiler();
        const uint32_t constant = compiler.makeConstant(Value(function));
        compiler.emitOpWithValue(OpCode::OP_CLOSURE, OpCode::OP_CLOSURE_LONG, constant);
        for (int i = 0; i < function->upvalueCount; ++i)
        {
            compiler.emitByte(functionScope.upvalues[i].isLocal ? 1 : 0);
            compiler.emitByte(functionScope.upvalues[i].index);
        }
        compiler.defineVariable(global);
    }

    compiler.beginScope();
    CompileGraph(script.main->Graph, compiler, options);
    compiler.endScope();
    ObjFunction* function = compiler.endCompiler();

    if (compiler.parser.hadError)
        return {};
    if (options.disassemble)
        disassembleChunk(function->chunk, function->name ? function->name->chars.c_str() : "<script>");
    return { function, InterpretResult::INTERPRET_OK };
}

InterpretResult ScriptRuntime::Execute(VM& vm, ObjFunction* function)
{
    if (!function)
        return InterpretResult::INTERPRET_COMPILE_ERROR;

    vm.resetStack();
    vm.push(Value(function));
    ObjClosure* closure = newClosure(function);
    vm.pop();
    vm.push(Value(closure));
    if (!vm.callValue(Value(closure), 0))
        return InterpretResult::INTERPRET_RUNTIME_ERROR;

    const InterpretResult result = vm.run(0);
    if (result == InterpretResult::INTERPRET_OK)
        vm.pop();
    return result;
}

InterpretResult ScriptRuntime::Run(VM& vm, const Script& script,
                                   const ScriptCompileOptions& options)
{
    const ScriptCompileResult compiled = Compile(vm, script, options);
    return compiled ? Execute(vm, compiled.function) : InterpretResult::INTERPRET_COMPILE_ERROR;
}
