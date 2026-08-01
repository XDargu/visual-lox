#include "scriptSerializerTest.h"

#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/commentBox.h"
#include "../native/nodes/function.h"
#include "../native/nodes/math.h"
#include "../native/nodes/object.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"
#include "../runtime/scriptRuntime.h"
#include "../runtime/standardLibrary.h"
#include "../tests/testFramework.h"
#include "scriptSerializer.h"

#include <Vm.h>

#include <algorithm>
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
        NodePtr commentBox = BuildCommentBoxNode(ids, "Inputs are prepared here", CommentBoxColor::Blue);
        commentBox->State = "{\"location\": [100, 80], \"size\": [360, 220]}";
        AttachNode(script.main->Graph, commentBox);

        ScriptClassPtr student =
            std::make_shared<ScriptClass>(ids.GetNextId(), "Student");
        script.classes.push_back(student);

        ScriptFunctionPtr accepts =
            std::make_shared<ScriptFunction>(ids.GetNextId(), "Accepts");
        accepts->functionDef->description = "Checks a candidate value.";
        accepts->functionDef->flags |= NodeDefinitionFlags::Pure;
        accepts->functionDef->inputs.push_back(
            { "Value", Value(copyString("", 0)), ids.GetNextId(),
              PinType::String, "The value to check" });
        accepts->functionDef->outputs.push_back(
            { "Accepted", Value(false), ids.GetNextId(),
              PinType::Bool, "Whether the value is accepted" });
        NodePtr acceptsBegin = BuildBeginNode(ids, accepts);
        NodePtr acceptsReturn = BuildReturnNode(ids, *accepts);
        AttachNode(accepts->Graph, acceptsBegin);
        AttachNode(accepts->Graph, acceptsReturn);
        accepts->Graph.AddLink(Link(
            ids.GetNextId(), acceptsBegin->Outputs[0].ID,
            acceptsReturn->Inputs[0].ID));
        AttachNode(
            accepts->Graph,
            BuildGetMethodNode(
                ids, accepts, ScriptElementID::Invalid,
                TypeRef::Object(student->ID.id, student->Name)));
        student->methods.push_back(accepts);

        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(ids.GetNextId(), "Samples");
        property->Description = "Mixed sample values.";
        ObjList* nested = newList();
        nested->append(Value(true));
        nested->append(Value(42.5));
        nested->append(Value(copyString("hello", 5)));
        property->type = TypeRef::List(PinType::Any);
        property->defaultValue = Value(nested);
        script.variables.push_back(property);
        AttachNode(script.main->Graph, BuildGetVariableNode(ids, property));
        ScriptPropertyPtr advisor =
            std::make_shared<ScriptProperty>(ids.GetNextId(), "Advisor");
        advisor->Description = "A student advisor, or nil.";
        advisor->type = TypeRef::Object(student->ID.id, student->Name);
        advisor->defaultValue = Value();
        script.variables.push_back(advisor);
        ScriptPropertyPtr sorter =
            std::make_shared<ScriptProperty>(ids.GetNextId(), "Sorter");
        sorter->Description = "A typed higher-order sorting function.";
        sorter->type = TypeRef::Function(
            { TypeRef::List(PinType::String),
              TypeRef::Function(
                  { PinType::String, PinType::String },
                  { PinType::Bool }) },
            { TypeRef::List(PinType::String) });
        sorter->defaultValue = Value();
        script.variables.push_back(sorter);

        ScriptPropertyPtr scores = std::make_shared<ScriptProperty>(ids.GetNextId(), "Scores");
        scores->Description = "Scores by student name.";
        scores->type = TypeRef::Map(PinType::String, PinType::Float);
        ObjMap* scoreValues = newMap();
        scoreValues->set(Value(copyString("Ada", 3)), Value(9.5));
        scoreValues->set(Value(copyString("Linus", 5)), Value(8.0));
        scores->defaultValue = Value(scoreValues);
        script.variables.push_back(scores);

        const CompiledNodeDefPtr addDefinition = registry.FindCompiled("Math::Add");
        Require(static_cast<bool>(addDefinition), "Compiled definition was not registered.");
        NodePtr add = addDefinition->MakeNode(ids);
        add->InputValues[0] = Value(2.0);
        add->InputValues[1] = Value(3.0);
        add->AddInput(ids);
        add->AddInput(ids);
        add->InputValues[2] = Value(4.0);
        add->InputValues[3] = Value(5.0);
        AttachNode(script.main->Graph, add);

        const NativeFunctionDef* squareDefinition = registry.FindNative("Math::Square");
        Require(squareDefinition != nullptr, "Native definition was not registered.");
        NodePtr square = squareDefinition->functionDef->MakeNode(ids, ScriptElementID::Invalid);
        square->InputValues[0] = Value(7.0);
        AttachNode(script.main->Graph, square);

        NodePtr anyList =
            registry.FindNative("List::MakeList")->functionDef->MakeNode(
                ids, ScriptElementID::Invalid);
        anyList->AddInput(ids);
        anyList->TypeOverrides["T"] = PinType::Any;
        AttachNode(script.main->Graph, anyList);

        ScriptFunctionPtr function = std::make_shared<ScriptFunction>(ids.GetNextId(), "Echo");
        function->functionDef->description = "Returns the supplied text.";
        function->functionDef->flags |= NodeDefinitionFlags::Pure;
        function->functionDef->genericTypeProperties.push_back(
            { "T", "Value Type" });
        function->functionDef->inputs.push_back(
            { "Value", Value(copyString("default", 7)), ids.GetNextId(),
              TypeRef::Variable("T"), "Value to return." });
        function->functionDef->outputs.push_back(
            { "Result", Value(copyString("", 0)), ids.GetNextId(),
              TypeRef::Variable("T"), "The returned value." });
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
    Require(fixture.loaded.ModuleIdentity == fixture.script.ModuleIdentity,
            "Module UUID changed during round trip.");
    Require(fixture.loaded.main && fixture.loaded.main->functionDef->name == "Main",
            "Main function was not restored.");
    Require(fixture.loaded.main->Graph.GetNodes().size() ==
                fixture.script.main->Graph.GetNodes().size(),
            "Main graph node count changed.");
    const NodePtr loadedCommentBox = fixture.loaded.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->Type == NodeType::CommentBox; });
    Require(loadedCommentBox && loadedCommentBox->SerializationType == "comment_box" &&
            loadedCommentBox->Name == "Inputs are prepared here" &&
            loadedCommentBox->State == "{\"location\": [100, 80], \"size\": [360, 220]}" &&
            loadedCommentBox->Inputs.empty() && loadedCommentBox->Outputs.empty() &&
            static_cast<CommentBoxNode*>(loadedCommentBox.get())->BoxColor == CommentBoxColor::Blue,
            "Comment box text, color, layout state, or pin layout changed.");
    Require(fixture.loaded.functions.size() == 1, "Function count changed.");
    Require(fixture.loaded.functions[0]->PersistentId == fixture.script.functions[0]->PersistentId &&
            fixture.loaded.functions[0]->functionDef->inputs[0].persistentId == fixture.script.functions[0]->functionDef->inputs[0].persistentId,
            "Script element or script port UUID changed during round trip.");
    Require(fixture.loaded.functions[0]->Graph.GetLinks().size() == 2,
            "Function graph links were not restored.");
    Require(fixture.loaded.functions[0]->Graph.GetLinks()[0].PersistentId == fixture.script.functions[0]->Graph.GetLinks()[0].PersistentId,
            "Link UUID changed during round trip.");
    Require(fixture.loaded.variables.size() == 4 &&
            isList(fixture.loaded.variables[0]->defaultValue),
            "List property was not restored.");
    Require(asList(fixture.loaded.variables[0]->defaultValue)->items.size() == 3,
            "List property contents changed.");
    Require(fixture.loaded.variables[0]->type == TypeRef::List(PinType::Any),
            "The variable declaration type changed.");
    Require(fixture.loaded.classes.size() == 1 &&
            fixture.loaded.variables[1]->type ==
                TypeRef::Object(
                    fixture.loaded.classes[0]->ID.id, "Student") &&
            isNil(fixture.loaded.variables[1]->defaultValue),
            "Nil-capable script-class types changed.");
    Require(fixture.loaded.variables[2]->type.kind == PinType::Function &&
            fixture.loaded.variables[2]->type.parameters.size() == 3 &&
            fixture.loaded.variables[2]->type.parameters[1].kind ==
                PinType::Function,
            "Nested function signatures changed.");
    Require(fixture.loaded.variables[3]->type == TypeRef::Map(PinType::String, PinType::Float) && isMap(fixture.loaded.variables[3]->defaultValue) &&
            asMap(fixture.loaded.variables[3]->defaultValue)->size() == 2,
            "Map declarations or default entries changed.");
    Value adaScore;
    Require(asMap(fixture.loaded.variables[3]->defaultValue)->get(Value(copyString("Ada", 3)), &adaScore) && isNumber(adaScore) && asNumber(adaScore) == 9.5,
            "Map keys and values were not restored.");
    Require(fixture.loaded.variables[0]->Description ==
                "Mixed sample values." &&
            fixture.loaded.functions[0]->functionDef->description ==
                "Returns the supplied text." &&
            fixture.loaded.functions[0]->functionDef->inputs[0].description ==
                "Value to return.",
            "Script and port descriptions changed.");
    Require(fixture.loaded.functions[0]->functionDef->genericTypeProperties.size() == 1 &&
            fixture.loaded.functions[0]->functionDef->genericTypeProperties[0].variableName == "T" &&
            fixture.loaded.functions[0]->functionDef->genericTypeProperties[0].label == "Value Type",
            "Custom function generic type properties changed.");
    Require(HasFlag(fixture.loaded.functions[0]->functionDef->flags,
                    NodeDefinitionFlags::Pure),
            "Script function purity changed.");
    const NodePtr loadedVariable = fixture.loaded.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->SerializationType == "variable.get"; });
    Require(loadedVariable && loadedVariable->Outputs[0].DeclaredType ==
                TypeRef::List(PinType::Any),
            "The pin declaration type changed.");
    const NodePtr loadedFunction = fixture.loaded.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->SerializationType == "function.get"; });
    Require(loadedFunction && loadedFunction->Outputs[0].Type.kind == PinType::Function &&
            !loadedFunction->Outputs[0].Type.parameters.empty() &&
            loadedFunction->GenericTypeProperties.size() == 1 &&
            loadedFunction->GenericTypeProperties[0].variableName == "T",
            "First-class generic function signatures were not restored.");
    const NodePtr loadedMethod = fixture.loaded.classes[0]->methods[0]->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->SerializationType == "method.get"; });
    Require(loadedMethod &&
            loadedMethod->Inputs[0].Type ==
                TypeRef::Object(fixture.loaded.classes[0]->ID.id, "Student") &&
            loadedMethod->Outputs[0].Type ==
                TypeRef::Function({ PinType::String }, { PinType::Bool }),
            "Method Get nodes and their function signatures were not restored.");
    const NodePtr loadedAnyList = fixture.loaded.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->DefinitionId == "vlox.std.native.list.makelist"; });
    Require(loadedAnyList && loadedAnyList->TypeOverrides.at("T") == PinType::Any &&
            loadedAnyList->Outputs[0].Type == TypeRef::List(PinType::Any) &&
            loadedAnyList->GenericTypeProperties.size() == 1 &&
            loadedAnyList->GenericTypeProperties[0].variableName == "T",
            "The explicit MakeList element type changed.");
    const NodePtr loadedAdd = fixture.loaded.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->DefinitionId == "vlox.std.compiled.math.add"; });
    Require(loadedAdd && loadedAdd->Inputs.size() == 4 && loadedAdd->InputValues.size() == 4 &&
            isNumber(loadedAdd->InputValues[3]) && asNumber(loadedAdd->InputValues[3]) == 5.0 &&
            loadedAdd->CanAddInput() && loadedAdd->CanRemoveInput(loadedAdd->Inputs[3].ID),
            "Compiled dynamic inputs and values were not restored.");
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

void AddedNativeInputUsesCurrentDefault(const std::string& outputPath)
{
    TemporaryRoundTripFiles files(outputPath + ".added-native-input");
    SerializerFixture fixture;
    SerializationResult result = ScriptSerializer::Save(fixture.script, files.first);
    Require(result.success, result.error.c_str());

    const NativeFunctionDef* square = fixture.registry.FindNative("Math::Square");
    Require(square && square->functionDef, "Square definition was not registered.");
    BasicFunctionDef::Input added("Precision", Value(6.0), -1, PinType::Float, "New optional precision input.");
    added.key = "precision";
    square->functionDef->inputs.push_back(std::move(added));

    result = ScriptSerializer::Load(files.first, fixture.registry, fixture.loaded, fixture.loadedIds);
    Require(result.success, result.error.c_str());
    const NodePtr loadedSquare = fixture.loaded.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->DefinitionId == "vlox.std.native.math.square"; });
    Require(loadedSquare && loadedSquare->Inputs.size() == 2 && loadedSquare->InputValues.size() == 2,
        "Adding a native input prevented the old node layout from reconciling.");
    Require(isNumber(loadedSquare->InputValues[0]) && asNumber(loadedSquare->InputValues[0]) == 7.0 &&
        isNumber(loadedSquare->InputValues[1]) && asNumber(loadedSquare->InputValues[1]) == 6.0,
        "The saved value or current default moved to the wrong native input.");
}

void ScriptPortIdentitySurvivesDefinitionEvolution()
{
    SerializerFixture fixture;
    const ScriptFunctionPtr function = fixture.script.functions.front();
    const NodePtr call = fixture.script.main->Graph.FindNodeIf([&](const NodePtr& node)
    {
        return node->SerializationType == "function.call" && node->refId.id == function->ID.id;
    });
    Require(call && call->Inputs.size() == 1, "Echo call node was not created.");

    const ed::PinId originalPinId = call->Inputs[0].ID;
    const int originalPortId = function->functionDef->inputs[0].id;
    const ScriptPortId originalPersistentPortId = function->functionDef->inputs[0].persistentId;
    call->InputValues[0] = Value(copyString("custom", 6));
    const NodePtr sourceCall = function->functionDef->MakeNode(fixture.ids, function->ID);
    AttachNode(fixture.script.main->Graph, sourceCall);
    Link preservedLink(fixture.ids.GetNextId(), sourceCall->Outputs[0].ID, originalPinId);
    fixture.script.main->Graph.AddLink(preservedLink);

    BasicFunctionDef::Input added("Prefix", Value(copyString("prefix", 6)), fixture.ids.GetNextId(), TypeRef::Variable("T"), "Text prefix.");
    function->functionDef->inputs.insert(function->functionDef->inputs.begin(), std::move(added));
    function->functionDef->inputs[1].name = "Renamed Value";
    call->Refresh(fixture.script, fixture.ids);

    Require(call->Inputs.size() == 2 && call->Inputs[1].ID == originalPinId && call->Inputs[1].Identity.legacyScriptPortId == originalPortId,
        "Renaming or reordering a script port changed its call-site identity.");
    Require(isString(call->InputValues[0]) && asString(call->InputValues[0])->chars == "prefix" &&
        isString(call->InputValues[1]) && asString(call->InputValues[1])->chars == "custom",
        "Renaming or reordering a script port moved its literal value.");

    function->functionDef->inputs.erase(function->functionDef->inputs.begin() + 1);
    call->Refresh(fixture.script, fixture.ids);
    fixture.script.main->Graph.RefreshTypes();
    Require(call->Inputs.size() == 1 && call->UnresolvedInputs.size() == 1 && call->UnresolvedInputs[0].ID == originalPinId &&
        fixture.script.main->Graph.FindPin(originalPinId) == &call->UnresolvedInputs[0],
        "Removing a script port did not preserve its recoverable call-site data.");
    Require(fixture.script.main->Graph.GetLinks().back().PersistentId == preservedLink.PersistentId && !fixture.script.main->Graph.GetLinks().back().IsResolved,
        "A link to a removed script port was discarded or left active.");

    BasicFunctionDef::Input restored("Restored Value", Value(copyString("default", 7)), originalPortId, TypeRef::Variable("T"), "Restored value.");
    restored.persistentId = originalPersistentPortId;
    function->functionDef->inputs.push_back(std::move(restored));
    call->Refresh(fixture.script, fixture.ids);
    fixture.script.main->Graph.RefreshTypes();
    Require(call->UnresolvedInputs.empty() && call->Inputs[1].ID == originalPinId && isString(call->InputValues[1]) && asString(call->InputValues[1])->chars == "custom",
        "Restoring a script port did not resolve its original pin and value.");
    Require(fixture.script.main->Graph.GetLinks().back().IsResolved,
        "Restoring a script port did not reactivate its preserved link.");
}

void UnavailableDefinitionRemainsRecoverable()
{
    SerializerFixture fixture;
    const NodePtr source = fixture.script.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->DefinitionId == "vlox.std.compiled.math.add"; });
    Require(source != nullptr, "Add node was not created.");
    const GraphNodeId sourceId = source->PersistentId;
    const size_t inputCount = source->Inputs.size();
    source->SerializedExtensions["plugin_payload"] = "{\"mode\":\"preserve-me\",\"revision\":3}";

    fixture.registry.compiledDefinitions.erase(std::remove_if(fixture.registry.compiledDefinitions.begin(), fixture.registry.compiledDefinitions.end(),
        [](const CompiledNodeDefPtr& definition) { return definition && definition->id == "vlox.std.compiled.math.add"; }), fixture.registry.compiledDefinitions.end());

    std::string document;
    SerializationResult result = ScriptSerializer::SerializeToString(fixture.script, document);
    Require(result.success, result.error.c_str());
    result = ScriptSerializer::DeserializeFromString(document, fixture.registry, fixture.loaded, fixture.loadedIds);
    Require(result.success, result.error.c_str());
    const NodePtr missing = fixture.loaded.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->DefinitionId == "vlox.std.compiled.math.add"; });
    Require(missing && HasFlag(missing->InstanceFlags, NodeInstanceFlags::Error) && missing->PersistentId == sourceId && missing->Inputs.size() == inputCount &&
        missing->SerializedExtensions.at("plugin_payload") == "{\"mode\":\"preserve-me\",\"revision\":3}",
        "An unavailable definition did not load as a recoverable node.");

    std::string savedAgain;
    result = ScriptSerializer::SerializeToString(fixture.loaded, savedAgain);
    Require(result.success, result.error.c_str());
    Script reloaded;
    IDGenerator reloadedIds;
    result = ScriptSerializer::DeserializeFromString(savedAgain, fixture.registry, reloaded, reloadedIds);
    Require(result.success, result.error.c_str());
    const NodePtr missingAgain = reloaded.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->DefinitionId == "vlox.std.compiled.math.add"; });
    Require(missingAgain && missingAgain->PersistentId == sourceId && missingAgain->Inputs.size() == inputCount &&
        missingAgain->SerializedExtensions.at("plugin_payload") == "{\"mode\":\"preserve-me\",\"revision\":3}",
        "A recoverable unavailable node did not survive load-save-load.");
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
        runner.Test("an added native input uses its current default", [&]()
        {
            AddedNativeInputUsesCurrentDefault(outputPath);
        });
        runner.Test("script ports survive rename, reorder, removal, and restoration", ScriptPortIdentitySurvivesDefinitionEvolution);
        runner.Test("an unavailable definition remains recoverable", UnavailableDefinitionRemainsRecoverable);
    });
}
