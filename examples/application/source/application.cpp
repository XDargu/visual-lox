# include "application.h"
# include "setup.h"
# include "platform.h"
# include "renderer.h"
# include "IconsFontAwesome6.h"

extern "C" {
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"
}


Application::Application(const char* name)
    : Application(name, 0, nullptr)
{
}

Application::Application(const char* name, int argc, char** argv)
    : m_Name(name)
    , m_Platform(CreatePlatform(*this))
    , m_Renderer(CreateRenderer())
{
    for (int i = 1; i < argc; ++i)
        if (argv && argv[i])
            m_Arguments.emplace_back(argv[i]);
    m_Platform->ApplicationStart(argc, argv);
}

Application::~Application()
{
    m_Renderer->Destroy();

    m_Platform->ApplicationStop();

    if (m_Context)
    {
        ImGui::DestroyContext(m_Context);
        m_Context= nullptr;
    }
}

const std::vector<std::string>& Application::GetArguments() const
{
    return m_Arguments;
}

bool Application::Create(int width /*= -1*/, int height /*= -1*/, bool startMaximized /*= false*/)
{
    m_Context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_Context);
    m_StartMaximized = startMaximized;

    if (!m_Platform->OpenMainWindow(m_Name.c_str(), width, height))
        return false;

    if (!m_Renderer->Create(*m_Platform))
        return false;

    m_IniFilename = m_Name + ".ini";

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.IniFilename = m_IniFilename.c_str();
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();

    RecreateFontAtlas();

    m_Platform->AcknowledgeWindowScaleChanged();
    m_Platform->AcknowledgeFramebufferScaleChanged();

    OnStart();

    Frame();

    return true;
}

int Application::Run()
{
    m_Platform->ShowMainWindow(m_StartMaximized);

    while (m_Platform->ProcessMainWindowEvents())
    {
        if (!m_Platform->IsMainWindowVisible())
            continue;

        Frame();
    }

    OnStop();

    return 0;
}

void Application::RecreateFontAtlas()
{
    ImGuiIO& io = ImGui::GetIO();

    IM_DELETE(io.Fonts);

    io.Fonts = IM_NEW(ImFontAtlas);

    ImFontConfig config;
    config.OversampleH = 4;
    config.OversampleV = 4;
    config.PixelSnapH = false;

    auto mergeFontAwesome = [&io](float baseFontSize)
    {
        static const ImWchar iconRanges[] = {
            ICON_MIN_FA,
            ICON_MAX_16_FA,
            0
        };

        const float iconFontSize = baseFontSize * 2.0f / 3.0f;

        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.PixelSnapH = true;
        iconConfig.GlyphMinAdvanceX = iconFontSize;

        auto* mergedFont = io.Fonts->AddFontFromFileTTF(
            "data/" FONT_ICON_FILE_NAME_FAS,
            iconFontSize,
            &iconConfig,
            iconRanges);

        IM_ASSERT(mergedFont != nullptr);
    };

    auto loadInterfaceFont = [&io](float size, ImFontConfig* fontConfig, bool semibold = false)
    {
        ImFont* font = nullptr;
#ifdef _WIN32
        font = io.Fonts->AddFontFromFileTTF(
            semibold ? "C:/Windows/Fonts/seguisb.ttf" : "C:/Windows/Fonts/segoeui.ttf",
            size, fontConfig);
#else
        (void)semibold;
#endif
        if (!font)
            font = io.Fonts->AddFontFromFileTTF("data/Play-Regular.ttf", size, fontConfig);
        return font;
    };

    m_DefaultFont = loadInterfaceFont(16.0f, &config);
    mergeFontAwesome(16.0f);

    // Keep the entire editor on one restrained interface family.  The larger
    // size and weight communicate hierarchy without mixing display faces.
    ImFontConfig headerConfig = config;
    headerConfig.RasterizerMultiply = 1.15f;
    m_HeaderFont = loadInterfaceFont(18.0f, &headerConfig, true);
    mergeFontAwesome(18.0f);

    m_LargeNodeFont = loadInterfaceFont(32.0f, &config);
    mergeFontAwesome(32.0f);

    // Consolas is available on the supported Windows editor target.  Retain a
    // bundled-font fallback so a GLFW build remains usable elsewhere.
    m_MonoFont = nullptr;
#ifdef _WIN32
    m_MonoFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 15.0f, &config);
#endif
    if (!m_MonoFont)
        m_MonoFont = io.Fonts->AddFontFromFileTTF("data/Play-Regular.ttf", 15.0f, &config);

    io.Fonts->Build();
}

void Application::Frame()
{
    auto& io = ImGui::GetIO();

    if (m_Platform->HasWindowScaleChanged())
        m_Platform->AcknowledgeWindowScaleChanged();

    if (m_Platform->HasFramebufferScaleChanged())
    {
        RecreateFontAtlas();
        m_Platform->AcknowledgeFramebufferScaleChanged();
    }

    const float windowScale      = m_Platform->GetWindowScale();
    const float framebufferScale = m_Platform->GetFramebufferScale();

    if (io.WantSetMousePos)
    {
        io.MousePos.x *= windowScale;
        io.MousePos.y *= windowScale;
    }

    m_Platform->NewFrame();

    // Don't touch "uninitialized" mouse position
    if (io.MousePos.x > -FLT_MAX && io.MousePos.y > -FLT_MAX)
    {
        io.MousePos.x    /= windowScale;
        io.MousePos.y    /= windowScale;
    }
    io.DisplaySize.x /= windowScale;
    io.DisplaySize.y /= windowScale;

    io.DisplayFramebufferScale.x = framebufferScale;
    io.DisplayFramebufferScale.y = framebufferScale;

    m_Renderer->NewFrame();

    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const auto windowBorderSize = ImGui::GetStyle().WindowBorderSize;
    const auto windowRounding   = ImGui::GetStyle().WindowRounding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("Content", nullptr, GetWindowFlags());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, windowBorderSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, windowRounding);

    OnFrame(io.DeltaTime);

    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::PopStyleVar(2);

    // Rendering
    m_Renderer->Clear(ImColor(32, 32, 32, 255));
    ImGui::Render();
    m_Renderer->RenderDrawData(ImGui::GetDrawData());

    m_Platform->FinishFrame();
}

void Application::SetTitle(const char* title)
{
    m_Platform->SetMainWindowTitle(title);
}

bool Application::Close()
{
    return m_Platform->CloseMainWindow();
}

void Application::Quit()
{
    m_Platform->Quit();
}

const std::string& Application::GetName() const
{
    return m_Name;
}

ImFont* Application::DefaultFont() const
{
    return m_DefaultFont;
}

ImFont* Application::HeaderFont() const
{
    return m_HeaderFont;
}

ImFont* Application::LargeNodeFont() const
{
    return m_LargeNodeFont;
}

ImFont* Application::MonoFont() const
{
    return m_MonoFont;
}


ImTextureID Application::LoadTexture(const char* path)
{
    int width = 0, height = 0, component = 0;
    if (auto data = stbi_load(path, &width, &height, &component, 4))
    {
        auto texture = CreateTexture(data, width, height);
        stbi_image_free(data);
        return texture;
    }
    else
        return nullptr;
}

ImTextureID Application::CreateTexture(const void* data, int width, int height)
{
    return m_Renderer->CreateTexture(data, width, height);
}

void Application::DestroyTexture(ImTextureID texture)
{
    m_Renderer->DestroyTexture(texture);
}

int Application::GetTextureWidth(ImTextureID texture)
{
    return m_Renderer->GetTextureWidth(texture);
}

int Application::GetTextureHeight(ImTextureID texture)
{
    return m_Renderer->GetTextureHeight(texture);
}

ImGuiWindowFlags Application::GetWindowFlags() const
{
    return
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
}
