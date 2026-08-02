#include "visualApplication.h"

#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../runtime/scriptRuntime.h"
#include "../runtime/standardLibrary.h"
#include "../script/scriptSerializer.h"

#include <application.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
class VloxApplication : public Application
{
public:
    VloxApplication(int argc, char** argv)
        : Application("Vlox Application", argc, argv), vm(VM::getInstance())
    {}

    void OnStart() override
    {
        context = std::make_unique<VisualApplicationContext>(VisualApplicationTextureCallbacks{
            [this](const void* data, int width, int height) { return CreateTexture(data, width, height); },
            [this](ImTextureID texture) { DestroyTexture(texture); }
        });

        RegisterStandardLibrary(registry);
        RegisterVisualApplicationLibrary(registry);
        registry.RegisterNatives(vm);
        vm.setExternalMarkingFunc([this]()
        {
            MarkNodeRegistryRoots(registry, vm);
            ScriptUtils::MarkScriptRoots(script);
            if (context)
                context->MarkRoots(vm);
        });

        if (GetArguments().empty())
        {
            Fail("Usage: vlox-app <script.vlox> [arguments...]");
            return;
        }

        const std::string& scriptPath = GetArguments()[0];
        const SerializationResult loaded = ScriptSerializer::Load(scriptPath, registry, script, ids);
        if (!loaded)
        {
            Fail("Could not load '" + scriptPath + "': " + loaded.error);
            return;
        }

        ScriptCompileOptions options;
        options.programArguments.assign(GetArguments().begin() + 1, GetArguments().end());
        const ScriptCompileResult compiled = ScriptRuntime::Compile(vm, script, options);
        for (const ValidationDiagnostic& diagnostic : compiled.validation.diagnostics)
            std::cerr << FormatDiagnostic(diagnostic) << '\n';
        if (!compiled)
        {
            Fail("Visual Lox application compilation failed.");
            return;
        }

        if (ScriptRuntime::Execute(vm, compiled.function) != InterpretResult::INTERPRET_OK)
        {
            Fail("Visual Lox application initialization failed.");
            return;
        }

        if (!context->HasUpdateFunction())
        {
            Fail("The application must call UI::Start with an Update function.");
            return;
        }
    }

    void OnStop() override
    {
        ClearStandardLibraryTimers(vm);
        context.reset();
        vm.setExternalMarkingFunc([]() {});
    }

    void OnFrame(float deltaTime) override
    {
        if (!error.empty())
        {
            ImGui::TextWrapped("%s", error.c_str());
            return;
        }

        context->BeginFrame();
        InterpretResult result = PumpStandardLibraryTimers(vm) ? InterpretResult::INTERPRET_OK : InterpretResult::INTERPRET_RUNTIME_ERROR;
        if (result == InterpretResult::INTERPRET_OK)
            result = ScriptRuntime::Call(vm, context->GetUpdateFunction(), { Value(static_cast<double>(deltaTime)) });
        if (result == InterpretResult::INTERPRET_OK && !PumpStandardLibraryTimers(vm))
            result = InterpretResult::INTERPRET_RUNTIME_ERROR;
        context->EndFrame();
        if (result != InterpretResult::INTERPRET_OK)
            Fail("The UI update function stopped with a runtime error.");
    }

private:
    void Fail(std::string message)
    {
        error = std::move(message);
        std::cerr << error << '\n';
    }

    VM& vm;
    NodeRegistry registry;
    Script script;
    IDGenerator ids;
    std::unique_ptr<VisualApplicationContext> context;
    std::string error;
};
}

int Main(int argc, char** argv)
{
    VloxApplication application(argc, argv);
    if (!application.Create(900, 760))
        return 1;
    return application.Run();
}
