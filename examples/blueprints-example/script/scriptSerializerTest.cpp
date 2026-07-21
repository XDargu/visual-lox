#include "scriptSerializerTest.h"

#include "scriptSerializer.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/function.h"
#include "../native/nodes/math.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"

#include <Vm.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

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
}

int RunScriptSerializerRoundTripTest(const std::string& outputPath)
{
    const std::string secondPath = outputPath + ".second";
    const std::string logPath = outputPath + ".log";
    try
    {
        VM& vm = VM::getInstance();
        const bool wasGcAllowed = vm.isGarbageCollectionAllowed();
        vm.allowGarbageCollection(false);

        NodeRegistry registry;
        registry.RegisterCompiledNode("Math::Add", &CreateAddNode,
            { { "Value", Value(0.0) } }, { { "Value", Value(0.0) } });
        registry.RegisterDefinitions();

        IDGenerator ids;
        Script script;
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
        function->functionDef->inputs.push_back({ "Value", Value(copyString("default", 7)), ids.GetNextId() });
        function->functionDef->outputs.push_back({ "Result", Value(copyString("", 0)), ids.GetNextId() });
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
        AttachNode(script.main->Graph, BuildGetFunctionNode(ids, function->functionDef, function->ID));

        vm.allowGarbageCollection(wasGcAllowed);

        SerializationResult result = ScriptSerializer::Save(script, outputPath);
        Require(result.success, result.error.c_str());

        Script loaded;
        IDGenerator loadedIds;
        result = ScriptSerializer::Load(outputPath, registry, loaded, loadedIds);
        Require(result.success, result.error.c_str());
        Require(loaded.ID.id == script.ID.id, "Script ID changed during round trip.");
        Require(loaded.main && loaded.main->functionDef->name == "Main", "Main function was not restored.");
        Require(loaded.main->Graph.GetNodes().size() == script.main->Graph.GetNodes().size(), "Main graph node count changed.");
        Require(loaded.functions.size() == 1, "Function count changed.");
        Require(loaded.functions[0]->Graph.GetLinks().size() == 2, "Function graph links were not restored.");
        Require(loaded.variables.size() == 1 && isList(loaded.variables[0]->defaultValue), "List property was not restored.");
        Require(asList(loaded.variables[0]->defaultValue)->items.size() == 3, "List property contents changed.");
        Require(loadedIds.PeekNextId() == ids.PeekNextId(), "ID generator did not resume after the maximum persisted ID.");

        result = ScriptSerializer::Save(loaded, secondPath);
        Require(result.success, result.error.c_str());
        Require(ReadFile(outputPath) == ReadFile(secondPath), "Saving a loaded script changed the document.");

        std::remove(outputPath.c_str());
        std::remove(secondPath.c_str());
        std::remove(logPath.c_str());
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::ofstream log(logPath);
        log << exception.what();
        return 1;
    }
}
