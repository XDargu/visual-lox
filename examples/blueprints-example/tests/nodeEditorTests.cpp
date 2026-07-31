#include "nodeEditorTests.h"

#include "testFramework.h"

#include "imgui.h"
#include "imgui_node_editor.h"

namespace
{
namespace ed = ax::NodeEditor;

struct EditorContext
{
    EditorContext()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;

        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        ed::Config config;
        config.SettingsFile = nullptr;
        editor = ed::CreateEditor(&config);
        ed::SetCurrentEditor(editor);
    }

    ~EditorContext()
    {
        ed::DestroyEditor(editor);
        ImGui::DestroyContext();
    }

    ed::EditorContext* editor = nullptr;
};

void GroupResizeUsesMouseDownEdge()
{
    EditorContext context;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 nodeScreenPosition;
    ImVec2 nodeSize;

    const auto drawFrame = [&](ImVec2 mousePosition, bool mouseDown)
    {
        io.MousePos = mousePosition;
        io.MouseDown[0] = mouseDown;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowFocus();
        ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ed::Begin("Editor");
        ed::BeginNode(1);
        ImGui::TextUnformatted("Comment Box");
        ed::Group(ImVec2(300.0f, 200.0f));
        ed::EndNode();
        nodeScreenPosition = ed::CanvasToScreen(ed::GetNodePosition(1));
        nodeSize = ed::GetNodeSize(1);
        ed::End();
        ImGui::End();
        ImGui::Render();
    };

    const ImVec2 outsideCanvas(-100.0f, -100.0f);
    drawFrame(outsideCanvas, false);
    ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
    drawFrame(outsideCanvas, false);

    const ImVec2 initialSize = nodeSize;
    const ImVec2 dragStart(nodeScreenPosition.x + initialSize.x - 5.0f, nodeScreenPosition.y + initialSize.y - 5.0f);
    const ImVec2 dragEnd(dragStart.x + 48.0f, dragStart.y + 32.0f);
    drawFrame(dragStart, false);
    drawFrame(dragStart, true);
    drawFrame(dragEnd, true);
    drawFrame(dragEnd, true);
    drawFrame(dragEnd, false);

    Tests::Require(nodeSize.x > initialSize.x && nodeSize.y > initialSize.y,
                   "Dragging a group corner did not resize the comment box.");
}
}

void AddNodeEditorTests(Tests::Runner& runner)
{
    runner.Group("Node editor interactions", [&]()
    {
        runner.Test("comment box corners remain resizable", GroupResizeUsesMouseDownEdge);
    });
}
