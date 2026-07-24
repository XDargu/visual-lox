#include "runtimeTests.h"

#include "testFramework.h"
#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/object.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"
#include "../runtime/scriptRuntime.h"
#include "../runtime/standardLibrary.h"
#include "../script/script.h"
#include "../script/scriptSerializer.h"
#include "../validation/scriptValidator.h"

#include <Vm.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using Tests::Require;

void AttachNode(Graph& graph, const NodePtr& node)
{
    NodeUtils::BuildNode(node);
    graph.AddNode(node);
}

bool HasCode(const ValidationReport& report, const char* code)
{
    return std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
        [&](const ValidationDiagnostic& diagnostic) { return diagnostic.code == code; });
}

std::string MakeListLiteralSource(int itemCount)
{
    std::string source = "var boundaryList = [";
    for (int i = 0; i < itemCount; ++i)
    {
        if (i > 0)
            source += ", ";
        source += std::to_string(i);
    }
    source += "];";
    return source;
}

struct RuntimeFixture
{
    RuntimeFixture()
        : vm(VM::getInstance())
        , wasGcAllowed(vm.isGarbageCollectionAllowed())
    {
        vm.allowGarbageCollection(false);
        RegisterStandardLibrary(registry);
        registry.RegisterNatives(vm);
    }

    ~RuntimeFixture()
    {
        vm.setExternalMarkingFunc([]() {});
        vm.allowGarbageCollection(wasGcAllowed);
    }

    Value CallCollectionNode(const char* name, std::vector<Value> arguments)
    {
        const NativeFunctionDef* definition = registry.FindNative(name);
        Require(definition != nullptr, "Expected collection node to be registered.");
        Require(HasFlag(definition->functionDef->flags, NodeDefinitionFlags::ReadOnly) &&
                HasFlag(definition->functionDef->flags, NodeDefinitionFlags::Pure),
                "Collection query nodes should be read-only and pure.");
        NodePtr node = definition->functionDef->MakeNode(
            ids, ScriptElementID::Invalid);
        Require(node && node->DefinitionId == name,
                "Collection query definitions should construct callable nodes.");
        return definition->nativeFun(static_cast<int>(arguments.size()), arguments.data(), &vm);
    }

    Value CallNative(const char* name, std::vector<Value> arguments)
    {
        const NativeFunctionDef* definition = registry.FindNative(name);
        Require(definition != nullptr, "Expected native node to be registered.");
        return definition->nativeFun(
            static_cast<int>(arguments.size()), arguments.data(), &vm);
    }

    VM& vm;
    bool wasGcAllowed;
    NodeRegistry registry;
    IDGenerator ids;
};

Value StringValue(const std::string& text)
{
    return Value(copyString(text.c_str(), static_cast<int>(text.size())));
}

Value ReadGlobal(VM& vm, const char* name)
{
    Value value;
    const std::string key(name);
    Require(vm.globalTable().get(
                copyString(key.c_str(), static_cast<int>(key.size())), &value),
            "Expected a script global to exist.");
    return value;
}

ObjList* MakeList(std::initializer_list<Value> values)
{
    ObjList* list = newList();
    for (const Value& value : values)
        list->append(value);
    return list;
}

NodePtr BuildFailingExpressionNode(IDGenerator& ids, PinType outputType)
{
    struct FailingExpressionNode : Node
    {
        explicit FailingExpressionNode(int id)
            : Node(id, "Must Not Execute", ImColor(230, 230, 0))
        {
            Category = NodeCategory::Function;
        }

        void Compile(CompilerContext& context, const Graph& graph,
                     CompilationStage stage, int) const override
        {
            if (stage != CompilationStage::PullOutput)
                return;
            context.compiler.emitConstant(StringValue("not a number"));
            context.compiler.emitByte(OpByte(OpCode::OP_NEGATE));
            GraphCompiler::CompileOutput(context, graph, Outputs[0]);
        }
    };

    NodePtr node =
        std::make_shared<FailingExpressionNode>(ids.GetNextId());
    node->SerializationType = "test.failing-expression";
    node->Outputs.emplace_back(ids.GetNextId(), "Result", outputType);
    return node;
}

void StandardLibraryDeclaresCapabilities()
{
    RuntimeFixture fixture;
    const CompiledNodeDefPtr add = fixture.registry.FindCompiled("Math::Add");
    const CompiledNodeDefPtr print = fixture.registry.FindCompiled("Debug::Print");
    Require(add && HasFlag(add->functionDef->flags, NodeDefinitionFlags::Pure),
            "Math::Add should be declared pure.");
    Require(print && !HasFlag(print->functionDef->flags, NodeDefinitionFlags::Pure),
            "Debug::Print should be declared impure.");
    Require(HasFlag(fixture.registry.FindNative("Math::Square")->functionDef->flags,
                    NodeDefinitionFlags::Pure),
            "Math::Square should be declared pure.");
    Require(!HasFlag(fixture.registry.FindNative("File::FileExists")->functionDef->flags,
                     NodeDefinitionFlags::Pure),
            "File::FileExists depends on external state and must not be pure.");
    Require(!HasFlag(fixture.registry.FindNative("Functional::Map")->functionDef->flags,
                     NodeDefinitionFlags::Pure),
            "Higher-order functions cannot be pure without a pure callable contract.");
}

void ListNativeNodesOperateOnLists()
{
    RuntimeFixture fixture;
    ObjList* list = newList();
    list->append(Value(10.0));
    list->append(Value(20.0));
    list->append(Value(30.0));

    const Value length = fixture.CallCollectionNode("List::Length", { Value(list) });
    const Value inBounds = fixture.CallCollectionNode("List::In Bounds",
        { Value(list), Value(2.0) });
    const Value outOfBounds = fixture.CallCollectionNode("List::In Bounds",
        { Value(list), Value(3.0) });
    Require(isNumber(length) && asNumber(length) == 3.0,
            "List::Length should return the number of list items.");
    Require(isBoolean(inBounds) && asBoolean(inBounds) &&
            isBoolean(outOfBounds) && !asBoolean(outOfBounds),
            "List::In Bounds should distinguish valid and invalid indices.");

    const NativeFunctionDef* pop = fixture.registry.FindNative("List::Pop");
    Require(pop != nullptr, "List::Pop should be registered.");
    Value arguments[] = { Value(list) };
    const Value popped = pop->nativeFun(1, arguments, &fixture.vm);
    Require(isNumber(popped) && asNumber(popped) == 30.0 && list->items.size() == 2,
            "List::Pop should return and remove the final list item.");
}

void RangeNativeNodesSupportBothDirections()
{
    RuntimeFixture fixture;
    ObjRange* ascending = newRange(2.0, 4.0);
    ObjRange* descending = newRange(4.0, 2.0);
    const Value ascendingLength =
        fixture.CallCollectionNode("Range::Length", { Value(ascending) });
    const Value descendingLength =
        fixture.CallCollectionNode("Range::Length", { Value(descending) });
    const Value inBounds = fixture.CallCollectionNode("Range::In Bounds",
        { Value(ascending), Value(2.0) });
    const Value outOfBounds = fixture.CallCollectionNode("Range::In Bounds",
        { Value(ascending), Value(3.0) });
    const Value contains = fixture.CallCollectionNode("Range::Contains",
        { Value(descending), Value(3.0) });
    const Value doesNotContain = fixture.CallCollectionNode("Range::Contains",
        { Value(descending), Value(5.0) });
    const Value index = fixture.CallCollectionNode("Range::IndexOf",
        { Value(descending), Value(2.0) });

    Require(isNumber(ascendingLength) && asNumber(ascendingLength) == 3.0 &&
            isNumber(descendingLength) && asNumber(descendingLength) == 3.0,
            "Range::Length should support ascending and descending inclusive ranges.");
    Require(isBoolean(inBounds) && asBoolean(inBounds) &&
            isBoolean(outOfBounds) && !asBoolean(outOfBounds),
            "Range::In Bounds should distinguish valid and invalid indices.");
    Require(isBoolean(contains) && asBoolean(contains) &&
            isBoolean(doesNotContain) && !asBoolean(doesNotContain),
            "Range::Contains should find values in ascending or descending ranges.");
    Require(isNumber(index) && asNumber(index) == 2.0,
            "Range::IndexOf should return the zero-based range index.");
}

void RepeatedInterpretationReleasesStack()
{
    RuntimeFixture fixture;
    for (int iteration = 0; iteration < 300; ++iteration)
    {
        Require(fixture.vm.interpret("for value in [1, 2, 3] {}") ==
                    InterpretResult::INTERPRET_OK,
                "Repeated textual for-in execution should succeed.");
        Require(fixture.vm.getStackSize() == 0,
                "VM::interpret should leave the stack empty after successful execution.");
    }
}

void LargeListLiteralsPreserveItems()
{
    RuntimeFixture fixture;
    Require(fixture.vm.interpret(MakeListLiteralSource(1000)) ==
                InterpretResult::INTERPRET_OK,
            "A 1000-item list literal should execute successfully.");
    Require(fixture.vm.getStackSize() == 0,
            "List literal construction should leave the stack empty after execution.");
    Value listValue;
    Require(fixture.vm.globalTable().get(copyString("boundaryList", 12), &listValue) &&
            isList(listValue) && asList(listValue)->items.size() == 1000 &&
            isNumber(asList(listValue)->getValue(0)) &&
            isNumber(asList(listValue)->getValue(999)) &&
            asNumber(asList(listValue)->getValue(0)) == 0.0 &&
            asNumber(asList(listValue)->getValue(999)) == 999.0,
            "Large list literals should preserve every item in source order.");
}

void InstanceErrorsDoNotChangeDefinitionCapabilities()
{
    RuntimeFixture fixture;
    NodePtr node = fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    node->InstanceFlags |= NodeInstanceFlags::Error;
    Require(node->IsPure() && HasFlag(node->InstanceFlags, NodeInstanceFlags::Error),
            "Instance errors must not change immutable definition capabilities.");
}

void MissingBeginIsRejected()
{
    IDGenerator ids;
    Script script;
    script.ID = ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
    const ValidationReport report = ScriptValidator::Validate(script);
    Require(report.HasErrors() && HasCode(report, "begin-count"),
            "Validation should reject a graph without Begin.");
}

void DependencyCyclesAreRejected()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    AttachNode(script.main->Graph, BuildBeginNode(fixture.ids, script.main));
    const CompiledNodeDefPtr add = fixture.registry.FindCompiled("Math::Add");
    NodePtr first = add->MakeNode(fixture.ids);
    NodePtr second = add->MakeNode(fixture.ids);
    AttachNode(script.main->Graph, first);
    AttachNode(script.main->Graph, second);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        first->Outputs[0].ID, second->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        second->Outputs[0].ID, first->Inputs[0].ID));

    const ValidationReport report = ScriptValidator::Validate(script);
    Require(report.HasErrors() && HasCode(report, "graph-cycle"),
            "Validation should reject dependency cycles.");
}

void PureNodesAreConstantFolded()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr add = fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    add->InputValues[0] = Value(2.0);
    add->InputValues[1] = Value(3.0);
    NodePtr print = fixture.registry.FindCompiled("Debug::Print")->MakeNode(fixture.ids);
    AttachNode(script.main->Graph, begin);
    AttachNode(script.main->Graph, add);
    AttachNode(script.main->Graph, print);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, print->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        add->Outputs[0].ID, print->Inputs[1].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled), "A valid script should compile.");
    const auto folded = std::find(compiled.foldedNodeIds.begin(),
        compiled.foldedNodeIds.end(), add->ID);
    Require(folded != compiled.foldedNodeIds.end(),
            "A reachable pure Add node should be folded.");
    const size_t index = static_cast<size_t>(
        std::distance(compiled.foldedNodeIds.begin(), folded));
    Require(isNumber(compiled.foldedValues[index]) &&
            asNumber(compiled.foldedValues[index]) == 5.0,
            "Constant folding should preserve the Add result.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "The folded script should execute successfully.");
}

void ForInKeepsConstantStackFootprint()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "ForInMain");
    ScriptPropertyPtr result =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "ForInResult");
    result->defaultValue = Value(-1.0);
    script.variables.push_back(result);
    ScriptPropertyPtr length =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "CollectionLength");
    length->defaultValue = Value(-1.0);
    script.variables.push_back(length);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr forIn = fixture.registry.FindCompiled("Flow::For In")->MakeNode(fixture.ids);
    NodePtr storeResult = BuildSetVariableNode(fixture.ids, result);
    NodePtr listLength = fixture.registry.FindNative("List::Length")->functionDef->MakeNode(
        fixture.ids, ScriptElementID::Invalid);
    NodePtr storeLength = BuildSetVariableNode(fixture.ids, length);
    ObjList* list = newList();
    for (int value = 0; value < 1000; ++value)
        list->append(Value(static_cast<double>(value)));
    forIn->InputValues[1] = Value(list);
    listLength->InputValues[0] = Value(list);

    AttachNode(script.main->Graph, begin);
    AttachNode(script.main->Graph, forIn);
    AttachNode(script.main->Graph, storeResult);
    AttachNode(script.main->Graph, listLength);
    AttachNode(script.main->Graph, storeLength);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, forIn->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        forIn->Outputs[0].ID, storeResult->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        forIn->Outputs[1].ID, storeResult->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        forIn->Outputs[2].ID, storeLength->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        listLength->Outputs[0].ID, storeLength->Inputs[1].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled), "A large Flow::For In script should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "Flow::For In should iterate over more than 256 list items.");
    Require(fixture.vm.getStackSize() == 0,
            "Flow::For In should release all loop values after execution.");
    Value observedResult;
    Value observedLength;
    Require(fixture.vm.globalTable().get(copyString("ForInResult", 11), &observedResult) &&
            isNumber(observedResult) && asNumber(observedResult) == 999.0,
            "Flow::For In should expose every list value through its Value output.");
    Require(fixture.vm.globalTable().get(copyString("CollectionLength", 16), &observedLength) &&
            isNumber(observedLength) && asNumber(observedLength) == 1000.0,
            "List::Length should execute through the graph compiler.");
}

void MainReceivesProgramArgumentsAsAStringList()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(
        fixture.ids.GetNextId(), "Main");
    script.main->functionDef->inputs.push_back(
        { "Arguments", Value(newList()), fixture.ids.GetNextId() });

    ScriptPropertyPtr observed = std::make_shared<ScriptProperty>(
        fixture.ids.GetNextId(), "ArgumentCount");
    observed->defaultValue = Value(-1.0);
    script.variables.push_back(observed);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr listLength =
        fixture.registry.FindNative("List::Length")->functionDef->MakeNode(
            fixture.ids, ScriptElementID::Invalid);
    NodePtr storeCount = BuildSetVariableNode(fixture.ids, observed);
    AttachNode(script.main->Graph, begin);
    AttachNode(script.main->Graph, listLength);
    AttachNode(script.main->Graph, storeCount);
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), begin->Outputs[0].ID, storeCount->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), begin->Outputs[1].ID, listLength->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), listLength->Outputs[0].ID, storeCount->Inputs[1].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    ScriptCompileOptions options;
    options.programArguments = { "first", "second", "third" };
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script, options);
    Require(static_cast<bool>(compiled),
            "A Main graph using Arguments should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "A Main graph using Arguments should execute.");

    Value count;
    Require(fixture.vm.globalTable().get(
                copyString("ArgumentCount", 13), &count) &&
            isNumber(count) && asNumber(count) == 3.0,
            "Main did not receive program arguments as a three-item string list.");
}

void FunctionsAndMethodsSupportMultipleOutputs()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");

    const auto addVariable = [&](const char* name, const Value& defaultValue)
    {
        ScriptPropertyPtr variable =
            std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), name);
        variable->defaultValue = defaultValue;
        script.variables.push_back(variable);
        return variable;
    };

    ScriptPropertyPtr multiNumber = addVariable("MultiNumber", Value(0.0));
    ScriptPropertyPtr multiText =
        addVariable("MultiText", Value(takeString("", 0)));
    ScriptPropertyPtr multiReady = addVariable("MultiReady", Value(false));
    ScriptPropertyPtr methodNumber = addVariable("MethodNumber", Value(0.0));
    ScriptPropertyPtr methodText =
        addVariable("MethodText", Value(takeString("", 0)));

    ScriptFunctionPtr noOutputs =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "NoOutputs");
    NodePtr noOutputsBegin = BuildBeginNode(fixture.ids, noOutputs);
    NodePtr noOutputsReturn = BuildReturnNode(fixture.ids, *noOutputs);
    AttachNode(noOutputs->Graph, noOutputsBegin);
    AttachNode(noOutputs->Graph, noOutputsReturn);
    noOutputs->Graph.AddLink(Link(fixture.ids.GetNextId(),
        noOutputsBegin->Outputs[0].ID, noOutputsReturn->Inputs[0].ID));
    script.functions.push_back(noOutputs);

    ScriptFunctionPtr multiple =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Multiple");
    multiple->functionDef->outputs.push_back(
        { "Number", Value(0.0), fixture.ids.GetNextId() });
    multiple->functionDef->outputs.push_back(
        { "Text", Value(takeString("", 0)), fixture.ids.GetNextId() });
    multiple->functionDef->outputs.push_back(
        { "Ready", Value(false), fixture.ids.GetNextId() });
    NodePtr multipleBegin = BuildBeginNode(fixture.ids, multiple);
    NodePtr multipleReturn = BuildReturnNode(fixture.ids, *multiple);
    multipleReturn->InputValues[1] = Value(42.0);
    multipleReturn->InputValues[2] = Value(takeString("packed", 6));
    multipleReturn->InputValues[3] = Value(true);
    AttachNode(multiple->Graph, multipleBegin);
    AttachNode(multiple->Graph, multipleReturn);
    multiple->Graph.AddLink(Link(fixture.ids.GetNextId(),
        multipleBegin->Outputs[0].ID, multipleReturn->Inputs[0].ID));
    script.functions.push_back(multiple);

    ScriptClassPtr source =
        std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "OutputSource");
    ScriptFunctionPtr method =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "read");
    method->functionDef->outputs.push_back(
        { "Number", Value(0.0), fixture.ids.GetNextId() });
    method->functionDef->outputs.push_back(
        { "Text", Value(takeString("", 0)), fixture.ids.GetNextId() });
    NodePtr methodBegin = BuildBeginNode(fixture.ids, method);
    NodePtr methodReturn = BuildReturnNode(fixture.ids, *method);
    methodReturn->InputValues[1] = Value(7.0);
    methodReturn->InputValues[2] = Value(takeString("method", 6));
    AttachNode(method->Graph, methodBegin);
    AttachNode(method->Graph, methodReturn);
    method->Graph.AddLink(Link(fixture.ids.GetNextId(),
        methodBegin->Outputs[0].ID, methodReturn->Inputs[0].ID));
    source->methods.push_back(method);
    script.classes.push_back(source);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr callNoOutputs =
        noOutputs->functionDef->MakeNode(fixture.ids, noOutputs->ID);
    NodePtr callMultiple =
        multiple->functionDef->MakeNode(fixture.ids, multiple->ID);
    NodePtr setMultiNumber = BuildSetVariableNode(fixture.ids, multiNumber);
    NodePtr setMultiText = BuildSetVariableNode(fixture.ids, multiText);
    NodePtr setMultiReady = BuildSetVariableNode(fixture.ids, multiReady);
    NodePtr construct = BuildConstructObjectNode(fixture.ids, source);
    NodePtr callMethod = BuildMethodCallNode(fixture.ids, method);
    NodePtr setMethodNumber = BuildSetVariableNode(fixture.ids, methodNumber);
    NodePtr setMethodText = BuildSetVariableNode(fixture.ids, methodText);
    for (const NodePtr& node : { begin, callNoOutputs, callMultiple,
             setMultiNumber, setMultiText, setMultiReady, construct, callMethod,
             setMethodNumber, setMethodText })
        AttachNode(script.main->Graph, node);

    const auto connectFlow = [&](const NodePtr& from, const NodePtr& to)
    {
        script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
            from->Outputs[0].ID, to->Inputs[0].ID));
    };
    connectFlow(begin, callNoOutputs);
    connectFlow(callNoOutputs, callMultiple);
    connectFlow(callMultiple, setMultiNumber);
    connectFlow(setMultiNumber, setMultiText);
    connectFlow(setMultiText, setMultiReady);
    connectFlow(setMultiReady, construct);
    connectFlow(construct, callMethod);
    connectFlow(callMethod, setMethodNumber);
    connectFlow(setMethodNumber, setMethodText);

    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMultiple->Outputs[1].ID, setMultiNumber->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMultiple->Outputs[2].ID, setMultiText->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMultiple->Outputs[3].ID, setMultiReady->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        construct->Outputs[1].ID, callMethod->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMethod->Outputs[1].ID, setMethodNumber->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMethod->Outputs[2].ID, setMethodText->Inputs[1].ID));

    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled),
            "Functions with zero or multiple outputs should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "Functions and methods with multiple outputs should execute.");

    const auto readGlobal = [&](const char* name)
    {
        Value value;
        const std::string key(name);
        Require(fixture.vm.globalTable().get(
                    copyString(key.c_str(), static_cast<int>(key.size())), &value),
                "Expected a multi-output result variable.");
        return value;
    };
    const Value observedMultiNumber = readGlobal("MultiNumber");
    const Value observedMultiText = readGlobal("MultiText");
    const Value observedMultiReady = readGlobal("MultiReady");
    const Value observedMethodNumber = readGlobal("MethodNumber");
    const Value observedMethodText = readGlobal("MethodText");
    Require(isNumber(observedMultiNumber) && asNumber(observedMultiNumber) == 42.0,
            "The first function output was not unpacked correctly.");
    Require(isString(observedMultiText) &&
            asString(observedMultiText)->chars == "packed",
            "The second function output was not unpacked correctly.");
    Require(isBoolean(observedMultiReady) && asBoolean(observedMultiReady),
            "The third function output was not unpacked correctly.");
    Require(isNumber(observedMethodNumber) && asNumber(observedMethodNumber) == 7.0,
            "The first method output was not unpacked correctly.");
    Require(isString(observedMethodText) &&
            asString(observedMethodText)->chars == "method",
            "The second method output was not unpacked correctly.");
}

Script BuildClassRangeMatchScript(IDGenerator& ids, NodeRegistry& registry)
{
    Script script;
    script.ID = ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "FeatureMain");
    ScriptPropertyPtr classResult =
        std::make_shared<ScriptProperty>(ids.GetNextId(), "ClassFeatureResult");
    classResult->defaultValue = Value(0.0);
    script.variables.push_back(classResult);
    ScriptPropertyPtr matchResult =
        std::make_shared<ScriptProperty>(ids.GetNextId(), "MatchFeatureResult");
    matchResult->defaultValue = Value(false);
    script.variables.push_back(matchResult);
    ScriptPropertyPtr flowMatchResult =
        std::make_shared<ScriptProperty>(ids.GetNextId(), "FlowMatchFeatureResult");
    flowMatchResult->defaultValue = Value(0.0);
    script.variables.push_back(flowMatchResult);

    ScriptClassPtr counter = std::make_shared<ScriptClass>(ids.GetNextId(), "Counter");
    ScriptPropertyPtr counterValue = std::make_shared<ScriptProperty>(ids.GetNextId(), "value");
    counterValue->defaultValue = Value(1.0);
    counter->properties.push_back(counterValue);

    counter->constructor = std::make_shared<ScriptFunction>(ids.GetNextId(), "init");
    counter->constructor->functionDef->inputs.push_back(
        { "start", Value(0.0), ids.GetNextId() });
    NodePtr constructorBegin = BuildBeginNode(ids, counter->constructor);
    NodePtr constructorThis = BuildThisNode(ids);
    NodePtr constructorSet = BuildSetPropertyNode(ids, counterValue);
    AttachNode(counter->constructor->Graph, constructorBegin);
    AttachNode(counter->constructor->Graph, constructorThis);
    AttachNode(counter->constructor->Graph, constructorSet);
    counter->constructor->Graph.AddLink(Link(ids.GetNextId(),
        constructorBegin->Outputs[0].ID, constructorSet->Inputs[0].ID));
    counter->constructor->Graph.AddLink(Link(ids.GetNextId(),
        constructorThis->Outputs[0].ID, constructorSet->Inputs[1].ID));
    counter->constructor->Graph.AddLink(Link(ids.GetNextId(),
        constructorBegin->Outputs[1].ID, constructorSet->Inputs[2].ID));

    ScriptFunctionPtr getValue = std::make_shared<ScriptFunction>(ids.GetNextId(), "getValue");
    getValue->functionDef->outputs.push_back({ "Value", Value(0.0), ids.GetNextId() });
    NodePtr methodBegin = BuildBeginNode(ids, getValue);
    NodePtr methodThis = BuildThisNode(ids);
    NodePtr propertyGet = BuildGetPropertyNode(ids, counterValue);
    NodePtr methodReturn = BuildReturnNode(ids, *getValue);
    AttachNode(getValue->Graph, methodBegin);
    AttachNode(getValue->Graph, methodThis);
    AttachNode(getValue->Graph, propertyGet);
    AttachNode(getValue->Graph, methodReturn);
    getValue->Graph.AddLink(Link(ids.GetNextId(),
        methodBegin->Outputs[0].ID, methodReturn->Inputs[0].ID));
    getValue->Graph.AddLink(Link(ids.GetNextId(),
        methodThis->Outputs[0].ID, propertyGet->Inputs[0].ID));
    getValue->Graph.AddLink(Link(ids.GetNextId(),
        propertyGet->Outputs[0].ID, methodReturn->Inputs[1].ID));
    counter->methods.push_back(getValue);
    script.classes.push_back(counter);

    NodePtr begin = BuildBeginNode(ids, script.main);
    NodePtr construct = BuildConstructObjectNode(ids, counter);
    construct->InputValues[1] = Value(7.0);
    NodePtr callGetValue = BuildMethodCallNode(ids, getValue);
    NodePtr storeClassResult = BuildSetVariableNode(ids, classResult);
    NodePtr storeMatchResult = BuildSetVariableNode(ids, matchResult);
    storeMatchResult->InputValues[1] = Value(true);
    NodePtr flowMatch = registry.FindCompiled("Flow::Match")->MakeNode(ids);
    flowMatch->AddInput(ids);
    flowMatch->InputValues[1] = Value(3.0);
    flowMatch->InputValues[2] = Value(1.0);
    NodePtr flowRange = registry.FindCompiled("Range::Make")->MakeNode(ids);
    flowRange->InputValues[0] = Value(2.0);
    flowRange->InputValues[1] = Value(4.0);
    NodePtr firstCase = BuildSetVariableNode(ids, flowMatchResult);
    firstCase->InputValues[1] = Value(10.0);
    NodePtr secondCase = BuildSetVariableNode(ids, flowMatchResult);
    secondCase->InputValues[1] = Value(20.0);
    NodePtr defaultCase = BuildSetVariableNode(ids, flowMatchResult);
    defaultCase->InputValues[1] = Value(99.0);
    for (const NodePtr& node : { begin, construct, callGetValue, storeClassResult,
             storeMatchResult, flowMatch, flowRange, firstCase, secondCase, defaultCase })
        AttachNode(script.main->Graph, node);

    script.main->Graph.AddLink(Link(ids.GetNextId(),
        begin->Outputs[0].ID, construct->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        construct->Outputs[0].ID, callGetValue->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        construct->Outputs[1].ID, callGetValue->Inputs[1].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        callGetValue->Outputs[0].ID, storeClassResult->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        callGetValue->Outputs[1].ID, storeClassResult->Inputs[1].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        storeClassResult->Outputs[0].ID, flowMatch->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        flowRange->Outputs[0].ID, flowMatch->Inputs[3].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        flowMatch->Outputs[0].ID, firstCase->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        flowMatch->Outputs[1].ID, storeMatchResult->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        storeMatchResult->Outputs[0].ID, secondCase->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        flowMatch->Outputs[2].ID, defaultCase->Inputs[0].ID));
    return script;
}

void ClassesRangesAndMatchingRoundTripAndExecute()
{
    RuntimeFixture fixture;
    Script source = BuildClassRangeMatchScript(fixture.ids, fixture.registry);
    std::string document;
    Require(static_cast<bool>(ScriptSerializer::SerializeToString(source, document)),
            "Classes and object nodes should serialize.");
    Script restored;
    IDGenerator restoredIds;
    Require(static_cast<bool>(ScriptSerializer::DeserializeFromString(
                document, fixture.registry, restored, restoredIds)),
            "Classes and object nodes should deserialize.");
    Require(restored.classes.size() == 1 && restored.classes[0]->constructor &&
            restored.classes[0]->methods.size() == 1 &&
            restored.classes[0]->properties.size() == 1,
            "Class structure should survive a serialization round trip.");

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(restored);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, restored);
    Require(static_cast<bool>(compiled),
            "The complete class/range/match script should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "The complete class/range/match script should execute.");
    Value classResult;
    Value matchResult;
    Value flowMatchResult;
    Require(fixture.vm.globalTable().get(copyString("ClassFeatureResult", 18), &classResult) &&
            isNumber(classResult) && asNumber(classResult) == 7.0,
            "Constructor property assignment and method property access should produce 7.");
    Require(fixture.vm.globalTable().get(copyString("MatchFeatureResult", 18), &matchResult) &&
            isBoolean(matchResult) && asBoolean(matchResult),
            "Matching 3 against range 2..4 should produce true.");
    Require(fixture.vm.globalTable().get(
                copyString("FlowMatchFeatureResult", 22), &flowMatchResult) &&
            isNumber(flowMatchResult) && asNumber(flowMatchResult) == 20.0,
            "Flow Match should execute the first matching case and skip Default.");
}

void CompleteExpressionNodesCompileAndExecute()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "ExpressionMain");
    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    AttachNode(script.main->Graph, begin);
    ed::PinId previousFlow = begin->Outputs[0].ID;

    const auto addExpression =
        [&](const char* definitionName, const char* variableName,
            const Value& first, const Value& second, bool hasSecond,
            const Value& resultDefault)
    {
        const CompiledNodeDefPtr definition =
            fixture.registry.FindCompiled(definitionName);
        Require(definition != nullptr, "Expected expression node to be registered.");
        NodePtr expression = definition->MakeNode(fixture.ids);
        expression->InputValues[0] = first;
        if (hasSecond)
            expression->InputValues[1] = second;

        ScriptPropertyPtr result = std::make_shared<ScriptProperty>(
            fixture.ids.GetNextId(), variableName);
        result->defaultValue = resultDefault;
        script.variables.push_back(result);
        NodePtr setter = BuildSetVariableNode(fixture.ids, result);
        AttachNode(script.main->Graph, expression);
        AttachNode(script.main->Graph, setter);
        script.main->Graph.AddLink(Link(
            fixture.ids.GetNextId(), previousFlow, setter->Inputs[0].ID));
        script.main->Graph.AddLink(Link(
            fixture.ids.GetNextId(), expression->Outputs[0].ID,
            setter->Inputs[1].ID));
        previousFlow = setter->Outputs[0].ID;
        return expression;
    };

    addExpression("Logic::Not", "ExprNot", Value(true), Value(), false,
                  Value(false));
    addExpression("Math::Negate", "ExprNegate", Value(5.0), Value(), false,
                  Value(0.0));
    addExpression("Math::Not Equals", "ExprNotEquals",
                  StringValue("left"), StringValue("right"), true, Value(false));
    addExpression("Math::Greater Or Equal", "ExprGreaterEqual",
                  Value(4.0), Value(4.0), true, Value(false));
    addExpression("Math::Less Or Equal", "ExprLessEqual",
                  Value(3.0), Value(4.0), true, Value(false));
    addExpression("Math::Equals", "ExprAnyEquals",
                  StringValue("same"), StringValue("same"), true, Value(false));
    addExpression("Value::Is Nil", "ExprIsNil", Value(), Value(), false,
                  Value(false));
    addExpression("Logic::And", "ExprAnd", Value(true), Value(true), true,
                  Value(false));
    NodePtr shortAnd = addExpression(
        "Logic::And", "ExprAndShort", Value(false), Value(true), true, Value(true));
    addExpression("Logic::Or", "ExprOr", Value(false), Value(true), true,
                  Value(false));
    NodePtr shortOr = addExpression(
        "Logic::Or", "ExprOrShort", Value(true), Value(false), true, Value(false));
    addExpression("Value::Coalesce", "ExprCoalesce",
                  Value(), StringValue("fallback"), true, StringValue(""));
    NodePtr shortCoalesce = addExpression(
        "Value::Coalesce", "ExprCoalesceKeep", StringValue("left"),
        StringValue("right"), true, StringValue(""));

    for (const NodePtr& expression : { shortAnd, shortOr, shortCoalesce })
    {
        NodePtr failing = BuildFailingExpressionNode(
            fixture.ids, expression->Inputs[1].Type);
        AttachNode(script.main->Graph, failing);
        script.main->Graph.AddLink(Link(
            fixture.ids.GetNextId(), failing->Outputs[0].ID,
            expression->Inputs[1].ID));
    }

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    ScriptCompileOptions options;
    options.enableConstantFolding = false;
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script, options);
    Require(static_cast<bool>(compiled),
            "The complete expression graph should compile without folding.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "The complete expression graph should execute.");

    Require(isBoolean(ReadGlobal(fixture.vm, "ExprNot")) &&
            !asBoolean(ReadGlobal(fixture.vm, "ExprNot")),
            "Logic::Not should invert its input.");
    Require(asNumber(ReadGlobal(fixture.vm, "ExprNegate")) == -5.0,
            "Math::Negate should negate its input.");
    for (const char* name : { "ExprNotEquals", "ExprGreaterEqual",
             "ExprLessEqual", "ExprAnyEquals", "ExprIsNil", "ExprAnd",
             "ExprOr", "ExprOrShort" })
        Require(isBoolean(ReadGlobal(fixture.vm, name)) &&
                asBoolean(ReadGlobal(fixture.vm, name)),
                "Expected expression result to be true.");
    Require(isBoolean(ReadGlobal(fixture.vm, "ExprAndShort")) &&
            !asBoolean(ReadGlobal(fixture.vm, "ExprAndShort")),
            "Logic::And should preserve a false left operand.");
    Require(isString(ReadGlobal(fixture.vm, "ExprCoalesce")) &&
            asString(ReadGlobal(fixture.vm, "ExprCoalesce"))->chars == "fallback",
            "Value::Coalesce should use its fallback for nil.");
    Require(isString(ReadGlobal(fixture.vm, "ExprCoalesceKeep")) &&
            asString(ReadGlobal(fixture.vm, "ExprCoalesceKeep"))->chars == "left",
            "Value::Coalesce should preserve a non-nil left operand.");
}

void WhileAndRepeatNodesCompileAndExecute()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "LoopMain");

    const auto addVariable = [&](const char* name, const Value& initial)
    {
        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(
            fixture.ids.GetNextId(), name);
        property->defaultValue = initial;
        script.variables.push_back(property);
        return property;
    };
    ScriptPropertyPtr counter = addVariable("LoopCounter", Value(0.0));
    ScriptPropertyPtr whileDone = addVariable("WhileCompleted", Value(false));
    ScriptPropertyPtr repeatSum = addVariable("RepeatSum", Value(0.0));
    ScriptPropertyPtr repeatDone = addVariable("RepeatCompleted", Value(false));

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr whileNode =
        fixture.registry.FindCompiled("Flow::While")->MakeNode(fixture.ids);
    NodePtr condition =
        fixture.registry.FindCompiled("Math::Less Than")->MakeNode(fixture.ids);
    NodePtr conditionCounter = BuildGetVariableNode(fixture.ids, counter);
    condition->InputValues[1] = Value(3.0);
    NodePtr increment =
        fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    NodePtr bodyCounter = BuildGetVariableNode(fixture.ids, counter);
    increment->InputValues[1] = Value(1.0);
    NodePtr setCounter = BuildSetVariableNode(fixture.ids, counter);
    NodePtr setWhileDone = BuildSetVariableNode(fixture.ids, whileDone);
    setWhileDone->InputValues[1] = Value(true);

    NodePtr repeat =
        fixture.registry.FindCompiled("Flow::Repeat")->MakeNode(fixture.ids);
    repeat->InputValues[1] = Value(3.0);
    NodePtr sum =
        fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    NodePtr currentSum = BuildGetVariableNode(fixture.ids, repeatSum);
    NodePtr setSum = BuildSetVariableNode(fixture.ids, repeatSum);
    NodePtr setRepeatDone = BuildSetVariableNode(fixture.ids, repeatDone);
    setRepeatDone->InputValues[1] = Value(true);

    for (const NodePtr& node : { begin, whileNode, condition, conditionCounter,
             increment, bodyCounter, setCounter, setWhileDone, repeat, sum,
             currentSum, setSum, setRepeatDone })
        AttachNode(script.main->Graph, node);

    const auto link = [&](const Pin& output, const Pin& input)
    {
        script.main->Graph.AddLink(
            Link(fixture.ids.GetNextId(), output.ID, input.ID));
    };
    link(begin->Outputs[0], whileNode->Inputs[0]);
    link(conditionCounter->Outputs[0], condition->Inputs[0]);
    link(condition->Outputs[0], whileNode->Inputs[1]);
    link(whileNode->Outputs[0], setCounter->Inputs[0]);
    link(bodyCounter->Outputs[0], increment->Inputs[0]);
    link(increment->Outputs[0], setCounter->Inputs[1]);
    link(whileNode->Outputs[1], setWhileDone->Inputs[0]);
    link(setWhileDone->Outputs[0], repeat->Inputs[0]);
    link(repeat->Outputs[0], setSum->Inputs[0]);
    link(currentSum->Outputs[0], sum->Inputs[0]);
    link(repeat->Outputs[1], sum->Inputs[1]);
    link(sum->Outputs[0], setSum->Inputs[1]);
    link(repeat->Outputs[2], setRepeatDone->Inputs[0]);

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    ScriptCompileOptions options;
    options.enableConstantFolding = false;
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script, options);
    Require(static_cast<bool>(compiled),
            "While and Repeat graphs should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "While and Repeat graphs should execute.");
    Require(isNumber(ReadGlobal(fixture.vm, "LoopCounter")) &&
            asNumber(ReadGlobal(fixture.vm, "LoopCounter")) == 3.0,
            "While should re-evaluate its condition and execute three times.");
    Require(isBoolean(ReadGlobal(fixture.vm, "WhileCompleted")) &&
            asBoolean(ReadGlobal(fixture.vm, "WhileCompleted")),
            "While should continue through Completed.");
    Require(isNumber(ReadGlobal(fixture.vm, "RepeatSum")) &&
            asNumber(ReadGlobal(fixture.vm, "RepeatSum")) == 3.0,
            "Repeat should expose indices 0, 1, and 2.");
    Require(isBoolean(ReadGlobal(fixture.vm, "RepeatCompleted")) &&
            asBoolean(ReadGlobal(fixture.vm, "RepeatCompleted")),
            "Repeat should continue through Completed.");
}

void ExpandedMathAndStringNodesOperate()
{
    RuntimeFixture fixture;
    const auto number = [&](const char* name, std::vector<Value> arguments)
    {
        const Value result = fixture.CallNative(name, std::move(arguments));
        Require(isNumber(result), "Expected a numeric native result.");
        return asNumber(result);
    };
    Require(number("Math::Abs", { Value(-4.0) }) == 4.0,
            "Math::Abs should return magnitude.");
    Require(number("Math::Min", { Value(2.0), Value(5.0) }) == 2.0 &&
            number("Math::Max", { Value(2.0), Value(5.0) }) == 5.0,
            "Math::Min and Math::Max should select endpoints.");
    Require(number("Math::Clamp",
                { Value(7.0), Value(1.0), Value(5.0) }) == 5.0,
            "Math::Clamp should constrain values.");
    Require(number("Math::Power", { Value(2.0), Value(3.0) }) == 8.0 &&
            number("Math::Sqrt", { Value(9.0) }) == 3.0,
            "Power and square root should calculate expected values.");
    Require(number("Math::Floor", { Value(2.8) }) == 2.0 &&
            number("Math::Ceil", { Value(2.2) }) == 3.0 &&
            number("Math::Round", { Value(2.6) }) == 3.0,
            "Rounding nodes should use their documented direction.");
    const double random =
        number("Math::Random", { Value(-2.0), Value(2.0) });
    Require(random >= -2.0 && random <= 2.0,
            "Math::Random should stay within its requested bounds.");

    const auto string = [&](const char* name, std::vector<Value> arguments)
    {
        const Value result = fixture.CallNative(name, std::move(arguments));
        Require(isString(result), "Expected a string native result.");
        return asString(result)->chars;
    };
    Require(string("String::Trim", { StringValue("  hello \n") }) == "hello",
            "String::Trim should remove surrounding whitespace.");
    Require(string("String::Replace",
                { StringValue("a-b-a"), StringValue("a"), StringValue("x") }) ==
                "x-b-x",
            "String::Replace should replace every match.");
    Require(string("String::Join",
                { Value(MakeList({ StringValue("a"), StringValue("b") })),
                  StringValue(",") }) == "a,b",
            "String::Join should join list items.");
    Require(asBoolean(fixture.CallNative("String::Starts With",
                { StringValue("visual-lox"), StringValue("visual") })) &&
            asBoolean(fixture.CallNative("String::Ends With",
                { StringValue("visual-lox"), StringValue("lox") })),
            "Prefix and suffix nodes should match boundaries.");
    Require(string("String::Format",
                { StringValue("{0}:{1}"),
                  Value(MakeList({ StringValue("value"), Value(3.0) })) }) ==
                "value:3.000000",
            "String::Format should replace indexed placeholders.");

    const Value parsedNumber = fixture.CallNative(
        "String::Parse Number", { StringValue(" 12.5 ") });
    const Value parsedBool = fixture.CallNative(
        "String::Parse Bool", { StringValue("TRUE") });
    Require(isList(parsedNumber) && asList(parsedNumber)->items.size() == 2 &&
            asNumber(asList(parsedNumber)->items[0]) == 12.5 &&
            asBoolean(asList(parsedNumber)->items[1]),
            "String::Parse Number should package the value and success flag.");
    Require(isList(parsedBool) && asList(parsedBool)->items.size() == 2 &&
            asBoolean(asList(parsedBool)->items[0]) &&
            asBoolean(asList(parsedBool)->items[1]),
            "String::Parse Bool should package the value and success flag.");
}

void ExpandedListAndRangeNodesOperate()
{
    RuntimeFixture fixture;
    ObjList* source =
        MakeList({ Value(3.0), Value(1.0), Value(3.0), Value(2.0) });
    const Value inserted = fixture.CallNative(
        "List::Insert", { Value(source), Value(1.0), Value(9.0) });
    Require(isNumber(inserted) && asNumber(inserted) == 5.0 &&
            asNumber(source->items[1]) == 9.0,
            "List::Insert should mutate the list and return its new length.");
    source->items.erase(source->items.begin() + 1);

    const Value slice = fixture.CallNative(
        "List::Slice", { Value(source), Value(1.0), Value(2.0) });
    const Value reversed = fixture.CallNative("List::Reverse", { Value(source) });
    const Value sorted = fixture.CallNative("List::Sort", { Value(source) });
    const Value distinct = fixture.CallNative("List::Distinct", { Value(source) });
    const Value enumerated =
        fixture.CallNative("List::Enumerate", { Value(source) });
    const Value zipped = fixture.CallNative(
        "List::Zip",
        { Value(source), Value(MakeList({ StringValue("a"), StringValue("b") })) });
    Require(isList(slice) && asList(slice)->items.size() == 2 &&
            asNumber(asList(slice)->items[0]) == 1.0 &&
            asNumber(asList(slice)->items[1]) == 3.0,
            "List::Slice should return the selected window.");
    Require(isList(reversed) && asNumber(asList(reversed)->items[0]) == 2.0 &&
            asNumber(asList(reversed)->items[3]) == 3.0,
            "List::Reverse should return reverse order.");
    Require(isList(sorted) && asNumber(asList(sorted)->items[0]) == 1.0 &&
            asNumber(asList(sorted)->items[3]) == 3.0,
            "List::Sort should return ascending order.");
    Require(isList(distinct) && asList(distinct)->items.size() == 3 &&
            asNumber(asList(distinct)->items[0]) == 3.0 &&
            asNumber(asList(distinct)->items[2]) == 2.0,
            "List::Distinct should preserve first occurrences.");
    Require(isList(enumerated) && isList(asList(enumerated)->items[0]) &&
            asNumber(asList(asList(enumerated)->items[0])->items[0]) == 0.0 &&
            asNumber(asList(asList(enumerated)->items[0])->items[1]) == 3.0,
            "List::Enumerate should pair each value with its index.");
    Require(isList(zipped) && asList(zipped)->items.size() == 2 &&
            isList(asList(zipped)->items[1]) &&
            asString(asList(asList(zipped)->items[1])->items[1])->chars == "b",
            "List::Zip should pair inputs up to the shorter length.");

    ObjList* clearTarget = MakeList({ Value(1.0), Value(2.0) });
    const Value cleared =
        fixture.CallNative("List::Clear", { Value(clearTarget) });
    Require(isNumber(cleared) && asNumber(cleared) == 0.0 &&
            clearTarget->items.empty(),
            "List::Clear should empty the list.");

    const Value rangeValue = fixture.CallNative("Range::Make Advanced",
        { Value(0.0), Value(6.0), Value(2.0), Value(false), Value(false) });
    Require(isRange(rangeValue), "Range::Make Advanced should return a range.");
    ObjRange* range = asRange(rangeValue);
    Require(range->length() == 2 && range->getValue(0) == 2.0 &&
            range->getValue(1) == 4.0 && range->contains(4.0) &&
            !range->contains(6.0),
            "Advanced ranges should honor step and endpoint inclusion.");
}

void FilePathAndConsoleNodesOperate()
{
    RuntimeFixture fixture;
    const auto unique =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("visual-lox-tests-" + std::to_string(unique));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    Require(!error, "Expected to create a standard-library test directory.");
    const std::filesystem::path file = directory / "sample.txt";
    const std::string fileText = file.string();

    const Value write = fixture.CallNative(
        "File::Write Text", { StringValue(fileText), StringValue("first") });
    const Value append = fixture.CallNative(
        "File::Append Text", { StringValue(fileText), StringValue("-second") });
    const Value read =
        fixture.CallNative("File::Read Text", { StringValue(fileText) });
    const Value listing = fixture.CallNative(
        "File::List Directory", { StringValue(directory.string()) });
    Require(isList(write) && asBoolean(asList(write)->items[0]) &&
            isList(append) && asBoolean(asList(append)->items[0]),
            "File write and append should return structured success results.");
    Require(isList(read) && asString(asList(read)->items[0])->chars ==
                "first-second" && asBoolean(asList(read)->items[1]),
            "File::Read Text should return content, success, and error outputs.");
    Require(isList(listing) && isList(asList(listing)->items[0]) &&
            asList(asList(listing)->items[0])->items.size() == 1 &&
            asBoolean(asList(listing)->items[1]),
            "File::List Directory should return entries and status.");

    const Value combined = fixture.CallNative("Path::Combine",
        { StringValue(directory.string()), StringValue("sample.txt") });
    const Value extension =
        fixture.CallNative("Path::Extension", { StringValue(fileText) });
    const Value filename =
        fixture.CallNative("Path::Filename", { StringValue(fileText) });
    const Value parent =
        fixture.CallNative("Path::Parent", { StringValue(fileText) });
    Require(isString(combined) &&
            std::filesystem::path(asString(combined)->chars) == file &&
            asString(extension)->chars == ".txt" &&
            asString(filename)->chars == "sample.txt" &&
            std::filesystem::path(asString(parent)->chars) == directory,
            "Path nodes should expose combine, extension, filename, and parent.");

    std::istringstream input("typed input\n");
    std::streambuf* originalInput = std::cin.rdbuf(input.rdbuf());
    const Value console = fixture.CallNative("Console::Read Input", {});
    std::cin.rdbuf(originalInput);
    std::cin.clear();
    Require(isString(console) && asString(console)->chars == "typed input",
            "Console::Read Input should return one line.");

    std::filesystem::remove_all(directory, error);
    Require(!error, "Expected to clean up the standard-library test directory.");
}

void NewLinksReplaceOccupiedConnections()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    const CompiledNodeDefPtr addDefinition = fixture.registry.FindCompiled("Math::Add");
    const CompiledNodeDefPtr printDefinition = fixture.registry.FindCompiled("Debug::Print");
    NodePtr add = addDefinition->MakeNode(fixture.ids);
    NodePtr print = printDefinition->MakeNode(fixture.ids);
    NodePtr replacementAdd = addDefinition->MakeNode(fixture.ids);
    NodePtr replacementPrint = printDefinition->MakeNode(fixture.ids);
    for (const NodePtr& node : { begin, add, print, replacementAdd, replacementPrint })
        AttachNode(script.main->Graph, node);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, print->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        add->Outputs[0].ID, print->Inputs[1].ID));

    Link replacementData(fixture.ids.GetNextId(),
        replacementAdd->Outputs[0].ID, print->Inputs[1].ID);
    Require(script.main->Graph.CanCreateLink(
                &replacementAdd->Outputs[0], &print->Inputs[1], {}) ==
                ELinkQueryResult::Possible,
            "An occupied data input should accept a valid replacement connection.");
    script.main->Graph.AddLink(replacementData);
    Require(script.main->Graph.GetLinks().size() == 2,
            "Replacing a data input connection should remove the previous link.");
    Require(GraphUtils::FindConnectedOutput(script.main->Graph, print->Inputs[1])->Node ==
                replacementAdd,
            "The replacement data connection should become active.");

    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, replacementPrint->Inputs[0].ID));
    Require(script.main->Graph.GetLinks().size() == 2,
            "Replacing a flow output connection should remove the previous link.");
    const std::vector<const Pin*> targets =
        GraphUtils::FindConnectedInputs(script.main->Graph, begin->Outputs[0]);
    Require(targets.size() == 1 && targets[0]->Node == replacementPrint,
            "The replacement flow connection should become active.");
}
}

void AddRuntimeTests(Tests::Runner& runner)
{
    runner.Group("Runtime / standard library", [&]()
    {
        runner.Test("file, path, and console nodes operate",
            FilePathAndConsoleNodesOperate);
        runner.Test("node definitions declare their capabilities", StandardLibraryDeclaresCapabilities);
        runner.Test("list native nodes operate on lists", ListNativeNodesOperateOnLists);
        runner.Test("range native nodes support both directions", RangeNativeNodesSupportBothDirections);
        runner.Test("expanded math and string nodes operate",
            ExpandedMathAndStringNodesOperate);
        runner.Test("expanded list and range nodes operate",
            ExpandedListAndRangeNodesOperate);
    });
    runner.Group("Runtime / VM boundaries", [&]()
    {
        runner.Test("repeated interpretation releases the stack", RepeatedInterpretationReleasesStack);
        runner.Test("large list literals preserve their items", LargeListLiteralsPreserveItems);
        runner.Test("Flow For In keeps a constant stack footprint", ForInKeepsConstantStackFootprint);
        runner.Test("Main receives program arguments as a string list",
            MainReceivesProgramArgumentsAsAStringList);
        runner.Test("functions and methods support multiple outputs",
            FunctionsAndMethodsSupportMultipleOutputs);
    });
    runner.Group("Runtime / validation and compilation", [&]()
    {
        runner.Test("instance errors do not change definition capabilities",
            InstanceErrorsDoNotChangeDefinitionCapabilities);
        runner.Test("a missing Begin node is rejected", MissingBeginIsRejected);
        runner.Test("dependency cycles are rejected", DependencyCyclesAreRejected);
        runner.Test("pure nodes are constant folded", PureNodesAreConstantFolded);
        runner.Test("complete expression nodes compile and execute",
            CompleteExpressionNodesCompileAndExecute);
        runner.Test("While and Repeat nodes compile and execute",
            WhileAndRepeatNodesCompileAndExecute);
        runner.Test("classes, ranges, and matching round-trip and execute",
            ClassesRangesAndMatchingRoundTripAndExecute);
    });
    runner.Group("Runtime / graph links", [&]()
    {
        runner.Test("new links replace occupied connections", NewLinksReplaceOccupiedConnections);
    });
}
