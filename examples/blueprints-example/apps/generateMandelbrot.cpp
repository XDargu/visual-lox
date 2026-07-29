#include "visualApplication.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/function.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"
#include "../runtime/scriptRuntime.h"
#include "../runtime/standardLibrary.h"
#include "../script/scriptSerializer.h"
#include "../validation/scriptValidator.h"

#include <Object.h>
#include <Vm.h>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
struct Builder
{
    explicit Builder(const NodeRegistry& registry)
        : registry(registry)
    {
        script.ID = ids.GetNextId();
        script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
    }

    ScriptPropertyPtr Number(const char* name, double value)
    {
        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        property->type = PinType::Float;
        property->defaultValue = Value(value);
        script.variables.push_back(property);
        return property;
    }

    ScriptPropertyPtr NumberList(const char* name)
    {
        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        property->type = TypeRef::List(PinType::Float);
        property->defaultValue = Value(newList());
        script.variables.push_back(property);
        return property;
    }

    ScriptPropertyPtr LocalNumber(const ScriptFunctionPtr& function, const char* name, double value)
    {
        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        property->type = PinType::Float;
        property->defaultValue = Value(value);
        function->variables.push_back(property);
        return property;
    }

    NodePtr Compiled(const char* name)
    {
        CompiledNodeDefPtr definition = registry.FindCompiled(name);
        if (!definition)
            throw std::runtime_error(std::string("Missing compiled definition: ") + name);
        return definition->MakeNode(ids);
    }

    NodePtr Native(const char* name)
    {
        const NativeFunctionDef* definition = registry.FindNative(name);
        if (!definition)
            throw std::runtime_error(std::string("Missing native definition: ") + name);
        return definition->functionDef->MakeNode(ids, ScriptElementID::Invalid);
    }

    NodePtr Get(const ScriptPropertyPtr& property, const ScriptFunctionPtr& function = nullptr)
    {
        return BuildGetVariableNode(ids, property, ScriptElementID::Invalid, function ? function->ID : ScriptElementID::Invalid);
    }

    NodePtr Set(const ScriptPropertyPtr& property, const ScriptFunctionPtr& function = nullptr)
    {
        return BuildSetVariableNode(ids, property, ScriptElementID::Invalid, function ? function->ID : ScriptElementID::Invalid);
    }

    void Add(Graph& graph, std::initializer_list<NodePtr> nodes)
    {
        for (const NodePtr& node : nodes)
        {
            NodeUtils::BuildNode(node);
            graph.AddNode(node);
        }
    }

    void Link(Graph& graph, const Pin& output, const Pin& input)
    {
        graph.AddLink(::Link(ids.GetNextId(), output.ID, input.ID));
    }

    Pin& Input(const NodePtr& node, const char* name)
    {
        Pin* pin = node->FindInputByName(name);
        if (!pin)
            throw std::runtime_error("Missing input '" + std::string(name) + "' on " + node->Name);
        return *pin;
    }

    Pin& Output(const NodePtr& node, const char* name)
    {
        Pin* pin = node->FindOutputByName(name);
        if (!pin)
            throw std::runtime_error("Missing output '" + std::string(name) + "' on " + node->Name);
        return *pin;
    }

    void Default(const NodePtr& node, const char* name, Value value)
    {
        Pin& pin = Input(node, name);
        for (size_t index = 0; index < node->Inputs.size(); ++index)
            if (node->Inputs[index].ID == pin.ID)
            {
                node->InputValues[index] = value;
                return;
            }
    }

    const NodeRegistry& registry;
    IDGenerator ids;
    Script script;
};

NodePtr Binary(Builder& builder, const char* name, double right)
{
    NodePtr node = builder.Compiled(name);
    node->InputValues[1] = Value(right);
    return node;
}

void BuildRegenerate(Builder& builder, const ScriptFunctionPtr& function,
                     const ScriptPropertyPtr& centerX, const ScriptPropertyPtr& centerY,
                     const ScriptPropertyPtr& scale, const ScriptPropertyPtr& maxIterations,
                     const ScriptPropertyPtr& resolution, const ScriptPropertyPtr& pixels,
                     const ScriptPropertyPtr& revision, const ScriptPropertyPtr& coordinateX,
                     const ScriptPropertyPtr& coordinateY, const ScriptPropertyPtr& valueX,
                     const ScriptPropertyPtr& valueY, const ScriptPropertyPtr& temporaryX,
                     const ScriptPropertyPtr& iterations)
{
    Graph& graph = function->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, function);
    NodePtr clear = builder.Native("List::Clear");
    NodePtr getPixelsForClear = builder.Get(pixels, function);
    NodePtr outerRepeat = builder.Compiled("Flow::Repeat");
    NodePtr getResolutionOuter = builder.Get(resolution, function);
    NodePtr setCoordinateY = builder.Set(coordinateY, function);
    NodePtr divideY = builder.Compiled("Math::Divide");
    NodePtr getResolutionY = builder.Get(resolution, function);
    NodePtr normalizeY = Binary(builder, "Math::Subtract", 0.5);
    NodePtr getScaleY = builder.Get(scale, function);
    NodePtr scaleY = builder.Compiled("Math::Multiply");
    NodePtr getCenterY = builder.Get(centerY, function);
    NodePtr offsetY = builder.Compiled("Math::Add");
    NodePtr innerRepeat = builder.Compiled("Flow::Repeat");
    NodePtr getResolutionInner = builder.Get(resolution, function);
    NodePtr setCoordinateX = builder.Set(coordinateX, function);
    NodePtr divideX = builder.Compiled("Math::Divide");
    NodePtr getResolutionX = builder.Get(resolution, function);
    NodePtr normalizeX = Binary(builder, "Math::Subtract", 0.5);
    NodePtr getScaleX = builder.Get(scale, function);
    NodePtr scaleX = builder.Compiled("Math::Multiply");
    NodePtr getCenterX = builder.Get(centerX, function);
    NodePtr offsetX = builder.Compiled("Math::Add");
    NodePtr setXZero = builder.Set(valueX, function);
    NodePtr setYZero = builder.Set(valueY, function);
    NodePtr setIterationsZero = builder.Set(iterations, function);
    setXZero->InputValues[1] = Value(0.0);
    setYZero->InputValues[1] = Value(0.0);
    setIterationsZero->InputValues[1] = Value(0.0);

    builder.Add(graph, { begin, clear, getPixelsForClear, outerRepeat, getResolutionOuter, setCoordinateY, divideY, getResolutionY,
        normalizeY, getScaleY, scaleY, getCenterY, offsetY, innerRepeat, getResolutionInner, setCoordinateX, divideX, getResolutionX,
        normalizeX, getScaleX, scaleX, getCenterX, offsetX, setXZero, setYZero, setIterationsZero });
    builder.Link(graph, begin->Outputs[0], clear->Inputs[0]);
    builder.Link(graph, getPixelsForClear->Outputs[0], builder.Input(clear, "List"));
    builder.Link(graph, clear->Outputs[0], outerRepeat->Inputs[0]);
    builder.Link(graph, getResolutionOuter->Outputs[0], builder.Input(outerRepeat, "Count"));
    builder.Link(graph, outerRepeat->Outputs[0], setCoordinateY->Inputs[0]);
    builder.Link(graph, outerRepeat->Outputs[1], divideY->Inputs[0]);
    builder.Link(graph, getResolutionY->Outputs[0], divideY->Inputs[1]);
    builder.Link(graph, divideY->Outputs[0], normalizeY->Inputs[0]);
    builder.Link(graph, normalizeY->Outputs[0], scaleY->Inputs[0]);
    builder.Link(graph, getScaleY->Outputs[0], scaleY->Inputs[1]);
    builder.Link(graph, scaleY->Outputs[0], offsetY->Inputs[0]);
    builder.Link(graph, getCenterY->Outputs[0], offsetY->Inputs[1]);
    builder.Link(graph, offsetY->Outputs[0], setCoordinateY->Inputs[1]);
    builder.Link(graph, setCoordinateY->Outputs[0], innerRepeat->Inputs[0]);
    builder.Link(graph, getResolutionInner->Outputs[0], builder.Input(innerRepeat, "Count"));
    builder.Link(graph, innerRepeat->Outputs[0], setCoordinateX->Inputs[0]);
    builder.Link(graph, innerRepeat->Outputs[1], divideX->Inputs[0]);
    builder.Link(graph, getResolutionX->Outputs[0], divideX->Inputs[1]);
    builder.Link(graph, divideX->Outputs[0], normalizeX->Inputs[0]);
    builder.Link(graph, normalizeX->Outputs[0], scaleX->Inputs[0]);
    builder.Link(graph, getScaleX->Outputs[0], scaleX->Inputs[1]);
    builder.Link(graph, scaleX->Outputs[0], offsetX->Inputs[0]);
    builder.Link(graph, getCenterX->Outputs[0], offsetX->Inputs[1]);
    builder.Link(graph, offsetX->Outputs[0], setCoordinateX->Inputs[1]);
    builder.Link(graph, setCoordinateX->Outputs[0], setXZero->Inputs[0]);
    builder.Link(graph, setXZero->Outputs[0], setYZero->Inputs[0]);
    builder.Link(graph, setYZero->Outputs[0], setIterationsZero->Inputs[0]);

    NodePtr whileNode = builder.Compiled("Flow::While");
    NodePtr getXForMagnitude = builder.Get(valueX, function);
    NodePtr squareX = builder.Compiled("Math::Multiply");
    NodePtr getYForMagnitude = builder.Get(valueY, function);
    NodePtr squareY = builder.Compiled("Math::Multiply");
    NodePtr magnitude = builder.Compiled("Math::Add");
    NodePtr magnitudeLimit = Binary(builder, "Math::Less Or Equal", 4.0);
    NodePtr getIterationsForLimit = builder.Get(iterations, function);
    NodePtr iterationLimit = builder.Compiled("Math::Less Than");
    NodePtr getMaxIterations = builder.Get(maxIterations, function);
    NodePtr condition = builder.Compiled("Logic::And");
    builder.Add(graph, { whileNode, getXForMagnitude, squareX, getYForMagnitude, squareY, magnitude, magnitudeLimit,
        getIterationsForLimit, iterationLimit, getMaxIterations, condition });
    builder.Link(graph, setIterationsZero->Outputs[0], whileNode->Inputs[0]);
    builder.Link(graph, getXForMagnitude->Outputs[0], squareX->Inputs[0]);
    builder.Link(graph, getXForMagnitude->Outputs[0], squareX->Inputs[1]);
    builder.Link(graph, getYForMagnitude->Outputs[0], squareY->Inputs[0]);
    builder.Link(graph, getYForMagnitude->Outputs[0], squareY->Inputs[1]);
    builder.Link(graph, squareX->Outputs[0], magnitude->Inputs[0]);
    builder.Link(graph, squareY->Outputs[0], magnitude->Inputs[1]);
    builder.Link(graph, magnitude->Outputs[0], magnitudeLimit->Inputs[0]);
    builder.Link(graph, getIterationsForLimit->Outputs[0], iterationLimit->Inputs[0]);
    builder.Link(graph, getMaxIterations->Outputs[0], iterationLimit->Inputs[1]);
    builder.Link(graph, magnitudeLimit->Outputs[0], condition->Inputs[0]);
    builder.Link(graph, iterationLimit->Outputs[0], condition->Inputs[1]);
    builder.Link(graph, condition->Outputs[0], whileNode->Inputs[1]);

    NodePtr setTemporaryX = builder.Set(temporaryX, function);
    NodePtr getXForTemp = builder.Get(valueX, function);
    NodePtr tempSquareX = builder.Compiled("Math::Multiply");
    NodePtr getYForTemp = builder.Get(valueY, function);
    NodePtr tempSquareY = builder.Compiled("Math::Multiply");
    NodePtr subtractSquares = builder.Compiled("Math::Subtract");
    NodePtr getCoordinateX = builder.Get(coordinateX, function);
    NodePtr addCoordinateX = builder.Compiled("Math::Add");
    NodePtr setNextY = builder.Set(valueY, function);
    NodePtr getXForY = builder.Get(valueX, function);
    NodePtr getYForY = builder.Get(valueY, function);
    NodePtr multiplyXY = builder.Compiled("Math::Multiply");
    NodePtr doubleXY = Binary(builder, "Math::Multiply", 2.0);
    NodePtr getCoordinateY = builder.Get(coordinateY, function);
    NodePtr addCoordinateY = builder.Compiled("Math::Add");
    NodePtr setNextX = builder.Set(valueX, function);
    NodePtr getTemporaryX = builder.Get(temporaryX, function);
    NodePtr setNextIterations = builder.Set(iterations, function);
    NodePtr getIterationsForIncrement = builder.Get(iterations, function);
    NodePtr incrementIterations = Binary(builder, "Math::Add", 1.0);
    builder.Add(graph, { setTemporaryX, getXForTemp, tempSquareX, getYForTemp, tempSquareY, subtractSquares, getCoordinateX,
        addCoordinateX, setNextY, getXForY, getYForY, multiplyXY, doubleXY, getCoordinateY, addCoordinateY, setNextX,
        getTemporaryX, setNextIterations, getIterationsForIncrement, incrementIterations });
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

    NodePtr pushPixel = builder.Native("List::Push");
    NodePtr getPixelsForPush = builder.Get(pixels, function);
    NodePtr getPixelIteration = builder.Get(iterations, function);
    NodePtr setRevision = builder.Set(revision, function);
    NodePtr getRevision = builder.Get(revision, function);
    NodePtr incrementRevision = Binary(builder, "Math::Add", 1.0);
    builder.Add(graph, { pushPixel, getPixelsForPush, getPixelIteration, setRevision, getRevision, incrementRevision });
    builder.Link(graph, whileNode->Outputs[1], pushPixel->Inputs[0]);
    builder.Link(graph, getPixelsForPush->Outputs[0], builder.Input(pushPixel, "List"));
    builder.Link(graph, getPixelIteration->Outputs[0], builder.Input(pushPixel, "Value"));
    builder.Link(graph, outerRepeat->Outputs[2], setRevision->Inputs[0]);
    builder.Link(graph, getRevision->Outputs[0], incrementRevision->Inputs[0]);
    builder.Link(graph, incrementRevision->Outputs[0], setRevision->Inputs[1]);
}

NodePtr AddSlider(Builder& builder, Graph& graph, const char* label, double minimum, double maximum,
                  const ScriptPropertyPtr& property, const Pin& previousFlow, bool wholeNumber = false)
{
    NodePtr slider = builder.Native(wholeNumber ? "UI::Slider Integer" : "UI::Slider Number");
    NodePtr getValue = builder.Get(property);
    NodePtr setValue = builder.Set(property);
    builder.Default(slider, "Label", Value(copyString(label, static_cast<int>(std::char_traits<char>::length(label)))));
    builder.Default(slider, "Minimum", Value(minimum));
    builder.Default(slider, "Maximum", Value(maximum));
    builder.Add(graph, { slider, getValue, setValue });
    builder.Link(graph, previousFlow, slider->Inputs[0]);
    builder.Link(graph, getValue->Outputs[0], builder.Input(slider, "Value"));
    builder.Link(graph, builder.Output(slider, "Value"), setValue->Inputs[1]);
    builder.Link(graph, slider->Outputs[0], setValue->Inputs[0]);
    return setValue;
}

Script MakeApplication(const NodeRegistry& registry)
{
    Builder builder(registry);
    ScriptPropertyPtr centerX = builder.Number("CenterX", -0.5);
    ScriptPropertyPtr centerY = builder.Number("CenterY", 0.0);
    ScriptPropertyPtr scale = builder.Number("Scale", 3.0);
    ScriptPropertyPtr maxIterations = builder.Number("MaxIterations", 60.0);
    ScriptPropertyPtr resolution = builder.Number("Resolution", 180.0);
    ScriptPropertyPtr pixels = builder.NumberList("Pixels");
    ScriptPropertyPtr revision = builder.Number("ImageRevision", 0.0);

    ScriptFunctionPtr regenerate = std::make_shared<ScriptFunction>(builder.ids.GetNextId(), "Regenerate");
    regenerate->functionDef->description = "Rebuilds the Mandelbrot image values";
    builder.script.functions.push_back(regenerate);
    ScriptPropertyPtr coordinateX = builder.LocalNumber(regenerate, "CoordinateX", 0.0);
    ScriptPropertyPtr coordinateY = builder.LocalNumber(regenerate, "CoordinateY", 0.0);
    ScriptPropertyPtr valueX = builder.LocalNumber(regenerate, "ValueX", 0.0);
    ScriptPropertyPtr valueY = builder.LocalNumber(regenerate, "ValueY", 0.0);
    ScriptPropertyPtr temporaryX = builder.LocalNumber(regenerate, "TemporaryX", 0.0);
    ScriptPropertyPtr iterations = builder.LocalNumber(regenerate, "Iterations", 0.0);
    BuildRegenerate(builder, regenerate, centerX, centerY, scale, maxIterations, resolution, pixels, revision,
        coordinateX, coordinateY, valueX, valueY, temporaryX, iterations);

    ScriptFunctionPtr update = std::make_shared<ScriptFunction>(builder.ids.GetNextId(), "UpdateMandelbrot");
    update->functionDef->description = "Draws one application frame";
    update->functionDef->inputs.push_back({ "Delta Time", Value(0.0), builder.ids.GetNextId(), "Seconds since the previous frame" });
    builder.script.functions.push_back(update);

    Graph& mainGraph = builder.script.main->Graph;
    NodePtr mainBegin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr initialGenerate = regenerate->functionDef->MakeNode(builder.ids, regenerate->ID);
    NodePtr startUi = builder.Native("UI::Start");
    NodePtr getUpdate = BuildGetFunctionNode(builder.ids, update->functionDef, update->ID);
    builder.Add(mainGraph, { mainBegin, initialGenerate, startUi, getUpdate });
    builder.Link(mainGraph, mainBegin->Outputs[0], initialGenerate->Inputs[0]);
    builder.Link(mainGraph, initialGenerate->Outputs[0], startUi->Inputs[0]);
    builder.Link(mainGraph, getUpdate->Outputs[0], builder.Input(startUi, "Update"));

    Graph& frameGraph = update->Graph;
    NodePtr frameBegin = BuildBeginNode(builder.ids, update);
    NodePtr title = builder.Native("UI::Text");
    builder.Default(title, "Text", Value(copyString("Mandelbrot explorer", 19)));
    builder.Add(frameGraph, { frameBegin, title });
    builder.Link(frameGraph, frameBegin->Outputs[0], title->Inputs[0]);
    NodePtr centerXSlider = AddSlider(builder, frameGraph, "Center X", -2.5, 1.0, centerX, title->Outputs[0]);
    NodePtr centerYSlider = AddSlider(builder, frameGraph, "Center Y", -1.5, 1.5, centerY, centerXSlider->Outputs[0]);
    NodePtr scaleSlider = AddSlider(builder, frameGraph, "Scale", 0.1, 4.0, scale, centerYSlider->Outputs[0]);
    NodePtr iterationSlider = AddSlider(builder, frameGraph, "Iterations", 10.0, 250.0, maxIterations, scaleSlider->Outputs[0], true);
    NodePtr resolutionSlider = AddSlider(builder, frameGraph, "Resolution", 64.0, 420.0, resolution, iterationSlider->Outputs[0], true);

    NodePtr hint = builder.Native("UI::Text");
    builder.Default(hint, "Text", Value(copyString("Adjust parameters, then regenerate.", 35)));
    NodePtr separator = builder.Native("UI::Separator");
    NodePtr image = builder.Native("UI::Image");
    builder.Default(image, "Id", Value(copyString("Mandelbrot", 10)));
    NodePtr getPixels = builder.Get(pixels);
    NodePtr getResolutionWidth = builder.Get(resolution);
    NodePtr getResolutionHeight = builder.Get(resolution);
    NodePtr getMaximum = builder.Get(maxIterations);
    NodePtr getRevision = builder.Get(revision);
    NodePtr button = builder.Native("UI::Button");
    builder.Default(button, "Label", Value(copyString("Regenerate", 10)));
    NodePtr branch = builder.Compiled("Flow::Branch");
    NodePtr regenerateCall = regenerate->functionDef->MakeNode(builder.ids, regenerate->ID);
    builder.Add(frameGraph, { hint, separator, image, getPixels, getResolutionWidth, getResolutionHeight, getMaximum, getRevision,
        button, branch, regenerateCall });
    builder.Link(frameGraph, resolutionSlider->Outputs[0], hint->Inputs[0]);
    builder.Link(frameGraph, hint->Outputs[0], separator->Inputs[0]);
    builder.Link(frameGraph, separator->Outputs[0], image->Inputs[0]);
    builder.Link(frameGraph, getPixels->Outputs[0], builder.Input(image, "Values"));
    builder.Link(frameGraph, getResolutionWidth->Outputs[0], builder.Input(image, "Width"));
    builder.Link(frameGraph, getResolutionHeight->Outputs[0], builder.Input(image, "Height"));
    builder.Link(frameGraph, getMaximum->Outputs[0], builder.Input(image, "Maximum"));
    builder.Link(frameGraph, getRevision->Outputs[0], builder.Input(image, "Revision"));
    builder.Link(frameGraph, image->Outputs[0], button->Inputs[0]);
    builder.Link(frameGraph, button->Outputs[0], branch->Inputs[0]);
    builder.Link(frameGraph, builder.Output(button, "Pressed"), branch->Inputs[1]);
    builder.Link(frameGraph, branch->Outputs[0], regenerateCall->Inputs[0]);
    return std::move(builder.script);
}

void ValidateApplicationFrame(VM& vm, VisualApplicationContext& applicationContext, bool& textureCreated, bool& containsColor)
{
    ImGuiContext* imguiContext = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(500.0f, 500.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

    textureCreated = false;
    containsColor = false;
    ImGui::NewFrame();
    applicationContext.BeginFrame();
    const InterpretResult result = ScriptRuntime::Call(vm, applicationContext.GetUpdateFunction(), { Value(1.0 / 60.0) });
    applicationContext.EndFrame();
    ImGui::EndFrame();
    ImGui::DestroyContext(imguiContext);

    if (result != InterpretResult::INTERPRET_OK)
        throw std::runtime_error("The Mandelbrot UI update callback failed in an ImGui frame.");
    if (!textureCreated || !containsColor)
        throw std::runtime_error("The Mandelbrot image widget did not create a color texture.");
}
}

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path output = argc >= 2 ? argv[1] : std::filesystem::path("examples/blueprints-example/apps/mandelbrot.vlox");
        VM& vm = VM::getInstance();
        NodeRegistry registry;
        RegisterStandardLibrary(registry);
        RegisterVisualApplicationLibrary(registry);
        registry.RegisterNatives(vm);

        Script script = MakeApplication(registry);
        const ValidationReport validation = ScriptValidator::Validate(script);
        for (const ValidationDiagnostic& diagnostic : validation.diagnostics)
            std::clog << FormatDiagnostic(diagnostic) << '\n';
        if (validation.HasErrors())
            throw std::runtime_error("Generated Mandelbrot graph failed validation.");

        const SerializationResult saved = ScriptSerializer::Save(script, output.string());
        if (!saved)
            throw std::runtime_error(saved.error);

        const ScriptCompileResult compiled = ScriptRuntime::Compile(vm, script);
        if (!compiled)
            throw std::runtime_error("Generated Mandelbrot graph failed compilation.");

        int expectedDimension = 180;
        bool textureCreated = false;
        bool containsColor = false;
        VisualApplicationContext applicationContext({
            [&](const void* data, int width, int height)
            {
                textureCreated = width == expectedDimension && height == expectedDimension;
                const uint8_t* rgba = static_cast<const uint8_t*>(data);
                for (size_t index = 0; index < static_cast<size_t>(width) * static_cast<size_t>(height); ++index)
                    containsColor = containsColor || rgba[index * 4] != 0 || rgba[index * 4 + 1] != 0 || rgba[index * 4 + 2] != 0;
                return reinterpret_cast<ImTextureID>(1);
            },
            [](ImTextureID) {}
        });
        if (ScriptRuntime::Execute(vm, compiled.function) != InterpretResult::INTERPRET_OK)
            throw std::runtime_error("Generated Mandelbrot graph failed initialization.");
        if (!applicationContext.HasUpdateFunction())
            throw std::runtime_error("Generated Mandelbrot graph did not start its visual application.");

        Value pixelValue;
        if (!vm.globalTable().get(copyString("Pixels", 6), &pixelValue) || !isList(pixelValue))
            throw std::runtime_error("Generated Mandelbrot graph did not produce its pixel list.");
        const size_t initialPixelCount = asList(pixelValue)->items.size();
        if (initialPixelCount != 180 * 180)
            throw std::runtime_error("The default Mandelbrot resolution produced the wrong pixel count.");
        ValidateApplicationFrame(vm, applicationContext, textureCreated, containsColor);

        vm.globalTable().set(copyString("Resolution", 10), Value(127.0));
        if (ScriptRuntime::CallGlobal(vm, "Regenerate") != InterpretResult::INTERPRET_OK)
            throw std::runtime_error("Regenerating Mandelbrot at a different resolution failed.");
        if (asList(pixelValue)->items.size() != 127 * 127)
            throw std::runtime_error("Changing Mandelbrot resolution produced the wrong pixel count.");
        expectedDimension = 127;
        ValidateApplicationFrame(vm, applicationContext, textureCreated, containsColor);

        std::cout << "Generated " << output.string() << " (pixels=" << initialPixelCount
                  << ", resized_pixels=" << asList(pixelValue)->items.size() << ")\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Generation error: " << error.what() << '\n';
        return 1;
    }
}
