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

    VM& vm;
    bool wasGcAllowed;
    NodeRegistry registry;
    IDGenerator ids;
};

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
        runner.Test("node definitions declare their capabilities", StandardLibraryDeclaresCapabilities);
        runner.Test("list native nodes operate on lists", ListNativeNodesOperateOnLists);
        runner.Test("range native nodes support both directions", RangeNativeNodesSupportBothDirections);
    });
    runner.Group("Runtime / VM boundaries", [&]()
    {
        runner.Test("repeated interpretation releases the stack", RepeatedInterpretationReleasesStack);
        runner.Test("large list literals preserve their items", LargeListLiteralsPreserveItems);
        runner.Test("Flow For In keeps a constant stack footprint", ForInKeepsConstantStackFootprint);
        runner.Test("Main receives program arguments as a string list",
            MainReceivesProgramArgumentsAsAStringList);
    });
    runner.Group("Runtime / validation and compilation", [&]()
    {
        runner.Test("instance errors do not change definition capabilities",
            InstanceErrorsDoNotChangeDefinitionCapabilities);
        runner.Test("a missing Begin node is rejected", MissingBeginIsRejected);
        runner.Test("dependency cycles are rejected", DependencyCyclesAreRejected);
        runner.Test("pure nodes are constant folded", PureNodesAreConstantFolded);
        runner.Test("classes, ranges, and matching round-trip and execute",
            ClassesRangesAndMatchingRoundTripAndExecute);
    });
    runner.Group("Runtime / graph links", [&]()
    {
        runner.Test("new links replace occupied connections", NewLinksReplaceOccupiedConnections);
    });
}
