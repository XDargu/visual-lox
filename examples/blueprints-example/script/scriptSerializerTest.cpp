#include "scriptSerializerTest.h"

#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/function.h"
#include "../native/nodes/math.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"
#include "../runtime/scriptRuntime.h"
#include "../runtime/standardLibrary.h"
#include "../tests/testFramework.h"
#include "scriptSerializer.h"

#include <Vm.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{
using Tests::Require;

void AttachNode(Graph& graph, const NodePtr& node)
{
    NodeUtils::BuildNode(node);
    graph.AddNode(node);
}

std::string ReadFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

struct TemporaryRoundTripFiles
{
    explicit TemporaryRoundTripFiles(std::string basePath)
        : first(std::move(basePath))
        , second(first + ".second")
    {
    }

    ~TemporaryRoundTripFiles()
    {
        std::remove(first.c_str());
        std::remove(second.c_str());
    }

    std::string first;
    std::string second;
};

struct SerializerFixture
{
    SerializerFixture()
        : vm(VM::getInstance())
        , wasGcAllowed(vm.isGarbageCollectionAllowed())
    {
        vm.allowGarbageCollection(false);
        RegisterStandardLibrary(registry);
        registry.RegisterNatives(vm);
        BuildScript();
    }

    ~SerializerFixture()
    {
        vm.setExternalMarkingFunc([]() {});
        vm.allowGarbageCollection(wasGcAllowed);
    }

    void BuildScript()
    {
        script.ID = ids.GetNextId();
        script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
        NodePtr mainBegin = BuildBeginNode(ids, script.main);
        mainBegin->State = "{\"location\": [20, 40]}";
        AttachNode(script.main->Graph, mainBegin);

        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(ids.GetNextId(), "Samples");
        ObjList* nested = newList();
        nested->append(Value(true));
        nested->append(Value(42.5));
        nested->append(Value(copyString("hello", 5)));
        property->defaultValue = Value(nested);
        script.variables.push_back(property);
        AttachNode(script.main->Graph, BuildGetVariableNode(ids, property));

        const CompiledNodeDefPtr addDefinition = registry.FindCompiled("Math::Add");
        Require(static_cast<bool>(addDefinition), "Compiled definition was not registered.");
        NodePtr add = addDefinition->MakeNode(ids);
        add->InputValues[0] = Value(2.0);
        add->InputValues[1] = Value(3.0);
        AttachNode(script.main->Graph, add);

        const NativeFunctionDef* squareDefinition = registry.FindNative("Math::Square");
        Require(squareDefinition != nullptr, "Native definition was not registered.");
        NodePtr square = squareDefinition->functionDef->MakeNode(ids, ScriptElementID::Invalid);
        square->InputValues[0] = Value(7.0);
        AttachNode(script.main->Graph, square);

        ScriptFunctionPtr function = std::make_shared<ScriptFunction>(ids.GetNextId(), "Echo");
        function->functionDef->inputs.push_back(
            { "Value", Value(copyString("default", 7)), ids.GetNextId() });
        function->functionDef->outputs.push_back(
            { "Result", Value(copyString("", 0)), ids.GetNextId() });
        NodePtr begin = BuildBeginNode(ids, function);
        NodePtr returnNode = BuildReturnNode(ids, *function);
        AttachNode(function->Graph, begin);
        AttachNode(function->Graph, returnNode);

        Link flowLink(ids.GetNextId(), begin->Outputs[0].ID, returnNode->Inputs[0].ID);
        flowLink.Color = GetIconColor(PinType::Flow);
        function->Graph.AddLink(flowLink);
        Link valueLink(ids.GetNextId(), begin->Outputs[1].ID, returnNode->Inputs[1].ID);
        valueLink.Color = GetIconColor(PinType::String);
        function->Graph.AddLink(valueLink);
        script.functions.push_back(function);

        AttachNode(script.main->Graph, function->functionDef->MakeNode(ids, function->ID));
        AttachNode(script.main->Graph,
            BuildGetFunctionNode(ids, function->functionDef, function->ID));
    }

    void SaveAndLoad(const std::string& path)
    {
        SerializationResult result = ScriptSerializer::Save(script, path);
        Require(result.success, result.error.c_str());
        result = ScriptSerializer::Load(path, registry, loaded, loadedIds);
        Require(result.success, result.error.c_str());
    }

    VM& vm;
    bool wasGcAllowed;
    NodeRegistry registry;
    IDGenerator ids;
    Script script;
    IDGenerator loadedIds;
    Script loaded;
};

void RoundTripPreservesStructure(const std::string& outputPath)
{
    TemporaryRoundTripFiles files(outputPath + ".structure");
    SerializerFixture fixture;
    fixture.SaveAndLoad(files.first);
    Require(fixture.loaded.ID.id == fixture.script.ID.id,
            "Script ID changed during round trip.");
    Require(fixture.loaded.main && fixture.loaded.main->functionDef->name == "Main",
            "Main function was not restored.");
    Require(fixture.loaded.main->Graph.GetNodes().size() ==
                fixture.script.main->Graph.GetNodes().size(),
            "Main graph node count changed.");
    Require(fixture.loaded.functions.size() == 1, "Function count changed.");
    Require(fixture.loaded.functions[0]->Graph.GetLinks().size() == 2,
            "Function graph links were not restored.");
    Require(fixture.loaded.variables.size() == 1 &&
            isList(fixture.loaded.variables[0]->defaultValue),
            "List property was not restored.");
    Require(asList(fixture.loaded.variables[0]->defaultValue)->items.size() == 3,
            "List property contents changed.");
    Require(fixture.loadedIds.PeekNextId() == fixture.ids.PeekNextId(),
            "ID generator did not resume after the maximum persisted ID.");
}

void RoundTripIsDeterministic(const std::string& outputPath)
{
    TemporaryRoundTripFiles files(outputPath + ".deterministic");
    SerializerFixture fixture;
    fixture.SaveAndLoad(files.first);
    const SerializationResult result = ScriptSerializer::Save(fixture.loaded, files.second);
    Require(result.success, result.error.c_str());
    Require(ReadFile(files.first) == ReadFile(files.second),
            "Saving a loaded script changed the document.");
}

void RoundTrippedScriptCompilesAndExecutes(const std::string& outputPath)
{
    TemporaryRoundTripFiles files(outputPath + ".execution");
    SerializerFixture fixture;
    fixture.SaveAndLoad(files.first);
    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(fixture.loaded);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, fixture.loaded);
    Require(static_cast<bool>(compiled), "The round-tripped script did not compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "The round-tripped script did not execute successfully.");
}
}

void AddScriptSerializerTests(Tests::Runner& runner, const std::string& outputPath)
{
    runner.Group("Serialization / round trip", [&]()
    {
        runner.Test("script structure is preserved", [&]()
        {
            RoundTripPreservesStructure(outputPath);
        });
        runner.Test("serialized output is deterministic", [&]()
        {
            RoundTripIsDeterministic(outputPath);
        });
        runner.Test("a loaded script compiles and executes", [&]()
        {
            RoundTrippedScriptCompilesAndExecutes(outputPath);
        });
    });
}
