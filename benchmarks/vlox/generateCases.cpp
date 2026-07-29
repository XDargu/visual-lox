#include "graphs/idgeneration.h"
#include "graphs/nodeRegistry.h"
#include "native/nodes/begin.h"
#include "native/nodes/function.h"
#include "native/nodes/object.h"
#include "native/nodes/return.h"
#include "native/nodes/variable.h"
#include "runtime/standardLibrary.h"
#include "script/scriptSerializer.h"
#include "validation/scriptValidator.h"

#include <Object.h>
#include <Vm.h>

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
constexpr double FibonacciModulus = 1'000'000'007.0;

void AttachNode(Graph& graph, const NodePtr& node)
{
    NodeUtils::BuildNode(node);
    graph.AddNode(node);
}

void LinkPins(Graph& graph, IDGenerator& ids, const Pin& output, const Pin& input)
{
    graph.AddLink(Link(ids.GetNextId(), output.ID, input.ID));
}

ScriptPropertyPtr AddNumberVariable(Script& script, IDGenerator& ids, const char* name, double defaultValue)
{
    ScriptPropertyPtr variable = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
    variable->type = PinType::Float;
    variable->defaultValue = Value(defaultValue);
    script.variables.push_back(variable);
    return variable;
}

NodePtr MakeCompiledNode(const NodeRegistry& registry, IDGenerator& ids, const char* name)
{
    const CompiledNodeDefPtr definition = registry.FindCompiled(name);
    if (!definition)
        throw std::runtime_error(std::string("Missing compiled node definition: ") + name);
    return definition->MakeNode(ids);
}

Script MakeIterativeFibonacci(const NodeRegistry& registry, IDGenerator& ids)
{
    Script script;
    script.ID = ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");

    const ScriptPropertyPtr size = AddNumberVariable(script, ids, "BenchmarkSize", 2'000'000.0);
    const ScriptPropertyPtr checksum = AddNumberVariable(script, ids, "BenchmarkChecksum", 0.0);
    const ScriptPropertyPtr previous = AddNumberVariable(script, ids, "FibonacciPrevious", 0.0);
    const ScriptPropertyPtr current = AddNumberVariable(script, ids, "FibonacciCurrent", 1.0);
    const ScriptPropertyPtr temporary = AddNumberVariable(script, ids, "FibonacciTemporary", 0.0);

    Graph& graph = script.main->Graph;
    const NodePtr begin = BuildBeginNode(ids, script.main);
    const NodePtr repeat = MakeCompiledNode(registry, ids, "Flow::Repeat");
    const NodePtr getSize = BuildGetVariableNode(ids, size);
    const NodePtr setTemporary = BuildSetVariableNode(ids, temporary);
    const NodePtr getCurrentForTemporary = BuildGetVariableNode(ids, current);
    const NodePtr setCurrent = BuildSetVariableNode(ids, current);
    const NodePtr add = MakeCompiledNode(registry, ids, "Math::Add");
    const NodePtr getPreviousForAdd = BuildGetVariableNode(ids, previous);
    const NodePtr getCurrentForAdd = BuildGetVariableNode(ids, current);
    const NodePtr modulo = MakeCompiledNode(registry, ids, "Math::Modulo");
    modulo->InputValues[1] = Value(FibonacciModulus);
    const NodePtr setPrevious = BuildSetVariableNode(ids, previous);
    const NodePtr getTemporary = BuildGetVariableNode(ids, temporary);
    const NodePtr setChecksum = BuildSetVariableNode(ids, checksum);
    const NodePtr getPreviousForChecksum = BuildGetVariableNode(ids, previous);

    for (const NodePtr& node : {
             begin, repeat, getSize, setTemporary, getCurrentForTemporary, setCurrent, add, getPreviousForAdd, getCurrentForAdd, modulo,
             setPrevious, getTemporary, setChecksum, getPreviousForChecksum })
    {
        AttachNode(graph, node);
    }

    LinkPins(graph, ids, begin->Outputs[0], repeat->Inputs[0]);
    LinkPins(graph, ids, getSize->Outputs[0], repeat->Inputs[1]);
    LinkPins(graph, ids, repeat->Outputs[0], setTemporary->Inputs[0]);
    LinkPins(graph, ids, getCurrentForTemporary->Outputs[0], setTemporary->Inputs[1]);
    LinkPins(graph, ids, setTemporary->Outputs[0], setCurrent->Inputs[0]);
    LinkPins(graph, ids, getPreviousForAdd->Outputs[0], add->Inputs[0]);
    LinkPins(graph, ids, getCurrentForAdd->Outputs[0], add->Inputs[1]);
    LinkPins(graph, ids, add->Outputs[0], modulo->Inputs[0]);
    LinkPins(graph, ids, modulo->Outputs[0], setCurrent->Inputs[1]);
    LinkPins(graph, ids, setCurrent->Outputs[0], setPrevious->Inputs[0]);
    LinkPins(graph, ids, getTemporary->Outputs[0], setPrevious->Inputs[1]);
    LinkPins(graph, ids, repeat->Outputs[2], setChecksum->Inputs[0]);
    LinkPins(graph, ids, getPreviousForChecksum->Outputs[0], setChecksum->Inputs[1]);
    return script;
}

Script MakeRecursiveFibonacci(const NodeRegistry& registry, IDGenerator& ids)
{
    Script script;
    script.ID = ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");

    const ScriptPropertyPtr size = AddNumberVariable(script, ids, "BenchmarkSize", 30.0);
    const ScriptPropertyPtr checksum = AddNumberVariable(script, ids, "BenchmarkChecksum", 0.0);

    ScriptFunctionPtr fibonacci = std::make_shared<ScriptFunction>(ids.GetNextId(), "Fibonacci");
    fibonacci->functionDef->description = "Computes a Fibonacci number recursively";
    fibonacci->functionDef->flags |= NodeDefinitionFlags::Pure;
    fibonacci->functionDef->inputs.push_back({ "Value", Value(0.0), ids.GetNextId(), "The Fibonacci index" });
    fibonacci->functionDef->outputs.push_back({ "Result", Value(0.0), ids.GetNextId(), "The Fibonacci number" });
    script.functions.push_back(fibonacci);

    Graph& functionGraph = fibonacci->Graph;
    const NodePtr functionBegin = BuildBeginNode(ids, fibonacci);
    const NodePtr branch = MakeCompiledNode(registry, ids, "Flow::Branch");
    const NodePtr lessThan = MakeCompiledNode(registry, ids, "Math::Less Than");
    lessThan->InputValues[1] = Value(2.0);
    const NodePtr baseReturn = BuildReturnNode(ids, *fibonacci);
    const NodePtr recursiveReturn = BuildReturnNode(ids, *fibonacci);
    const NodePtr subtractOne = MakeCompiledNode(registry, ids, "Math::Subtract");
    subtractOne->InputValues[1] = Value(1.0);
    const NodePtr subtractTwo = MakeCompiledNode(registry, ids, "Math::Subtract");
    subtractTwo->InputValues[1] = Value(2.0);
    const NodePtr callOne = fibonacci->functionDef->MakeNode(ids, fibonacci->ID);
    const NodePtr callTwo = fibonacci->functionDef->MakeNode(ids, fibonacci->ID);
    const NodePtr add = MakeCompiledNode(registry, ids, "Math::Add");

    for (const NodePtr& node : {
             functionBegin, branch, lessThan, baseReturn, recursiveReturn, subtractOne, subtractTwo, callOne, callTwo, add })
    {
        AttachNode(functionGraph, node);
    }

    LinkPins(functionGraph, ids, functionBegin->Outputs[0], branch->Inputs[0]);
    LinkPins(functionGraph, ids, functionBegin->Outputs[1], lessThan->Inputs[0]);
    LinkPins(functionGraph, ids, lessThan->Outputs[0], branch->Inputs[1]);
    LinkPins(functionGraph, ids, branch->Outputs[0], baseReturn->Inputs[0]);
    LinkPins(functionGraph, ids, functionBegin->Outputs[1], baseReturn->Inputs[1]);
    LinkPins(functionGraph, ids, branch->Outputs[1], recursiveReturn->Inputs[0]);
    LinkPins(functionGraph, ids, functionBegin->Outputs[1], subtractOne->Inputs[0]);
    LinkPins(functionGraph, ids, subtractOne->Outputs[0], callOne->Inputs[0]);
    LinkPins(functionGraph, ids, functionBegin->Outputs[1], subtractTwo->Inputs[0]);
    LinkPins(functionGraph, ids, subtractTwo->Outputs[0], callTwo->Inputs[0]);
    LinkPins(functionGraph, ids, callOne->Outputs[0], add->Inputs[0]);
    LinkPins(functionGraph, ids, callTwo->Outputs[0], add->Inputs[1]);
    LinkPins(functionGraph, ids, add->Outputs[0], recursiveReturn->Inputs[1]);

    Graph& mainGraph = script.main->Graph;
    const NodePtr mainBegin = BuildBeginNode(ids, script.main);
    const NodePtr getSize = BuildGetVariableNode(ids, size);
    const NodePtr callFibonacci = fibonacci->functionDef->MakeNode(ids, fibonacci->ID);
    const NodePtr setChecksum = BuildSetVariableNode(ids, checksum);
    for (const NodePtr& node : { mainBegin, getSize, callFibonacci, setChecksum })
        AttachNode(mainGraph, node);

    LinkPins(mainGraph, ids, mainBegin->Outputs[0], setChecksum->Inputs[0]);
    LinkPins(mainGraph, ids, getSize->Outputs[0], callFibonacci->Inputs[0]);
    LinkPins(mainGraph, ids, callFibonacci->Outputs[0], setChecksum->Inputs[1]);
    return script;
}

}

namespace
{
constexpr double Modulus = 1'000'000'007.0;
constexpr double Uint32Modulus = 4'294'967'296.0;

struct CaseBuilder
{
    CaseBuilder(const NodeRegistry& nodeRegistry, double defaultSize, double initialChecksum = 0.0)
        : registry(nodeRegistry)
    {
        script.ID = ids.GetNextId();
        script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
        size = NumberVariable("BenchmarkSize", defaultSize);
        checksum = NumberVariable("BenchmarkChecksum", initialChecksum);
    }

    ScriptPropertyPtr NumberVariable(const char* name, double defaultValue)
    {
        ScriptPropertyPtr variable = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        variable->type = PinType::Float;
        variable->defaultValue = Value(defaultValue);
        script.variables.push_back(variable);
        return variable;
    }

    ScriptPropertyPtr BoolVariable(const char* name, bool defaultValue)
    {
        ScriptPropertyPtr variable = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        variable->type = PinType::Bool;
        variable->defaultValue = Value(defaultValue);
        script.variables.push_back(variable);
        return variable;
    }

    ScriptPropertyPtr StringVariable(const char* name, const char* defaultValue = "")
    {
        ScriptPropertyPtr variable = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        variable->type = PinType::String;
        variable->defaultValue = Value(copyString(defaultValue, static_cast<int>(std::char_traits<char>::length(defaultValue))));
        script.variables.push_back(variable);
        return variable;
    }

    ScriptPropertyPtr ListVariable(const char* name, TypeRef elementType = PinType::Any)
    {
        ScriptPropertyPtr variable = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        variable->type = TypeRef::List(std::move(elementType));
        variable->defaultValue = Value(newList());
        script.variables.push_back(variable);
        return variable;
    }

    NodePtr Compiled(const char* name)
    {
        const CompiledNodeDefPtr definition = registry.FindCompiled(name);
        if (!definition)
            throw std::runtime_error(std::string("Missing compiled node definition: ") + name);
        return definition->MakeNode(ids);
    }

    NodePtr Native(const char* name)
    {
        const NativeFunctionDef* definition = registry.FindNative(name);
        if (!definition)
            throw std::runtime_error(std::string("Missing native node definition: ") + name);
        return definition->functionDef->MakeNode(ids, ScriptElementID::Invalid);
    }

    NodePtr Get(const ScriptPropertyPtr& variable) { return BuildGetVariableNode(ids, variable); }
    NodePtr Set(const ScriptPropertyPtr& variable) { return BuildSetVariableNode(ids, variable); }

    void Add(Graph& graph, const NodePtr& node)
    {
        NodeUtils::BuildNode(node);
        graph.AddNode(node);
    }

    void Add(Graph& graph, std::initializer_list<NodePtr> nodes)
    {
        for (const NodePtr& node : nodes)
            Add(graph, node);
    }

    void Link(Graph& graph, const Pin& output, const Pin& input)
    {
        graph.AddLink(::Link(ids.GetNextId(), output.ID, input.ID));
    }

    const Pin& Input(const NodePtr& node, const char* name)
    {
        Pin* pin = node->FindInputByName(name);
        if (!pin)
            throw std::runtime_error("Node '" + node->Name + "' has no input named '" + name + "'.");
        return *pin;
    }

    const Pin& Output(const NodePtr& node, const char* name)
    {
        Pin* pin = node->FindOutputByName(name);
        if (!pin)
            throw std::runtime_error("Node '" + node->Name + "' has no output named '" + name + "'.");
        return *pin;
    }

    Script Finish() { return std::move(script); }

    const NodeRegistry& registry;
    IDGenerator ids;
    Script script;
    ScriptPropertyPtr size;
    ScriptPropertyPtr checksum;
};

NodePtr AddNumber(CaseBuilder& builder, double right)
{
    NodePtr node = builder.Compiled("Math::Add");
    node->InputValues[1] = Value(right);
    return node;
}

NodePtr MultiplyNumber(CaseBuilder& builder, double right)
{
    NodePtr node = builder.Compiled("Math::Multiply");
    node->InputValues[1] = Value(right);
    return node;
}

NodePtr ModuloNumber(CaseBuilder& builder, double divisor)
{
    NodePtr node = builder.Compiled("Math::Modulo");
    node->InputValues[1] = Value(divisor);
    return node;
}

void ValidateAndSave(const Script& script, const std::filesystem::path& path)
{
    const ValidationReport validation = ScriptValidator::Validate(script);
    for (const ValidationDiagnostic& diagnostic : validation.diagnostics)
        std::clog << FormatDiagnostic(diagnostic) << '\n';
    if (validation.HasErrors())
        throw std::runtime_error("Generated graph validation failed for " + path.string());

    const SerializationResult result = ScriptSerializer::Save(script, path.string());
    if (!result)
        throw std::runtime_error("Could not save " + path.string() + ": " + result.error);
    std::cout << "Generated " << path.string() << '\n';
}

Script MakeNumberLoop(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 2'000'000.0);
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr indexOneBased = AddNumber(builder, 1.0);
    NodePtr square = builder.Compiled("Math::Multiply");
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr addChecksum = builder.Compiled("Math::Add");
    NodePtr modulo = ModuloNumber(builder, Modulus);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { begin, repeat, getSize, indexOneBased, square, getChecksum, addChecksum, modulo, setChecksum });

    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, repeat->Outputs[1], indexOneBased->Inputs[0]);
    builder.Link(graph, indexOneBased->Outputs[0], square->Inputs[0]);
    builder.Link(graph, indexOneBased->Outputs[0], square->Inputs[1]);
    builder.Link(graph, getChecksum->Outputs[0], addChecksum->Inputs[0]);
    builder.Link(graph, square->Outputs[0], addChecksum->Inputs[1]);
    builder.Link(graph, addChecksum->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, modulo->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

ScriptFunctionPtr AddModuloFunction(CaseBuilder& builder)
{
    ScriptFunctionPtr function = std::make_shared<ScriptFunction>(builder.ids.GetNextId(), "AddModulo");
    function->functionDef->description = "Adds two numbers and applies the benchmark modulus";
    function->functionDef->flags |= NodeDefinitionFlags::Pure;
    function->functionDef->inputs.push_back({ "Left", Value(0.0), builder.ids.GetNextId() });
    function->functionDef->inputs.push_back({ "Right", Value(0.0), builder.ids.GetNextId() });
    function->functionDef->outputs.push_back({ "Result", Value(0.0), builder.ids.GetNextId() });
    builder.script.functions.push_back(function);

    Graph& graph = function->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, function);
    NodePtr add = builder.Compiled("Math::Add");
    NodePtr modulo = ModuloNumber(builder, Modulus);
    NodePtr returnNode = BuildReturnNode(builder.ids, *function);
    builder.Add(graph, { begin, add, modulo, returnNode });
    builder.Link(graph, begin->Outputs[0], returnNode->Inputs[0]);
    builder.Link(graph, begin->Outputs[1], add->Inputs[0]);
    builder.Link(graph, begin->Outputs[2], add->Inputs[1]);
    builder.Link(graph, add->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, modulo->Outputs[0], returnNode->Inputs[1]);
    return function;
}

Script MakeFunctionCalls(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 2'000'000.0);
    ScriptFunctionPtr addModulo = AddModuloFunction(builder);

    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr call = addModulo->functionDef->MakeNode(builder.ids, addModulo->ID);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { begin, repeat, getSize, getChecksum, call, setChecksum });

    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, getChecksum->Outputs[0], builder.Input(call, "Left"));
    builder.Link(graph, repeat->Outputs[1], builder.Input(call, "Right"));
    builder.Link(graph, builder.Output(call, "Result"), setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeShortScript(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 10.0, 1.0);
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr multiply = MultiplyNumber(builder, 31.0);
    NodePtr add = builder.Compiled("Math::Add");
    NodePtr modulo = ModuloNumber(builder, Uint32Modulus);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { begin, repeat, getSize, getChecksum, multiply, add, modulo, setChecksum });

    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, getChecksum->Outputs[0], multiply->Inputs[0]);
    builder.Link(graph, multiply->Outputs[0], add->Inputs[0]);
    builder.Link(graph, repeat->Outputs[1], add->Inputs[1]);
    builder.Link(graph, add->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, modulo->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeConstantFolding(const NodeRegistry& registry, bool folded)
{
    CaseBuilder builder(registry, 2'000'000.0);
    ScriptPropertyPtr left;
    ScriptPropertyPtr right;
    ScriptPropertyPtr offset;
    if (!folded)
    {
        left = builder.NumberVariable("RuntimeLeft", 17.0);
        right = builder.NumberVariable("RuntimeRight", 23.0);
        offset = builder.NumberVariable("RuntimeOffset", 11.0);
    }

    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr multiply = builder.Compiled("Math::Multiply");
    multiply->InputValues[0] = Value(17.0);
    multiply->InputValues[1] = Value(23.0);
    NodePtr addOffset = builder.Compiled("Math::Add");
    addOffset->InputValues[1] = Value(11.0);
    NodePtr addChecksum = builder.Compiled("Math::Add");
    NodePtr modulo = ModuloNumber(builder, Modulus);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { begin, repeat, getSize, getChecksum, multiply, addOffset, addChecksum, modulo, setChecksum });

    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], setChecksum->Inputs[0]);
    if (!folded)
    {
        NodePtr getLeft = builder.Get(left);
        NodePtr getRight = builder.Get(right);
        NodePtr getOffset = builder.Get(offset);
        builder.Add(graph, { getLeft, getRight, getOffset });
        builder.Link(graph, getLeft->Outputs[0], multiply->Inputs[0]);
        builder.Link(graph, getRight->Outputs[0], multiply->Inputs[1]);
        builder.Link(graph, getOffset->Outputs[0], addOffset->Inputs[1]);
    }
    builder.Link(graph, multiply->Outputs[0], addOffset->Inputs[0]);
    builder.Link(graph, getChecksum->Outputs[0], addChecksum->Inputs[0]);
    builder.Link(graph, addOffset->Outputs[0], addChecksum->Inputs[1]);
    builder.Link(graph, addChecksum->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, modulo->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeNativeCall(const NodeRegistry& registry, bool native)
{
    CaseBuilder builder(registry, 2'000'000.0);
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr indexModulo = ModuloNumber(builder, 2'001.0);
    NodePtr subtract = builder.Compiled("Math::Subtract");
    subtract->InputValues[1] = Value(1'000.0);
    NodePtr absolute = native ? builder.Native("Math::Abs") : builder.Compiled("Math::Max");
    NodePtr negate;
    if (!native)
        negate = builder.Compiled("Math::Negate");
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr addChecksum = builder.Compiled("Math::Add");
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { begin, repeat, getSize, indexModulo, subtract, absolute, getChecksum, addChecksum, setChecksum });
    if (negate)
        builder.Add(graph, negate);

    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, repeat->Outputs[1], indexModulo->Inputs[0]);
    builder.Link(graph, indexModulo->Outputs[0], subtract->Inputs[0]);
    if (native)
    {
        builder.Link(graph, subtract->Outputs[0], builder.Input(absolute, "Value"));
    }
    else
    {
        builder.Link(graph, subtract->Outputs[0], negate->Inputs[0]);
        builder.Link(graph, subtract->Outputs[0], absolute->Inputs[0]);
        builder.Link(graph, negate->Outputs[0], absolute->Inputs[1]);
    }
    builder.Link(graph, getChecksum->Outputs[0], addChecksum->Inputs[0]);
    builder.Link(graph, absolute->Outputs[0], addChecksum->Inputs[1]);
    builder.Link(graph, addChecksum->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

ScriptFunctionPtr AddSplitFunction(CaseBuilder& builder)
{
    ScriptFunctionPtr function = std::make_shared<ScriptFunction>(builder.ids.GetNextId(), "SplitValue");
    function->functionDef->description = "Returns three consecutive values";
    function->functionDef->flags |= NodeDefinitionFlags::Pure;
    function->functionDef->inputs.push_back({ "Value", Value(0.0), builder.ids.GetNextId() });
    function->functionDef->outputs.push_back({ "First", Value(0.0), builder.ids.GetNextId() });
    function->functionDef->outputs.push_back({ "Second", Value(0.0), builder.ids.GetNextId() });
    function->functionDef->outputs.push_back({ "Third", Value(0.0), builder.ids.GetNextId() });
    builder.script.functions.push_back(function);

    Graph& graph = function->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, function);
    NodePtr addOne = AddNumber(builder, 1.0);
    NodePtr addTwo = AddNumber(builder, 2.0);
    NodePtr addThree = AddNumber(builder, 3.0);
    NodePtr returnNode = BuildReturnNode(builder.ids, *function);
    builder.Add(graph, { begin, addOne, addTwo, addThree, returnNode });
    builder.Link(graph, begin->Outputs[0], returnNode->Inputs[0]);
    builder.Link(graph, begin->Outputs[1], addOne->Inputs[0]);
    builder.Link(graph, begin->Outputs[1], addTwo->Inputs[0]);
    builder.Link(graph, begin->Outputs[1], addThree->Inputs[0]);
    builder.Link(graph, addOne->Outputs[0], returnNode->Inputs[1]);
    builder.Link(graph, addTwo->Outputs[0], returnNode->Inputs[2]);
    builder.Link(graph, addThree->Outputs[0], returnNode->Inputs[3]);
    return function;
}

Script MakeMultipleOutputs(const NodeRegistry& registry, bool multiple)
{
    CaseBuilder builder(registry, 2'000'000.0);
    ScriptFunctionPtr split;
    if (multiple)
        split = AddSplitFunction(builder);

    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr sum = builder.Compiled("Math::Add");
    sum->AddInput(builder.ids);
    sum->AddInput(builder.ids);
    NodePtr modulo = ModuloNumber(builder, Uint32Modulus);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { begin, repeat, getSize, getChecksum, sum, modulo, setChecksum });

    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, getChecksum->Outputs[0], sum->Inputs[0]);
    if (multiple)
    {
        NodePtr call = split->functionDef->MakeNode(builder.ids, split->ID);
        builder.Add(graph, call);
        builder.Link(graph, repeat->Outputs[1], builder.Input(call, "Value"));
        builder.Link(graph, builder.Output(call, "First"), sum->Inputs[1]);
        builder.Link(graph, builder.Output(call, "Second"), sum->Inputs[2]);
        builder.Link(graph, builder.Output(call, "Third"), sum->Inputs[3]);
    }
    else
    {
        NodePtr addOne = AddNumber(builder, 1.0);
        NodePtr addTwo = AddNumber(builder, 2.0);
        NodePtr addThree = AddNumber(builder, 3.0);
        builder.Add(graph, { addOne, addTwo, addThree });
        builder.Link(graph, repeat->Outputs[1], addOne->Inputs[0]);
        builder.Link(graph, repeat->Outputs[1], addTwo->Inputs[0]);
        builder.Link(graph, repeat->Outputs[1], addThree->Inputs[0]);
        builder.Link(graph, addOne->Outputs[0], sum->Inputs[1]);
        builder.Link(graph, addTwo->Outputs[0], sum->Inputs[2]);
        builder.Link(graph, addThree->Outputs[0], sum->Inputs[3]);
    }
    builder.Link(graph, sum->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, modulo->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeEquivalentForms(const NodeRegistry& registry, bool temporaries)
{
    CaseBuilder builder(registry, 2'000'000.0);
    ScriptPropertyPtr multiplied;
    ScriptPropertyPtr offset;
    if (temporaries)
    {
        multiplied = builder.NumberVariable("IntermediateMultiplied", 0.0);
        offset = builder.NumberVariable("IntermediateOffset", 0.0);
    }

    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr multiplyThree = MultiplyNumber(builder, 3.0);
    NodePtr addOne = AddNumber(builder, 1.0);
    NodePtr multiplyFive = MultiplyNumber(builder, 5.0);
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr addChecksum = builder.Compiled("Math::Add");
    NodePtr modulo = ModuloNumber(builder, Modulus);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { begin, repeat, getSize, multiplyThree, addOne, multiplyFive, getChecksum, addChecksum, modulo, setChecksum });
    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));

    if (!temporaries)
    {
        builder.Link(graph, repeat->Outputs[0], setChecksum->Inputs[0]);
        builder.Link(graph, repeat->Outputs[1], multiplyThree->Inputs[0]);
        builder.Link(graph, multiplyThree->Outputs[0], addOne->Inputs[0]);
        builder.Link(graph, addOne->Outputs[0], multiplyFive->Inputs[0]);
    }
    else
    {
        NodePtr setMultiplied = builder.Set(multiplied);
        NodePtr getMultiplied = builder.Get(multiplied);
        NodePtr setOffset = builder.Set(offset);
        NodePtr getOffset = builder.Get(offset);
        builder.Add(graph, { setMultiplied, getMultiplied, setOffset, getOffset });
        builder.Link(graph, repeat->Outputs[0], setMultiplied->Inputs[0]);
        builder.Link(graph, repeat->Outputs[1], multiplyThree->Inputs[0]);
        builder.Link(graph, multiplyThree->Outputs[0], setMultiplied->Inputs[1]);
        builder.Link(graph, setMultiplied->Outputs[0], setOffset->Inputs[0]);
        builder.Link(graph, getMultiplied->Outputs[0], addOne->Inputs[0]);
        builder.Link(graph, addOne->Outputs[0], setOffset->Inputs[1]);
        builder.Link(graph, setOffset->Outputs[0], setChecksum->Inputs[0]);
        builder.Link(graph, getOffset->Outputs[0], multiplyFive->Inputs[0]);
    }

    builder.Link(graph, getChecksum->Outputs[0], addChecksum->Inputs[0]);
    builder.Link(graph, multiplyFive->Outputs[0], addChecksum->Inputs[1]);
    builder.Link(graph, addChecksum->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, modulo->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakePatternMatching(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 2'000'000.0);
    ScriptPropertyPtr classification = builder.NumberVariable("Classification", 0.0);
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr valueModulo = ModuloNumber(builder, 128.0);
    NodePtr setClassZero = builder.Set(classification);
    setClassZero->InputValues[1] = Value(11.0);
    NodePtr setClassOneTwo = builder.Set(classification);
    setClassOneTwo->InputValues[1] = Value(17.0);
    NodePtr setClassUnderTen = builder.Set(classification);
    setClassUnderTen->InputValues[1] = Value(23.0);
    NodePtr setClassUnderHundred = builder.Set(classification);
    setClassUnderHundred->InputValues[1] = Value(31.0);
    NodePtr setClassDefault = builder.Set(classification);
    setClassDefault->InputValues[1] = Value(47.0);
    NodePtr branchZero = builder.Compiled("Flow::Branch");
    NodePtr equalsZero = builder.Compiled("Math::Equals");
    equalsZero->InputValues[1] = Value(0.0);
    NodePtr branchOneTwo = builder.Compiled("Flow::Branch");
    NodePtr lessThree = builder.Compiled("Math::Less Than");
    lessThree->InputValues[1] = Value(3.0);
    NodePtr branchTen = builder.Compiled("Flow::Branch");
    NodePtr lessTen = builder.Compiled("Math::Less Than");
    lessTen->InputValues[1] = Value(10.0);
    NodePtr branchHundred = builder.Compiled("Flow::Branch");
    NodePtr lessHundred = builder.Compiled("Math::Less Than");
    lessHundred->InputValues[1] = Value(100.0);
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr getClass = builder.Get(classification);
    NodePtr addChecksum = builder.Compiled("Math::Add");
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, {
        begin, repeat, getSize, valueModulo, setClassZero, setClassOneTwo, setClassUnderTen, setClassUnderHundred, setClassDefault,
        branchZero, equalsZero, branchOneTwo, lessThree, branchTen, lessTen, branchHundred, lessHundred, getChecksum, getClass,
        addChecksum, setChecksum
    });

    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[1], valueModulo->Inputs[0]);
    builder.Link(graph, repeat->Outputs[0], branchZero->Inputs[0]);
    builder.Link(graph, valueModulo->Outputs[0], equalsZero->Inputs[0]);
    builder.Link(graph, equalsZero->Outputs[0], branchZero->Inputs[1]);
    builder.Link(graph, branchZero->Outputs[0], setClassZero->Inputs[0]);
    builder.Link(graph, branchZero->Outputs[1], branchOneTwo->Inputs[0]);
    builder.Link(graph, valueModulo->Outputs[0], lessThree->Inputs[0]);
    builder.Link(graph, lessThree->Outputs[0], branchOneTwo->Inputs[1]);
    builder.Link(graph, branchOneTwo->Outputs[0], setClassOneTwo->Inputs[0]);
    builder.Link(graph, branchOneTwo->Outputs[1], branchTen->Inputs[0]);
    builder.Link(graph, valueModulo->Outputs[0], lessTen->Inputs[0]);
    builder.Link(graph, lessTen->Outputs[0], branchTen->Inputs[1]);
    builder.Link(graph, branchTen->Outputs[0], setClassUnderTen->Inputs[0]);
    builder.Link(graph, branchTen->Outputs[1], branchHundred->Inputs[0]);
    builder.Link(graph, valueModulo->Outputs[0], lessHundred->Inputs[0]);
    builder.Link(graph, lessHundred->Outputs[0], branchHundred->Inputs[1]);
    builder.Link(graph, branchHundred->Outputs[0], setClassUnderHundred->Inputs[0]);
    builder.Link(graph, branchHundred->Outputs[1], setClassDefault->Inputs[0]);
    for (const NodePtr& setter : { setClassZero, setClassOneTwo, setClassUnderTen, setClassUnderHundred, setClassDefault })
        builder.Link(graph, setter->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, getChecksum->Outputs[0], addChecksum->Inputs[0]);
    builder.Link(graph, getClass->Outputs[0], addChecksum->Inputs[1]);
    builder.Link(graph, addChecksum->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

struct FreshList
{
    NodePtr setList;
    NodePtr clearList;
};

FreshList InitializeFreshList(CaseBuilder& builder, Graph& graph, const NodePtr& incoming, const ScriptPropertyPtr& list)
{
    NodePtr makeList = builder.Native("List::MakeList");
    makeList->TypeOverrides["T"] = list->type.ElementType();
    makeList->AddInput(builder.ids);
    makeList->InputValues[0] = MakeValueFromType(list->type.ElementType());
    NodePtr setList = builder.Set(list);
    NodePtr getList = builder.Get(list);
    NodePtr clearList = builder.Native("List::Clear");
    builder.Add(graph, { makeList, setList, getList, clearList });
    builder.Link(graph, incoming->Outputs[0], setList->Inputs[0]);
    builder.Link(graph, builder.Output(makeList, "List"), setList->Inputs[1]);
    builder.Link(graph, setList->Outputs[0], clearList->Inputs[0]);
    builder.Link(graph, getList->Outputs[0], builder.Input(clearList, "List"));
    return { setList, clearList };
}

struct LcgLoop
{
    NodePtr repeat;
    NodePtr push;
};

LcgLoop AppendLcgValues(
    CaseBuilder& builder, Graph& graph, const Pin& incomingFlow, const ScriptPropertyPtr& list, const ScriptPropertyPtr& state, bool limitToMillion)
{
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr getStateForMultiply = builder.Get(state);
    NodePtr multiply = MultiplyNumber(builder, 1'664'525.0);
    NodePtr add = AddNumber(builder, 1'013'904'223.0);
    NodePtr stateModulo = ModuloNumber(builder, Uint32Modulus);
    NodePtr setState = builder.Set(state);
    NodePtr getList = builder.Get(list);
    NodePtr getStateForPush = builder.Get(state);
    NodePtr push = builder.Native("List::Push");
    push->TypeOverrides["T"] = list->type.ElementType();
    NodePtr valueModulo;
    if (limitToMillion)
        valueModulo = ModuloNumber(builder, 1'000'000.0);
    builder.Add(graph, { repeat, getSize, getStateForMultiply, multiply, add, stateModulo, setState, getList, getStateForPush, push });
    if (valueModulo)
        builder.Add(graph, valueModulo);

    builder.Link(graph, incomingFlow, repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], setState->Inputs[0]);
    builder.Link(graph, getStateForMultiply->Outputs[0], multiply->Inputs[0]);
    builder.Link(graph, multiply->Outputs[0], add->Inputs[0]);
    builder.Link(graph, add->Outputs[0], stateModulo->Inputs[0]);
    builder.Link(graph, stateModulo->Outputs[0], setState->Inputs[1]);
    builder.Link(graph, setState->Outputs[0], push->Inputs[0]);
    builder.Link(graph, getList->Outputs[0], builder.Input(push, "List"));
    if (valueModulo)
    {
        builder.Link(graph, getStateForPush->Outputs[0], valueModulo->Inputs[0]);
        builder.Link(graph, valueModulo->Outputs[0], builder.Input(push, "Value"));
    }
    else
    {
        builder.Link(graph, getStateForPush->Outputs[0], builder.Input(push, "Value"));
    }
    return { repeat, push };
}

ScriptFunctionPtr AddUnaryFunction(
    CaseBuilder& builder, const char* name, const char* outputName, const Value& outputDefault,
    const std::function<NodePtr(CaseBuilder&, Graph&, const NodePtr&)>& expression)
{
    ScriptFunctionPtr function = std::make_shared<ScriptFunction>(builder.ids.GetNextId(), name);
    function->functionDef->flags |= NodeDefinitionFlags::Pure;
    function->functionDef->inputs.push_back({ "Value", Value(0.0), builder.ids.GetNextId() });
    function->functionDef->outputs.push_back({ outputName, outputDefault, builder.ids.GetNextId() });
    builder.script.functions.push_back(function);

    Graph& graph = function->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, function);
    NodePtr result = expression(builder, graph, begin);
    NodePtr returnNode = BuildReturnNode(builder.ids, *function);
    builder.Add(graph, returnNode);
    builder.Link(graph, begin->Outputs[0], returnNode->Inputs[0]);
    builder.Link(graph, result->Outputs[0], returnNode->Inputs[1]);
    return function;
}

ScriptFunctionPtr AddReducerFunction(CaseBuilder& builder)
{
    ScriptFunctionPtr function = std::make_shared<ScriptFunction>(builder.ids.GetNextId(), "ReduceModulo");
    function->functionDef->flags |= NodeDefinitionFlags::Pure;
    function->functionDef->inputs.push_back({ "Total", Value(0.0), builder.ids.GetNextId() });
    function->functionDef->inputs.push_back({ "Value", Value(0.0), builder.ids.GetNextId() });
    function->functionDef->outputs.push_back({ "Result", Value(0.0), builder.ids.GetNextId() });
    builder.script.functions.push_back(function);

    Graph& graph = function->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, function);
    NodePtr add = builder.Compiled("Math::Add");
    NodePtr modulo = ModuloNumber(builder, Modulus);
    NodePtr returnNode = BuildReturnNode(builder.ids, *function);
    builder.Add(graph, { begin, add, modulo, returnNode });
    builder.Link(graph, begin->Outputs[0], returnNode->Inputs[0]);
    builder.Link(graph, begin->Outputs[1], add->Inputs[0]);
    builder.Link(graph, begin->Outputs[2], add->Inputs[1]);
    builder.Link(graph, add->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, modulo->Outputs[0], returnNode->Inputs[1]);
    return function;
}

Script MakeListProcessing(const NodeRegistry& registry, bool callbacks)
{
    CaseBuilder builder(registry, 500'000.0);
    ScriptPropertyPtr values = builder.ListVariable("Values", PinType::Float);
    ScriptPropertyPtr state = builder.NumberVariable("LcgState", 0xC0FFEE);
    ScriptPropertyPtr selectedCount = builder.NumberVariable("SelectedCount", 0.0);
    ScriptPropertyPtr selectedTotal = builder.NumberVariable("SelectedTotal", 0.0);

    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    builder.Add(graph, begin);
    const FreshList fresh = InitializeFreshList(builder, graph, begin, values);
    const LcgLoop generation = AppendLcgValues(builder, graph, fresh.clearList->Outputs[0], values, state, true);
    NodePtr getValues = builder.Get(values);
    builder.Add(graph, getValues);

    if (callbacks)
    {
        ScriptFunctionPtr transform = AddUnaryFunction(builder, "Transform", "Result", Value(0.0), [](CaseBuilder& inner, Graph& functionGraph, const NodePtr& functionBegin)
        {
            NodePtr multiply = MultiplyNumber(inner, 3.0);
            NodePtr add = AddNumber(inner, 1.0);
            NodePtr modulo = ModuloNumber(inner, Modulus);
            inner.Add(functionGraph, { functionBegin, multiply, add, modulo });
            inner.Link(functionGraph, functionBegin->Outputs[1], multiply->Inputs[0]);
            inner.Link(functionGraph, multiply->Outputs[0], add->Inputs[0]);
            inner.Link(functionGraph, add->Outputs[0], modulo->Inputs[0]);
            return modulo;
        });
        ScriptFunctionPtr predicate = AddUnaryFunction(builder, "IsEven", "Result", Value(false), [](CaseBuilder& inner, Graph& functionGraph, const NodePtr& functionBegin)
        {
            NodePtr modulo = ModuloNumber(inner, 2.0);
            NodePtr equals = inner.Compiled("Math::Equals");
            equals->InputValues[1] = Value(0.0);
            inner.Add(functionGraph, { functionBegin, modulo, equals });
            inner.Link(functionGraph, functionBegin->Outputs[1], modulo->Inputs[0]);
            inner.Link(functionGraph, modulo->Outputs[0], equals->Inputs[0]);
            return equals;
        });
        ScriptFunctionPtr reducer = AddReducerFunction(builder);

        NodePtr getTransform = BuildGetFunctionNode(builder.ids, transform->functionDef, transform->ID);
        NodePtr getPredicate = BuildGetFunctionNode(builder.ids, predicate->functionDef, predicate->ID);
        NodePtr getReducer = BuildGetFunctionNode(builder.ids, reducer->functionDef, reducer->ID);
        NodePtr map = builder.Native("Functional::Map");
        NodePtr filter = builder.Native("Functional::Filter");
        NodePtr reduce = builder.Native("Functional::Reduce");
        reduce->InputValues[2] = Value(0.0);
        NodePtr length = builder.Native("List::Length");
        NodePtr countScale = MultiplyNumber(builder, Modulus);
        NodePtr combine = builder.Compiled("Math::Add");
        NodePtr setChecksum = builder.Set(builder.checksum);
        builder.Add(graph, { getTransform, getPredicate, getReducer, map, filter, reduce, length, countScale, combine, setChecksum });

        builder.Link(graph, generation.repeat->Outputs[2], setChecksum->Inputs[0]);
        builder.Link(graph, getValues->Outputs[0], builder.Input(map, "Iterable"));
        builder.Link(graph, getTransform->Outputs[0], builder.Input(map, "Function"));
        builder.Link(graph, builder.Output(map, "Result"), builder.Input(filter, "Iterable"));
        builder.Link(graph, getPredicate->Outputs[0], builder.Input(filter, "Function"));
        builder.Link(graph, builder.Output(filter, "Result"), builder.Input(reduce, "Iterable"));
        builder.Link(graph, getReducer->Outputs[0], builder.Input(reduce, "Function"));
        builder.Link(graph, builder.Output(filter, "Result"), builder.Input(length, "List"));
        builder.Link(graph, builder.Output(length, "Length"), countScale->Inputs[0]);
        builder.Link(graph, countScale->Outputs[0], combine->Inputs[0]);
        builder.Link(graph, builder.Output(reduce, "Result"), combine->Inputs[1]);
        builder.Link(graph, combine->Outputs[0], setChecksum->Inputs[1]);
    }
    else
    {
        NodePtr forIn = builder.Compiled("Flow::For In");
        NodePtr multiply = MultiplyNumber(builder, 3.0);
        NodePtr addOne = AddNumber(builder, 1.0);
        NodePtr transformedModulo = ModuloNumber(builder, Modulus);
        NodePtr parity = ModuloNumber(builder, 2.0);
        NodePtr equalsZero = builder.Compiled("Math::Equals");
        equalsZero->InputValues[1] = Value(0.0);
        NodePtr branch = builder.Compiled("Flow::Branch");
        NodePtr getCount = builder.Get(selectedCount);
        NodePtr incrementCount = AddNumber(builder, 1.0);
        NodePtr setCount = builder.Set(selectedCount);
        NodePtr getTotal = builder.Get(selectedTotal);
        NodePtr addTotal = builder.Compiled("Math::Add");
        NodePtr totalModulo = ModuloNumber(builder, Modulus);
        NodePtr setTotal = builder.Set(selectedTotal);
        NodePtr getFinalCount = builder.Get(selectedCount);
        NodePtr countScale = MultiplyNumber(builder, Modulus);
        NodePtr getFinalTotal = builder.Get(selectedTotal);
        NodePtr combine = builder.Compiled("Math::Add");
        NodePtr setChecksum = builder.Set(builder.checksum);
        builder.Add(graph, {
            forIn, multiply, addOne, transformedModulo, parity, equalsZero, branch, getCount, incrementCount, setCount, getTotal, addTotal,
            totalModulo, setTotal, getFinalCount, countScale, getFinalTotal, combine, setChecksum
        });

        builder.Link(graph, generation.repeat->Outputs[2], forIn->Inputs[0]);
        builder.Link(graph, getValues->Outputs[0], builder.Input(forIn, "Iterable"));
        builder.Link(graph, forIn->Outputs[1], multiply->Inputs[0]);
        builder.Link(graph, multiply->Outputs[0], addOne->Inputs[0]);
        builder.Link(graph, addOne->Outputs[0], transformedModulo->Inputs[0]);
        builder.Link(graph, transformedModulo->Outputs[0], parity->Inputs[0]);
        builder.Link(graph, parity->Outputs[0], equalsZero->Inputs[0]);
        builder.Link(graph, forIn->Outputs[0], branch->Inputs[0]);
        builder.Link(graph, equalsZero->Outputs[0], branch->Inputs[1]);
        builder.Link(graph, branch->Outputs[0], setCount->Inputs[0]);
        builder.Link(graph, getCount->Outputs[0], incrementCount->Inputs[0]);
        builder.Link(graph, incrementCount->Outputs[0], setCount->Inputs[1]);
        builder.Link(graph, setCount->Outputs[0], setTotal->Inputs[0]);
        builder.Link(graph, getTotal->Outputs[0], addTotal->Inputs[0]);
        builder.Link(graph, transformedModulo->Outputs[0], addTotal->Inputs[1]);
        builder.Link(graph, addTotal->Outputs[0], totalModulo->Inputs[0]);
        builder.Link(graph, totalModulo->Outputs[0], setTotal->Inputs[1]);
        builder.Link(graph, forIn->Outputs[2], setChecksum->Inputs[0]);
        builder.Link(graph, getFinalCount->Outputs[0], countScale->Inputs[0]);
        builder.Link(graph, countScale->Outputs[0], combine->Inputs[0]);
        builder.Link(graph, getFinalTotal->Outputs[0], combine->Inputs[1]);
        builder.Link(graph, combine->Outputs[0], setChecksum->Inputs[1]);
    }
    return builder.Finish();
}

Script MakeSorting(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 500'000.0);
    ScriptPropertyPtr values = builder.ListVariable("Values", PinType::Float);
    ScriptPropertyPtr state = builder.NumberVariable("LcgState", 0xC0FFEE);
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    builder.Add(graph, begin);
    const FreshList fresh = InitializeFreshList(builder, graph, begin, values);
    const LcgLoop generation = AppendLcgValues(builder, graph, fresh.clearList->Outputs[0], values, state, false);
    NodePtr getValues = builder.Get(values);
    NodePtr sort = builder.Native("List::Sort");
    NodePtr forIn = builder.Compiled("Flow::For In");
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr add = builder.Compiled("Math::Add");
    NodePtr modulo = ModuloNumber(builder, Modulus);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { getValues, sort, forIn, getChecksum, add, modulo, setChecksum });
    builder.Link(graph, generation.repeat->Outputs[2], forIn->Inputs[0]);
    builder.Link(graph, getValues->Outputs[0], builder.Input(sort, "List"));
    builder.Link(graph, builder.Output(sort, "Result"), builder.Input(forIn, "Iterable"));
    builder.Link(graph, forIn->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, getChecksum->Outputs[0], add->Inputs[0]);
    builder.Link(graph, forIn->Outputs[1], add->Inputs[1]);
    builder.Link(graph, add->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, modulo->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeStringBuilding(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 200'000.0);
    ScriptPropertyPtr parts = builder.ListVariable("Parts", PinType::String);
    ScriptPropertyPtr lengths = builder.ListVariable("Lengths", PinType::Float);
    ScriptPropertyPtr lengthSum = builder.NumberVariable("LengthSum", 0.0);
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    builder.Add(graph, begin);
    const FreshList freshParts = InitializeFreshList(builder, graph, begin, parts);
    const FreshList freshLengths = InitializeFreshList(builder, graph, freshParts.clearList, lengths);

    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr indexModulo = ModuloNumber(builder, 10'000.0);
    NodePtr toString = builder.Compiled("String::ToString");
    NodePtr append = builder.Compiled("String::Append");
    append->InputValues[0] = Value(copyString(" item-", 6));
    append->AddInput(builder.ids);
    append->InputValues[2] = Value(copyString(" ", 1));
    NodePtr trim = builder.Native("String::Trim");
    NodePtr replace = builder.Native("String::Replace");
    replace->InputValues[1] = Value(copyString("-", 1));
    replace->InputValues[2] = Value(copyString(":", 1));
    NodePtr upper = builder.Native("String::ToUpper");
    NodePtr length = builder.Native("String::Length");
    NodePtr getPartsForPush = builder.Get(parts);
    NodePtr pushPart = builder.Native("List::Push");
    pushPart->TypeOverrides["T"] = PinType::String;
    NodePtr getLengthsForPush = builder.Get(lengths);
    NodePtr pushLength = builder.Native("List::Push");
    pushLength->TypeOverrides["T"] = PinType::Float;
    NodePtr getLengthSum = builder.Get(lengthSum);
    NodePtr addLength = builder.Compiled("Math::Add");
    NodePtr setLengthSum = builder.Set(lengthSum);
    builder.Add(graph, {
        repeat, getSize, indexModulo, toString, append, trim, replace, upper, length, getPartsForPush, pushPart, getLengthsForPush, pushLength,
        getLengthSum, addLength, setLengthSum
    });
    builder.Link(graph, freshLengths.clearList->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[1], indexModulo->Inputs[0]);
    builder.Link(graph, indexModulo->Outputs[0], toString->Inputs[0]);
    builder.Link(graph, toString->Outputs[0], append->Inputs[1]);
    builder.Link(graph, append->Outputs[0], builder.Input(trim, "Text"));
    builder.Link(graph, builder.Output(trim, "Result"), builder.Input(replace, "Text"));
    builder.Link(graph, builder.Output(replace, "Result"), builder.Input(upper, "Text"));
    builder.Link(graph, builder.Output(upper, "Uppercase"), builder.Input(length, "Value"));
    builder.Link(graph, repeat->Outputs[0], pushPart->Inputs[0]);
    builder.Link(graph, getPartsForPush->Outputs[0], builder.Input(pushPart, "List"));
    builder.Link(graph, builder.Output(upper, "Uppercase"), builder.Input(pushPart, "Value"));
    builder.Link(graph, pushPart->Outputs[0], pushLength->Inputs[0]);
    builder.Link(graph, getLengthsForPush->Outputs[0], builder.Input(pushLength, "List"));
    builder.Link(graph, builder.Output(length, "Length"), builder.Input(pushLength, "Value"));
    builder.Link(graph, pushLength->Outputs[0], setLengthSum->Inputs[0]);
    builder.Link(graph, getLengthSum->Outputs[0], addLength->Inputs[0]);
    builder.Link(graph, builder.Output(length, "Length"), addLength->Inputs[1]);
    builder.Link(graph, addLength->Outputs[0], setLengthSum->Inputs[1]);

    NodePtr getPartsForJoin = builder.Get(parts);
    NodePtr join = builder.Native("String::Join");
    join->InputValues[1] = Value(copyString("|", 1));
    NodePtr combinedLength = builder.Native("String::Length");
    NodePtr scaleCombinedLength = MultiplyNumber(builder, Modulus);
    NodePtr getFinalLengthSum = builder.Get(lengthSum);
    NodePtr combine = builder.Compiled("Math::Add");
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { getPartsForJoin, join, combinedLength, scaleCombinedLength, getFinalLengthSum, combine, setChecksum });
    builder.Link(graph, repeat->Outputs[2], setChecksum->Inputs[0]);
    builder.Link(graph, getPartsForJoin->Outputs[0], builder.Input(join, "Values"));
    builder.Link(graph, builder.Output(join, "Result"), builder.Input(combinedLength, "Value"));
    builder.Link(graph, builder.Output(combinedLength, "Length"), scaleCombinedLength->Inputs[0]);
    builder.Link(graph, scaleCombinedLength->Outputs[0], combine->Inputs[0]);
    builder.Link(graph, getFinalLengthSum->Outputs[0], combine->Inputs[1]);
    builder.Link(graph, combine->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeDynamicValues(const NodeRegistry& registry, bool mixed)
{
    CaseBuilder builder(registry, 500'000.0);
    ScriptPropertyPtr values = builder.ListVariable("Values", mixed ? TypeRef(PinType::Any) : TypeRef(PinType::Float));
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    builder.Add(graph, begin);
    const FreshList fresh = InitializeFreshList(builder, graph, begin, values);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr getList = builder.Get(values);
    NodePtr pushNumber = builder.Native("List::Push");
    NodePtr numberModulo = ModuloNumber(builder, 1'000.0);
    builder.Add(graph, { repeat, getSize, getList, pushNumber, numberModulo });
    builder.Link(graph, fresh.clearList->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, getList->Outputs[0], builder.Input(pushNumber, "List"));
    builder.Link(graph, repeat->Outputs[1], numberModulo->Inputs[0]);
    builder.Link(graph, numberModulo->Outputs[0], builder.Input(pushNumber, "Value"));

    if (!mixed)
    {
        builder.Link(graph, repeat->Outputs[0], pushNumber->Inputs[0]);
    }
    else
    {
        pushNumber->TypeOverrides["T"] = PinType::Any;
        NodePtr selector = ModuloNumber(builder, 4.0);
        NodePtr equalsZero = builder.Compiled("Math::Equals");
        equalsZero->InputValues[1] = Value(0.0);
        NodePtr branchZero = builder.Compiled("Flow::Branch");
        NodePtr equalsOne = builder.Compiled("Math::Equals");
        equalsOne->InputValues[1] = Value(1.0);
        NodePtr branchOne = builder.Compiled("Flow::Branch");
        NodePtr equalsTwo = builder.Compiled("Math::Equals");
        equalsTwo->InputValues[1] = Value(2.0);
        NodePtr branchTwo = builder.Compiled("Flow::Branch");
        NodePtr numberToString = builder.Compiled("String::ToString");
        NodePtr appendStringPrefix = builder.Compiled("String::Append");
        appendStringPrefix->InputValues[0] = Value(copyString("v", 1));
        NodePtr pushString = builder.Native("List::Push");
        NodePtr pushBool = builder.Native("List::Push");
        pushBool->InputValues[2] = Value(true);
        NodePtr pushNil = builder.Native("List::Push");
        pushNil->InputValues[2] = Value();
        pushString->TypeOverrides["T"] = PinType::Any;
        pushBool->TypeOverrides["T"] = PinType::Any;
        pushNil->TypeOverrides["T"] = PinType::Any;
        builder.Add(graph, {
            selector, equalsZero, branchZero, equalsOne, branchOne, equalsTwo, branchTwo, numberToString, appendStringPrefix, pushString, pushBool, pushNil
        });
        for (const NodePtr& push : { pushString, pushBool, pushNil })
            builder.Link(graph, getList->Outputs[0], builder.Input(push, "List"));
        builder.Link(graph, repeat->Outputs[1], selector->Inputs[0]);
        builder.Link(graph, repeat->Outputs[0], branchZero->Inputs[0]);
        builder.Link(graph, selector->Outputs[0], equalsZero->Inputs[0]);
        builder.Link(graph, equalsZero->Outputs[0], branchZero->Inputs[1]);
        builder.Link(graph, branchZero->Outputs[0], pushNumber->Inputs[0]);
        builder.Link(graph, branchZero->Outputs[1], branchOne->Inputs[0]);
        builder.Link(graph, selector->Outputs[0], equalsOne->Inputs[0]);
        builder.Link(graph, equalsOne->Outputs[0], branchOne->Inputs[1]);
        builder.Link(graph, branchOne->Outputs[0], pushString->Inputs[0]);
        builder.Link(graph, numberModulo->Outputs[0], numberToString->Inputs[0]);
        builder.Link(graph, numberToString->Outputs[0], appendStringPrefix->Inputs[1]);
        builder.Link(graph, appendStringPrefix->Outputs[0], builder.Input(pushString, "Value"));
        builder.Link(graph, branchOne->Outputs[1], branchTwo->Inputs[0]);
        builder.Link(graph, selector->Outputs[0], equalsTwo->Inputs[0]);
        builder.Link(graph, equalsTwo->Outputs[0], branchTwo->Inputs[1]);
        builder.Link(graph, branchTwo->Outputs[0], pushBool->Inputs[0]);
        builder.Link(graph, branchTwo->Outputs[1], pushNil->Inputs[0]);
    }

    NodePtr length = builder.Native("List::Length");
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { length, setChecksum });
    builder.Link(graph, repeat->Outputs[2], setChecksum->Inputs[0]);
    builder.Link(graph, getList->Outputs[0], builder.Input(length, "List"));
    builder.Link(graph, builder.Output(length, "Length"), setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeObjects(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 200'000.0);
    ScriptClassPtr counter = std::make_shared<ScriptClass>(builder.ids.GetNextId(), "Counter");
    ScriptPropertyPtr value = std::make_shared<ScriptProperty>(builder.ids.GetNextId(), "Value");
    value->type = PinType::Float;
    value->defaultValue = Value(0.0);
    ScriptPropertyPtr updates = std::make_shared<ScriptProperty>(builder.ids.GetNextId(), "Updates");
    updates->type = PinType::Float;
    updates->defaultValue = Value(0.0);
    counter->properties = { value, updates };
    builder.script.classes.push_back(counter);
    const TypeRef counterType = TypeRef::Object(counter->ID.id, counter->Name);
    ScriptPropertyPtr counters = builder.ListVariable("Counters", counterType);

    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    builder.Add(graph, begin);
    const FreshList fresh = InitializeFreshList(builder, graph, begin, counters);

    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr construct = BuildConstructObjectNode(builder.ids, counter);
    NodePtr setInitialValue = BuildSetPropertyNode(builder.ids, value, value->ID, counterType);
    NodePtr getCountersForPush = builder.Get(counters);
    NodePtr push = builder.Native("List::Push");
    push->TypeOverrides["T"] = counterType;
    builder.Add(graph, { repeat, getSize, construct, setInitialValue, getCountersForPush, push });
    builder.Link(graph, fresh.clearList->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], construct->Inputs[0]);
    builder.Link(graph, construct->Outputs[0], setInitialValue->Inputs[0]);
    builder.Link(graph, construct->Outputs[1], setInitialValue->Inputs[1]);
    builder.Link(graph, repeat->Outputs[1], setInitialValue->Inputs[2]);
    builder.Link(graph, setInitialValue->Outputs[0], push->Inputs[0]);
    builder.Link(graph, getCountersForPush->Outputs[0], builder.Input(push, "List"));
    builder.Link(graph, construct->Outputs[1], builder.Input(push, "Value"));

    NodePtr forIn = builder.Compiled("Flow::For In");
    NodePtr getCountersForIteration = builder.Get(counters);
    NodePtr getValue = BuildGetPropertyNode(builder.ids, value, value->ID, counterType);
    NodePtr multiply = MultiplyNumber(builder, 3.0);
    NodePtr addOne = AddNumber(builder, 1.0);
    NodePtr modulo = ModuloNumber(builder, Modulus);
    NodePtr setValue = BuildSetPropertyNode(builder.ids, value, value->ID, counterType);
    NodePtr getUpdates = BuildGetPropertyNode(builder.ids, updates, updates->ID, counterType);
    NodePtr incrementUpdates = AddNumber(builder, 1.0);
    NodePtr setUpdates = BuildSetPropertyNode(builder.ids, updates, updates->ID, counterType);
    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr addChecksum = builder.Compiled("Math::Add");
    addChecksum->AddInput(builder.ids);
    NodePtr checksumModulo = ModuloNumber(builder, Modulus);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, {
        forIn, getCountersForIteration, getValue, multiply, addOne, modulo, setValue, getUpdates, incrementUpdates, setUpdates, getChecksum,
        addChecksum, checksumModulo, setChecksum
    });
    builder.Link(graph, repeat->Outputs[2], forIn->Inputs[0]);
    builder.Link(graph, getCountersForIteration->Outputs[0], builder.Input(forIn, "Iterable"));
    builder.Link(graph, forIn->Outputs[1], getValue->Inputs[0]);
    builder.Link(graph, getValue->Outputs[0], multiply->Inputs[0]);
    builder.Link(graph, multiply->Outputs[0], addOne->Inputs[0]);
    builder.Link(graph, addOne->Outputs[0], modulo->Inputs[0]);
    builder.Link(graph, forIn->Outputs[0], setValue->Inputs[0]);
    builder.Link(graph, forIn->Outputs[1], setValue->Inputs[1]);
    builder.Link(graph, modulo->Outputs[0], setValue->Inputs[2]);
    builder.Link(graph, setValue->Outputs[0], setUpdates->Inputs[0]);
    builder.Link(graph, forIn->Outputs[1], getUpdates->Inputs[0]);
    builder.Link(graph, getUpdates->Outputs[0], incrementUpdates->Inputs[0]);
    builder.Link(graph, forIn->Outputs[1], setUpdates->Inputs[1]);
    builder.Link(graph, incrementUpdates->Outputs[0], setUpdates->Inputs[2]);
    builder.Link(graph, setUpdates->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, getChecksum->Outputs[0], addChecksum->Inputs[0]);
    builder.Link(graph, setValue->Outputs[1], addChecksum->Inputs[1]);
    builder.Link(graph, setUpdates->Outputs[1], addChecksum->Inputs[2]);
    builder.Link(graph, addChecksum->Outputs[0], checksumModulo->Inputs[0]);
    builder.Link(graph, checksumModulo->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeGcPressure(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 200'000.0);
    ScriptClassPtr temporaryClass = std::make_shared<ScriptClass>(builder.ids.GetNextId(), "TemporaryNode");
    ScriptPropertyPtr value = std::make_shared<ScriptProperty>(builder.ids.GetNextId(), "Value");
    value->type = PinType::Float;
    value->defaultValue = Value(0.0);
    temporaryClass->properties.push_back(value);
    builder.script.classes.push_back(temporaryClass);
    ScriptPropertyPtr objectSlot = std::make_shared<ScriptProperty>(builder.ids.GetNextId(), "TemporaryObject");
    objectSlot->type = TypeRef::Object(temporaryClass->ID.id, temporaryClass->Name);
    objectSlot->defaultValue = Value();
    builder.script.variables.push_back(objectSlot);
    ScriptPropertyPtr stringSlot = builder.StringVariable("TemporaryString");
    ScriptPropertyPtr listSlot = builder.ListVariable("TemporaryList", PinType::Float);

    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr repeat = builder.Compiled("Flow::Repeat");
    NodePtr getSize = builder.Get(builder.size);
    NodePtr construct = BuildConstructObjectNode(builder.ids, temporaryClass);
    NodePtr setObject = builder.Set(objectSlot);
    NodePtr toString = builder.Compiled("String::ToString");
    NodePtr append = builder.Compiled("String::Append");
    append->InputValues[0] = Value(copyString("temporary-", 10));
    NodePtr setString = builder.Set(stringSlot);
    NodePtr makeList = builder.Native("List::MakeList");
    makeList->TypeOverrides["T"] = PinType::Float;
    makeList->AddInput(builder.ids);
    makeList->AddInput(builder.ids);
    makeList->AddInput(builder.ids);
    NodePtr addOne = AddNumber(builder, 1.0);
    NodePtr addTwo = AddNumber(builder, 2.0);
    NodePtr setList = builder.Set(listSlot);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, {
        begin, repeat, getSize, construct, setObject, toString, append, setString, makeList, addOne, addTwo, setList, setChecksum
    });
    builder.Link(graph, begin->Outputs[0], repeat->Inputs[0]);
    builder.Link(graph, getSize->Outputs[0], builder.Input(repeat, "Count"));
    builder.Link(graph, repeat->Outputs[0], construct->Inputs[0]);
    builder.Link(graph, construct->Outputs[0], setObject->Inputs[0]);
    builder.Link(graph, construct->Outputs[1], setObject->Inputs[1]);
    builder.Link(graph, setObject->Outputs[0], setString->Inputs[0]);
    builder.Link(graph, repeat->Outputs[1], toString->Inputs[0]);
    builder.Link(graph, toString->Outputs[0], append->Inputs[1]);
    builder.Link(graph, append->Outputs[0], setString->Inputs[1]);
    builder.Link(graph, setString->Outputs[0], setList->Inputs[0]);
    builder.Link(graph, repeat->Outputs[1], makeList->Inputs[0]);
    builder.Link(graph, repeat->Outputs[1], addOne->Inputs[0]);
    builder.Link(graph, repeat->Outputs[1], addTwo->Inputs[0]);
    builder.Link(graph, addOne->Outputs[0], makeList->Inputs[1]);
    builder.Link(graph, addTwo->Outputs[0], makeList->Inputs[2]);
    builder.Link(graph, builder.Output(makeList, "List"), setList->Inputs[1]);
    builder.Link(graph, setList->Outputs[0], setChecksum->Inputs[0]);
    builder.Link(graph, repeat->Outputs[1], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakePrimeSieve(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 250'000.0);
    ScriptPropertyPtr flags = builder.ListVariable("PrimeFlags", PinType::Bool);
    ScriptPropertyPtr candidate = builder.NumberVariable("Candidate", 2.0);
    ScriptPropertyPtr multiple = builder.NumberVariable("Multiple", 4.0);
    ScriptPropertyPtr primeCount = builder.NumberVariable("PrimeCount", 0.0);
    ScriptPropertyPtr primeSum = builder.NumberVariable("PrimeSum", 0.0);
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    builder.Add(graph, begin);
    const FreshList fresh = InitializeFreshList(builder, graph, begin, flags);

    NodePtr initializeRepeat = builder.Compiled("Flow::Repeat");
    NodePtr getSizeForInitialize = builder.Get(builder.size);
    NodePtr sizePlusOne = AddNumber(builder, 1.0);
    NodePtr minimumFlagCount = builder.Compiled("Math::Max");
    minimumFlagCount->InputValues[1] = Value(2.0);
    NodePtr getFlagsForPush = builder.Get(flags);
    NodePtr pushTrue = builder.Native("List::Push");
    pushTrue->TypeOverrides["T"] = PinType::Bool;
    pushTrue->InputValues[2] = Value(true);
    NodePtr getFlagsForZero = builder.Get(flags);
    NodePtr setZero = builder.Compiled("List::Set By Index");
    setZero->TypeOverrides["T"] = PinType::Bool;
    setZero->InputValues[2] = Value(0.0);
    setZero->InputValues[3] = Value(false);
    NodePtr getFlagsForOne = builder.Get(flags);
    NodePtr setOne = builder.Compiled("List::Set By Index");
    setOne->TypeOverrides["T"] = PinType::Bool;
    setOne->InputValues[2] = Value(1.0);
    setOne->InputValues[3] = Value(false);
    builder.Add(graph, {
        initializeRepeat, getSizeForInitialize, sizePlusOne, minimumFlagCount, getFlagsForPush, pushTrue, getFlagsForZero, setZero, getFlagsForOne, setOne
    });
    builder.Link(graph, fresh.clearList->Outputs[0], initializeRepeat->Inputs[0]);
    builder.Link(graph, getSizeForInitialize->Outputs[0], sizePlusOne->Inputs[0]);
    builder.Link(graph, sizePlusOne->Outputs[0], minimumFlagCount->Inputs[0]);
    builder.Link(graph, minimumFlagCount->Outputs[0], builder.Input(initializeRepeat, "Count"));
    builder.Link(graph, initializeRepeat->Outputs[0], pushTrue->Inputs[0]);
    builder.Link(graph, getFlagsForPush->Outputs[0], builder.Input(pushTrue, "List"));
    builder.Link(graph, initializeRepeat->Outputs[2], setZero->Inputs[0]);
    builder.Link(graph, getFlagsForZero->Outputs[0], builder.Input(setZero, "List"));
    builder.Link(graph, setZero->Outputs[0], setOne->Inputs[0]);
    builder.Link(graph, getFlagsForOne->Outputs[0], builder.Input(setOne, "List"));

    NodePtr outerWhile = builder.Compiled("Flow::While");
    NodePtr getCandidateForSquare = builder.Get(candidate);
    NodePtr candidateSquare = builder.Compiled("Math::Multiply");
    NodePtr getSizeForOuter = builder.Get(builder.size);
    NodePtr outerCondition = builder.Compiled("Math::Less Or Equal");
    NodePtr getFlagsForCandidate = builder.Get(flags);
    NodePtr getCandidateForFlag = builder.Get(candidate);
    NodePtr getCandidateFlag = builder.Compiled("List::Get By Index");
    getCandidateFlag->TypeOverrides["T"] = PinType::Bool;
    NodePtr primeBranch = builder.Compiled("Flow::Branch");
    NodePtr getCandidateForMultiple = builder.Get(candidate);
    NodePtr candidateSquareForMultiple = builder.Compiled("Math::Multiply");
    NodePtr setMultiple = builder.Set(multiple);
    builder.Add(graph, {
        outerWhile, getCandidateForSquare, candidateSquare, getSizeForOuter, outerCondition, getFlagsForCandidate, getCandidateForFlag,
        getCandidateFlag, primeBranch, getCandidateForMultiple, candidateSquareForMultiple, setMultiple
    });
    builder.Link(graph, setOne->Outputs[0], outerWhile->Inputs[0]);
    builder.Link(graph, getCandidateForSquare->Outputs[0], candidateSquare->Inputs[0]);
    builder.Link(graph, getCandidateForSquare->Outputs[0], candidateSquare->Inputs[1]);
    builder.Link(graph, candidateSquare->Outputs[0], outerCondition->Inputs[0]);
    builder.Link(graph, getSizeForOuter->Outputs[0], outerCondition->Inputs[1]);
    builder.Link(graph, outerCondition->Outputs[0], outerWhile->Inputs[1]);
    builder.Link(graph, outerWhile->Outputs[0], primeBranch->Inputs[0]);
    builder.Link(graph, getFlagsForCandidate->Outputs[0], builder.Input(getCandidateFlag, "List"));
    builder.Link(graph, getCandidateForFlag->Outputs[0], builder.Input(getCandidateFlag, "Index"));
    builder.Link(graph, builder.Output(getCandidateFlag, "Value"), primeBranch->Inputs[1]);
    builder.Link(graph, primeBranch->Outputs[0], setMultiple->Inputs[0]);
    builder.Link(graph, getCandidateForMultiple->Outputs[0], candidateSquareForMultiple->Inputs[0]);
    builder.Link(graph, getCandidateForMultiple->Outputs[0], candidateSquareForMultiple->Inputs[1]);
    builder.Link(graph, candidateSquareForMultiple->Outputs[0], setMultiple->Inputs[1]);

    NodePtr innerWhile = builder.Compiled("Flow::While");
    NodePtr getMultipleForCondition = builder.Get(multiple);
    NodePtr getSizeForInner = builder.Get(builder.size);
    NodePtr innerCondition = builder.Compiled("Math::Less Or Equal");
    NodePtr getFlagsForComposite = builder.Get(flags);
    NodePtr getMultipleForSet = builder.Get(multiple);
    NodePtr setComposite = builder.Compiled("List::Set By Index");
    setComposite->TypeOverrides["T"] = PinType::Bool;
    setComposite->InputValues[3] = Value(false);
    NodePtr getMultipleForIncrement = builder.Get(multiple);
    NodePtr getCandidateForIncrement = builder.Get(candidate);
    NodePtr incrementMultiple = builder.Compiled("Math::Add");
    NodePtr setNextMultiple = builder.Set(multiple);
    builder.Add(graph, {
        innerWhile, getMultipleForCondition, getSizeForInner, innerCondition, getFlagsForComposite, getMultipleForSet, setComposite,
        getMultipleForIncrement, getCandidateForIncrement, incrementMultiple, setNextMultiple
    });
    builder.Link(graph, setMultiple->Outputs[0], innerWhile->Inputs[0]);
    builder.Link(graph, getMultipleForCondition->Outputs[0], innerCondition->Inputs[0]);
    builder.Link(graph, getSizeForInner->Outputs[0], innerCondition->Inputs[1]);
    builder.Link(graph, innerCondition->Outputs[0], innerWhile->Inputs[1]);
    builder.Link(graph, innerWhile->Outputs[0], setComposite->Inputs[0]);
    builder.Link(graph, getFlagsForComposite->Outputs[0], builder.Input(setComposite, "List"));
    builder.Link(graph, getMultipleForSet->Outputs[0], builder.Input(setComposite, "Index"));
    builder.Link(graph, setComposite->Outputs[0], setNextMultiple->Inputs[0]);
    builder.Link(graph, getMultipleForIncrement->Outputs[0], incrementMultiple->Inputs[0]);
    builder.Link(graph, getCandidateForIncrement->Outputs[0], incrementMultiple->Inputs[1]);
    builder.Link(graph, incrementMultiple->Outputs[0], setNextMultiple->Inputs[1]);

    NodePtr getCandidateForNext = builder.Get(candidate);
    NodePtr incrementCandidate = AddNumber(builder, 1.0);
    NodePtr setCandidate = builder.Set(candidate);
    builder.Add(graph, { getCandidateForNext, incrementCandidate, setCandidate });
    builder.Link(graph, innerWhile->Outputs[1], setCandidate->Inputs[0]);
    builder.Link(graph, primeBranch->Outputs[1], setCandidate->Inputs[0]);
    builder.Link(graph, getCandidateForNext->Outputs[0], incrementCandidate->Inputs[0]);
    builder.Link(graph, incrementCandidate->Outputs[0], setCandidate->Inputs[1]);

    NodePtr countRepeat = builder.Compiled("Flow::Repeat");
    NodePtr getSizeForCount = builder.Get(builder.size);
    NodePtr countSizePlusOne = AddNumber(builder, 1.0);
    NodePtr getFlagsForCount = builder.Get(flags);
    NodePtr getCountFlag = builder.Compiled("List::Get By Index");
    getCountFlag->TypeOverrides["T"] = PinType::Bool;
    NodePtr countBranch = builder.Compiled("Flow::Branch");
    NodePtr getPrimeCount = builder.Get(primeCount);
    NodePtr incrementPrimeCount = AddNumber(builder, 1.0);
    NodePtr setPrimeCount = builder.Set(primeCount);
    NodePtr getPrimeSum = builder.Get(primeSum);
    NodePtr addPrimeSum = builder.Compiled("Math::Add");
    NodePtr setPrimeSum = builder.Set(primeSum);
    builder.Add(graph, {
        countRepeat, getSizeForCount, countSizePlusOne, getFlagsForCount, getCountFlag, countBranch, getPrimeCount, incrementPrimeCount,
        setPrimeCount, getPrimeSum, addPrimeSum, setPrimeSum
    });
    builder.Link(graph, outerWhile->Outputs[1], countRepeat->Inputs[0]);
    builder.Link(graph, getSizeForCount->Outputs[0], countSizePlusOne->Inputs[0]);
    builder.Link(graph, countSizePlusOne->Outputs[0], builder.Input(countRepeat, "Count"));
    builder.Link(graph, countRepeat->Outputs[0], countBranch->Inputs[0]);
    builder.Link(graph, getFlagsForCount->Outputs[0], builder.Input(getCountFlag, "List"));
    builder.Link(graph, countRepeat->Outputs[1], builder.Input(getCountFlag, "Index"));
    builder.Link(graph, builder.Output(getCountFlag, "Value"), countBranch->Inputs[1]);
    builder.Link(graph, countBranch->Outputs[0], setPrimeCount->Inputs[0]);
    builder.Link(graph, getPrimeCount->Outputs[0], incrementPrimeCount->Inputs[0]);
    builder.Link(graph, incrementPrimeCount->Outputs[0], setPrimeCount->Inputs[1]);
    builder.Link(graph, setPrimeCount->Outputs[0], setPrimeSum->Inputs[0]);
    builder.Link(graph, getPrimeSum->Outputs[0], addPrimeSum->Inputs[0]);
    builder.Link(graph, countRepeat->Outputs[1], addPrimeSum->Inputs[1]);
    builder.Link(graph, addPrimeSum->Outputs[0], setPrimeSum->Inputs[1]);

    NodePtr getFinalCount = builder.Get(primeCount);
    NodePtr scaleCount = MultiplyNumber(builder, Modulus);
    NodePtr getFinalSum = builder.Get(primeSum);
    NodePtr combine = builder.Compiled("Math::Add");
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { getFinalCount, scaleCount, getFinalSum, combine, setChecksum });
    builder.Link(graph, countRepeat->Outputs[2], setChecksum->Inputs[0]);
    builder.Link(graph, getFinalCount->Outputs[0], scaleCount->Inputs[0]);
    builder.Link(graph, scaleCount->Outputs[0], combine->Inputs[0]);
    builder.Link(graph, getFinalSum->Outputs[0], combine->Inputs[1]);
    builder.Link(graph, combine->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}

Script MakeMandelbrot(const NodeRegistry& registry)
{
    CaseBuilder builder(registry, 200.0);
    ScriptPropertyPtr coordinateX = builder.NumberVariable("CoordinateX", 0.0);
    ScriptPropertyPtr coordinateY = builder.NumberVariable("CoordinateY", 0.0);
    ScriptPropertyPtr valueX = builder.NumberVariable("ValueX", 0.0);
    ScriptPropertyPtr valueY = builder.NumberVariable("ValueY", 0.0);
    ScriptPropertyPtr temporaryX = builder.NumberVariable("TemporaryX", 0.0);
    ScriptPropertyPtr iterations = builder.NumberVariable("Iterations", 0.0);
    Graph& graph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr outerRepeat = builder.Compiled("Flow::Repeat");
    NodePtr getSizeOuter = builder.Get(builder.size);
    NodePtr setCoordinateY = builder.Set(coordinateY);
    NodePtr scalePy = MultiplyNumber(builder, 2.5);
    NodePtr getSizeForY = builder.Get(builder.size);
    NodePtr divideY = builder.Compiled("Math::Divide");
    NodePtr offsetY = AddNumber(builder, -1.25);
    NodePtr innerRepeat = builder.Compiled("Flow::Repeat");
    NodePtr getSizeInner = builder.Get(builder.size);
    NodePtr setCoordinateX = builder.Set(coordinateX);
    NodePtr scalePx = MultiplyNumber(builder, 3.0);
    NodePtr getSizeForX = builder.Get(builder.size);
    NodePtr divideX = builder.Compiled("Math::Divide");
    NodePtr offsetX = AddNumber(builder, -2.0);
    NodePtr setXZero = builder.Set(valueX);
    setXZero->InputValues[1] = Value(0.0);
    NodePtr setYZero = builder.Set(valueY);
    setYZero->InputValues[1] = Value(0.0);
    NodePtr setIterationsZero = builder.Set(iterations);
    setIterationsZero->InputValues[1] = Value(0.0);
    builder.Add(graph, {
        begin, outerRepeat, getSizeOuter, setCoordinateY, scalePy, getSizeForY, divideY, offsetY, innerRepeat, getSizeInner,
        setCoordinateX, scalePx, getSizeForX, divideX, offsetX, setXZero, setYZero, setIterationsZero
    });
    builder.Link(graph, begin->Outputs[0], outerRepeat->Inputs[0]);
    builder.Link(graph, getSizeOuter->Outputs[0], builder.Input(outerRepeat, "Count"));
    builder.Link(graph, outerRepeat->Outputs[0], setCoordinateY->Inputs[0]);
    builder.Link(graph, outerRepeat->Outputs[1], scalePy->Inputs[0]);
    builder.Link(graph, scalePy->Outputs[0], divideY->Inputs[0]);
    builder.Link(graph, getSizeForY->Outputs[0], divideY->Inputs[1]);
    builder.Link(graph, divideY->Outputs[0], offsetY->Inputs[0]);
    builder.Link(graph, offsetY->Outputs[0], setCoordinateY->Inputs[1]);
    builder.Link(graph, setCoordinateY->Outputs[0], innerRepeat->Inputs[0]);
    builder.Link(graph, getSizeInner->Outputs[0], builder.Input(innerRepeat, "Count"));
    builder.Link(graph, innerRepeat->Outputs[0], setCoordinateX->Inputs[0]);
    builder.Link(graph, innerRepeat->Outputs[1], scalePx->Inputs[0]);
    builder.Link(graph, scalePx->Outputs[0], divideX->Inputs[0]);
    builder.Link(graph, getSizeForX->Outputs[0], divideX->Inputs[1]);
    builder.Link(graph, divideX->Outputs[0], offsetX->Inputs[0]);
    builder.Link(graph, offsetX->Outputs[0], setCoordinateX->Inputs[1]);
    builder.Link(graph, setCoordinateX->Outputs[0], setXZero->Inputs[0]);
    builder.Link(graph, setXZero->Outputs[0], setYZero->Inputs[0]);
    builder.Link(graph, setYZero->Outputs[0], setIterationsZero->Inputs[0]);

    NodePtr whileNode = builder.Compiled("Flow::While");
    NodePtr getXForMagnitudeA = builder.Get(valueX);
    NodePtr squareX = builder.Compiled("Math::Multiply");
    NodePtr getYForMagnitudeA = builder.Get(valueY);
    NodePtr squareY = builder.Compiled("Math::Multiply");
    NodePtr magnitude = builder.Compiled("Math::Add");
    NodePtr magnitudeLimit = builder.Compiled("Math::Less Or Equal");
    magnitudeLimit->InputValues[1] = Value(4.0);
    NodePtr getIterationsForLimit = builder.Get(iterations);
    NodePtr iterationLimit = builder.Compiled("Math::Less Than");
    iterationLimit->InputValues[1] = Value(50.0);
    NodePtr condition = builder.Compiled("Logic::And");
    builder.Add(graph, {
        whileNode, getXForMagnitudeA, squareX, getYForMagnitudeA, squareY, magnitude, magnitudeLimit, getIterationsForLimit,
        iterationLimit, condition
    });
    builder.Link(graph, setIterationsZero->Outputs[0], whileNode->Inputs[0]);
    builder.Link(graph, getXForMagnitudeA->Outputs[0], squareX->Inputs[0]);
    builder.Link(graph, getXForMagnitudeA->Outputs[0], squareX->Inputs[1]);
    builder.Link(graph, getYForMagnitudeA->Outputs[0], squareY->Inputs[0]);
    builder.Link(graph, getYForMagnitudeA->Outputs[0], squareY->Inputs[1]);
    builder.Link(graph, squareX->Outputs[0], magnitude->Inputs[0]);
    builder.Link(graph, squareY->Outputs[0], magnitude->Inputs[1]);
    builder.Link(graph, magnitude->Outputs[0], magnitudeLimit->Inputs[0]);
    builder.Link(graph, getIterationsForLimit->Outputs[0], iterationLimit->Inputs[0]);
    builder.Link(graph, magnitudeLimit->Outputs[0], condition->Inputs[0]);
    builder.Link(graph, iterationLimit->Outputs[0], condition->Inputs[1]);
    builder.Link(graph, condition->Outputs[0], whileNode->Inputs[1]);

    NodePtr setTemporaryX = builder.Set(temporaryX);
    NodePtr getXForTemp = builder.Get(valueX);
    NodePtr tempSquareX = builder.Compiled("Math::Multiply");
    NodePtr getYForTemp = builder.Get(valueY);
    NodePtr tempSquareY = builder.Compiled("Math::Multiply");
    NodePtr subtractSquares = builder.Compiled("Math::Subtract");
    NodePtr getCoordinateX = builder.Get(coordinateX);
    NodePtr addCoordinateX = builder.Compiled("Math::Add");
    NodePtr setNextY = builder.Set(valueY);
    NodePtr getXForY = builder.Get(valueX);
    NodePtr getYForY = builder.Get(valueY);
    NodePtr multiplyXY = builder.Compiled("Math::Multiply");
    NodePtr doubleXY = MultiplyNumber(builder, 2.0);
    NodePtr getCoordinateY = builder.Get(coordinateY);
    NodePtr addCoordinateY = builder.Compiled("Math::Add");
    NodePtr setNextX = builder.Set(valueX);
    NodePtr getTemporaryX = builder.Get(temporaryX);
    NodePtr setNextIterations = builder.Set(iterations);
    NodePtr getIterationsForIncrement = builder.Get(iterations);
    NodePtr incrementIterations = AddNumber(builder, 1.0);
    builder.Add(graph, {
        setTemporaryX, getXForTemp, tempSquareX, getYForTemp, tempSquareY, subtractSquares, getCoordinateX, addCoordinateX, setNextY,
        getXForY, getYForY, multiplyXY, doubleXY, getCoordinateY, addCoordinateY, setNextX, getTemporaryX, setNextIterations,
        getIterationsForIncrement, incrementIterations
    });
    builder.Link(graph, whileNode->Outputs[0], setTemporaryX->Inputs[0]);
    builder.Link(graph, getXForTemp->Outputs[0], tempSquareX->Inputs[0]);
    builder.Link(graph, getXForTemp->Outputs[0], tempSquareX->Inputs[1]);
    builder.Link(graph, getYForTemp->Outputs[0], tempSquareY->Inputs[0]);
    builder.Link(graph, getYForTemp->Outputs[0], tempSquareY->Inputs[1]);
    builder.Link(graph, tempSquareX->Outputs[0], subtractSquares->Inputs[0]);
    builder.Link(graph, tempSquareY->Outputs[0], subtractSquares->Inputs[1]);
    builder.Link(graph, subtractSquares->Outputs[0], addCoordinateX->Inputs[0]);
    builder.Link(graph, getCoordinateX->Outputs[0], addCoordinateX->Inputs[1]);
    builder.Link(graph, addCoordinateX->Outputs[0], setTemporaryX->Inputs[1]);
    builder.Link(graph, setTemporaryX->Outputs[0], setNextY->Inputs[0]);
    builder.Link(graph, getXForY->Outputs[0], multiplyXY->Inputs[0]);
    builder.Link(graph, getYForY->Outputs[0], multiplyXY->Inputs[1]);
    builder.Link(graph, multiplyXY->Outputs[0], doubleXY->Inputs[0]);
    builder.Link(graph, doubleXY->Outputs[0], addCoordinateY->Inputs[0]);
    builder.Link(graph, getCoordinateY->Outputs[0], addCoordinateY->Inputs[1]);
    builder.Link(graph, addCoordinateY->Outputs[0], setNextY->Inputs[1]);
    builder.Link(graph, setNextY->Outputs[0], setNextX->Inputs[0]);
    builder.Link(graph, getTemporaryX->Outputs[0], setNextX->Inputs[1]);
    builder.Link(graph, setNextX->Outputs[0], setNextIterations->Inputs[0]);
    builder.Link(graph, getIterationsForIncrement->Outputs[0], incrementIterations->Inputs[0]);
    builder.Link(graph, incrementIterations->Outputs[0], setNextIterations->Inputs[1]);

    NodePtr getChecksum = builder.Get(builder.checksum);
    NodePtr getIterationsForChecksum = builder.Get(iterations);
    NodePtr addChecksum = builder.Compiled("Math::Add");
    NodePtr checksumModulo = ModuloNumber(builder, Uint32Modulus);
    NodePtr setChecksum = builder.Set(builder.checksum);
    builder.Add(graph, { getChecksum, getIterationsForChecksum, addChecksum, checksumModulo, setChecksum });
    builder.Link(graph, whileNode->Outputs[1], setChecksum->Inputs[0]);
    builder.Link(graph, getChecksum->Outputs[0], addChecksum->Inputs[0]);
    builder.Link(graph, getIterationsForChecksum->Outputs[0], addChecksum->Inputs[1]);
    builder.Link(graph, addChecksum->Outputs[0], checksumModulo->Inputs[0]);
    builder.Link(graph, checksumModulo->Outputs[0], setChecksum->Inputs[1]);
    return builder.Finish();
}
}

void GenerateBenchmarkCases(const NodeRegistry& registry, const std::filesystem::path& outputDirectory)
{
    IDGenerator iterativeIds;
    ValidateAndSave(MakeIterativeFibonacci(registry, iterativeIds), outputDirectory / "fibonacci-iterative.vlox");

    IDGenerator recursiveIds;
    ValidateAndSave(MakeRecursiveFibonacci(registry, recursiveIds), outputDirectory / "fibonacci-recursive.vlox");

    ValidateAndSave(MakeNumberLoop(registry), outputDirectory / "number-loop.vlox");
    ValidateAndSave(MakeFunctionCalls(registry), outputDirectory / "function-calls.vlox");
    ValidateAndSave(MakeShortScript(registry), outputDirectory / "short-script.vlox");
    ValidateAndSave(MakeConstantFolding(registry, true), outputDirectory / "constant-folding-folded.vlox");
    ValidateAndSave(MakeConstantFolding(registry, false), outputDirectory / "constant-folding-runtime.vlox");
    ValidateAndSave(MakeNativeCall(registry, true), outputDirectory / "native-call-native.vlox");
    ValidateAndSave(MakeNativeCall(registry, false), outputDirectory / "native-call-inline.vlox");
    ValidateAndSave(MakeMultipleOutputs(registry, true), outputDirectory / "multiple-outputs-multiple.vlox");
    ValidateAndSave(MakeMultipleOutputs(registry, false), outputDirectory / "multiple-outputs-inline.vlox");
    ValidateAndSave(MakeEquivalentForms(registry, false), outputDirectory / "equivalent-forms-direct.vlox");
    ValidateAndSave(MakeEquivalentForms(registry, true), outputDirectory / "equivalent-forms-temporaries.vlox");
    ValidateAndSave(MakePatternMatching(registry), outputDirectory / "pattern-matching.vlox");
    ValidateAndSave(MakeListProcessing(registry, false), outputDirectory / "list-processing-loop.vlox");
    ValidateAndSave(MakeListProcessing(registry, true), outputDirectory / "list-processing-callbacks.vlox");
    ValidateAndSave(MakeSorting(registry), outputDirectory / "sorting.vlox");
    ValidateAndSave(MakeStringBuilding(registry), outputDirectory / "string-building.vlox");
    ValidateAndSave(MakeDynamicValues(registry, false), outputDirectory / "dynamic-values-homogeneous.vlox");
    ValidateAndSave(MakeDynamicValues(registry, true), outputDirectory / "dynamic-values-mixed.vlox");
    ValidateAndSave(MakeObjects(registry), outputDirectory / "objects.vlox");
    ValidateAndSave(MakeGcPressure(registry), outputDirectory / "gc-pressure.vlox");
    ValidateAndSave(MakePrimeSieve(registry), outputDirectory / "prime-sieve.vlox");
    ValidateAndSave(MakeMandelbrot(registry), outputDirectory / "mandelbrot.vlox");
}

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path outputDirectory = argc >= 2 ? argv[1] : std::filesystem::path("benchmarks/vlox/cases");
        std::filesystem::create_directories(outputDirectory);

        VM& vm = VM::getInstance();
        NodeRegistry registry;
        RegisterStandardLibrary(registry);
        registry.RegisterNatives(vm);

        GenerateBenchmarkCases(registry, outputDirectory);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Generation error: " << error.what() << '\n';
        return 1;
    }
}
