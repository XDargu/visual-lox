#include "visualApplication.h"

#include "../graphs/nodeRegistry.h"

#include <Object.h>
#include <Vm.h>
#include <VMUtils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
VisualApplicationContext* activeContext = nullptr;

bool CanDraw()
{
    return activeContext && activeContext->IsFrameActive() && ImGui::GetCurrentContext();
}

const char* StringArgument(const Value& value)
{
    return isString(value) ? asCString(value) : "";
}

double NumberArgument(const Value& value, double fallback = 0.0)
{
    return isNumber(value) ? asNumber(value) : fallback;
}

Value Package(VM* vm, std::initializer_list<Value> values)
{
    ObjList* result = newList();
    vm->push(Value(result));
    for (const Value& value : values)
        result->append(value);
    vm->pop();
    return Value(result);
}

Value UiText(int, Value* args, VM*)
{
    if (CanDraw())
        ImGui::TextUnformatted(StringArgument(args[0]));
    return Value();
}

Value UiStart(int, Value* args, VM*)
{
    if (activeContext)
        activeContext->SetUpdateFunction(args[0]);
    return Value();
}

Value UiButton(int, Value* args, VM*)
{
    return Value(CanDraw() && ImGui::Button(StringArgument(args[0])));
}

Value UiSliderNumber(int, Value* args, VM* vm)
{
    double value = NumberArgument(args[1]);
    double minimum = NumberArgument(args[2]);
    double maximum = NumberArgument(args[3]);
    if (minimum > maximum)
        std::swap(minimum, maximum);

    bool changed = false;
    if (CanDraw())
        changed = ImGui::SliderScalar(StringArgument(args[0]), ImGuiDataType_Double, &value, &minimum, &maximum, "%.3f");
    return Package(vm, { Value(value), Value(changed) });
}

int IntegerArgument(const Value& value)
{
    const double rounded = std::round(NumberArgument(value));
    return static_cast<int>(std::clamp(
        rounded,
        static_cast<double>(std::numeric_limits<int>::min()),
        static_cast<double>(std::numeric_limits<int>::max())));
}

Value UiSliderInteger(int, Value* args, VM* vm)
{
    int value = IntegerArgument(args[1]);
    int minimum = IntegerArgument(args[2]);
    int maximum = IntegerArgument(args[3]);
    if (minimum > maximum)
        std::swap(minimum, maximum);
    value = std::clamp(value, minimum, maximum);

    bool changed = false;
    if (CanDraw())
        changed = ImGui::SliderInt(StringArgument(args[0]), &value, minimum, maximum);
    return Package(vm, { Value(static_cast<double>(value)), Value(changed) });
}

Value UiInputNumber(int, Value* args, VM* vm)
{
    double value = NumberArgument(args[1]);
    const double step = std::max(0.0, NumberArgument(args[2]));
    bool changed = false;
    if (CanDraw())
        changed = ImGui::InputDouble(StringArgument(args[0]), &value, step, step * 10.0, "%.6f");
    return Package(vm, { Value(value), Value(changed) });
}

Value UiSameLine(int, Value*, VM*)
{
    if (CanDraw())
        ImGui::SameLine();
    return Value();
}

Value UiSeparator(int, Value*, VM*)
{
    if (CanDraw())
        ImGui::Separator();
    return Value();
}

Value UiImage(int, Value* args, VM*)
{
    if (activeContext && isList(args[1]))
    {
        activeContext->DrawImage(
            StringArgument(args[0]), asList(args[1]),
            static_cast<int>(NumberArgument(args[2])),
            static_cast<int>(NumberArgument(args[3])),
            NumberArgument(args[4], 1.0),
            NumberArgument(args[5]),
            static_cast<int>(NumberArgument(args[6]))
        );
    }
    return Value();
}

uint8_t ColorChannel(double value)
{
    return static_cast<uint8_t>(std::clamp(value, 0.0, 255.0));
}
}

VisualApplicationContext::VisualApplicationContext(VisualApplicationTextureCallbacks textureCallbacks)
    : callbacks(std::move(textureCallbacks))
{
    if (activeContext)
        throw std::runtime_error("Only one visual application context can be active.");
    activeContext = this;
}

VisualApplicationContext::~VisualApplicationContext()
{
    for (auto& entry : textures)
        if (entry.second.texture && callbacks.destroy)
            callbacks.destroy(entry.second.texture);
    if (activeContext == this)
        activeContext = nullptr;
}

void VisualApplicationContext::BeginFrame()
{
    frameActive = true;
}

void VisualApplicationContext::EndFrame()
{
    frameActive = false;
}

bool VisualApplicationContext::IsFrameActive() const
{
    return frameActive;
}

bool VisualApplicationContext::SetUpdateFunction(const Value& function)
{
    if (!isCallable(function) || getCallableArity(function) != 1)
        return false;

    updateFunction = function;
    return true;
}

bool VisualApplicationContext::HasUpdateFunction() const
{
    return isCallable(updateFunction) && getCallableArity(updateFunction) == 1;
}

const Value& VisualApplicationContext::GetUpdateFunction() const
{
    return updateFunction;
}

void VisualApplicationContext::MarkRoots(VM& vm)
{
    vm.markValue(updateFunction);
}

void VisualApplicationContext::DrawImage(const std::string& id, ObjList* values, int width, int height, double maximum, double revision, int scale /*= 1*/)
{
    if (!IsFrameActive() || !ImGui::GetCurrentContext() || !values || width <= 0 || height <= 0 || maximum <= 0.0 || !callbacks.create)
        return;

    CachedTexture& cached = textures[id];
    if (cached.source != values || cached.width != width || cached.height != height || cached.revision != revision)
    {
        if (cached.texture && callbacks.destroy)
            callbacks.destroy(cached.texture);

        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<uint8_t> pixels(pixelCount * 4, 0);
        const size_t available = std::min(pixelCount, values->items.size());
        for (size_t index = 0; index < available; ++index)
        {
            const double iteration = NumberArgument(values->items[index]);
            const double t = std::clamp(iteration / maximum, 0.0, 1.0);
            const bool inside = iteration >= maximum;
            pixels[index * 4 + 0] = inside ? 0 : ColorChannel(9.0 * (1.0 - t) * t * t * t * 255.0);
            pixels[index * 4 + 1] = inside ? 0 : ColorChannel(15.0 * (1.0 - t) * (1.0 - t) * t * t * 255.0);
            pixels[index * 4 + 2] = inside ? 0 : ColorChannel(8.5 * (1.0 - t) * (1.0 - t) * (1.0 - t) * t * 255.0);
            pixels[index * 4 + 3] = 255;
        }

        cached.texture = callbacks.create(pixels.data(), width, height);
        cached.source = values;
        cached.width = width;
        cached.height = height;
        cached.revision = revision;
    }

    if (!cached.texture)
        return;

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float displayWidth = std::min(static_cast<float>(width * scale), availableWidth);
    const float displayHeight = displayWidth * static_cast<float>(height * scale) / static_cast<float>(width * scale);
    ImGui::Image(cached.texture, ImVec2(displayWidth, displayHeight));
}

void RegisterVisualApplicationLibrary(NodeRegistry& registry)
{
    registry.RegisterNativeFunc("UI::Start",
        { { "Update", Value(newFunction()), -1, TypeRef::Function({ PinType::Float }, {}) } }, {}, &UiStart,
        NodeDefinitionFlags::None,
        { "Starts a visual application and calls Update once per frame",
          { "A function receiving the elapsed time in seconds" }, {} });

    registry.RegisterNativeFunc("UI::Text",
        { { "Text", Value(copyString("", 0)), -1, PinType::String } }, {}, &UiText,
        NodeDefinitionFlags::None,
        { "Draws text in the current application window", { "The text to draw" }, {} });

    registry.RegisterNativeFunc("UI::Button",
        { { "Label", Value(copyString("Button", 6)), -1, PinType::String } },
        { { "Pressed", Value(false) } }, &UiButton, NodeDefinitionFlags::None,
        { "Draws a button", { "The visible button label" }, { "True only on the frame the button is pressed" } });

    registry.RegisterNativeFunc("UI::Slider Number",
        { { "Label", Value(copyString("Value", 5)), -1, PinType::String }, { "Value", Value(0.0) },
          { "Minimum", Value(0.0) }, { "Maximum", Value(1.0) } },
        { { "Value", Value(0.0) }, { "Changed", Value(false) } }, &UiSliderNumber, NodeDefinitionFlags::None,
        { "Draws a bounded number slider", { "The visible label", "The current value", "The minimum value", "The maximum value" },
          { "The edited value", "True when the value changed this frame" } });

    registry.RegisterNativeFunc("UI::Slider Integer",
        { { "Label", Value(copyString("Value", 5)), -1, PinType::String }, { "Value", Value(0.0) },
          { "Minimum", Value(0.0) }, { "Maximum", Value(100.0) } },
        { { "Value", Value(0.0) }, { "Changed", Value(false) } }, &UiSliderInteger, NodeDefinitionFlags::None,
        { "Draws a bounded whole-number slider", { "The visible label", "The current value", "The minimum value", "The maximum value" },
          { "The edited whole-number value", "True when the value changed this frame" } });

    registry.RegisterNativeFunc("UI::Input Number",
        { { "Label", Value(copyString("Value", 5)), -1, PinType::String }, { "Value", Value(0.0) }, { "Step", Value(0.1) } },
        { { "Value", Value(0.0) }, { "Changed", Value(false) } }, &UiInputNumber, NodeDefinitionFlags::None,
        { "Draws a number input field", { "The visible label", "The current value", "The increment used by the step buttons" },
          { "The edited value", "True when the value changed this frame" } });

    registry.RegisterNativeFunc("UI::Same Line", {}, {}, &UiSameLine, NodeDefinitionFlags::None,
        { "Places the next widget on the current line", {}, {} });
    registry.RegisterNativeFunc("UI::Separator", {}, {}, &UiSeparator, NodeDefinitionFlags::None,
        { "Draws a horizontal separator", {}, {} });

    registry.RegisterNativeFunc("UI::Image",
        { { "Id", Value(copyString("Image", 5)), -1, PinType::String },
          { "Values", Value(newList()), -1, TypeRef::List(PinType::Float) },
          { "Width", Value(1.0) }, { "Height", Value(1.0) }, { "Maximum", Value(1.0) }, { "Revision", Value(0.0) }, { "Scale", Value(1.0) } },
        {}, &UiImage, NodeDefinitionFlags::None,
        { "Draws a color-mapped numeric image. A new list value or revision triggers a texture upload",
          { "A stable texture cache key", "Row-major scalar pixel values", "Pixel width", "Pixel height", "The value mapped to black",
            "Increment this after mutating an existing values list", "Scale of the image, defaults to 1"}, {}});
}
