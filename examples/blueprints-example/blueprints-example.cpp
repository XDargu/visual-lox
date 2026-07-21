#define IMGUI_DEFINE_MATH_OPERATORS

#include "editor.h"
#include "script/scriptSerializerTest.h"

#include <cstring>

//extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vkey);
//extern "C" bool Debug_KeyPress(int vkey)
//{
//    static std::map<int, bool> state;
//    auto lastState = state[vkey];
//    state[vkey] = (GetAsyncKeyState(vkey) & 0x8000) != 0;
//    if (state[vkey] && !lastState)
//        return true;
//    else
//        return false;
//}

int Main(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "--serialization-test") == 0)
    {
        const std::string outputPath = argc >= 3 ? argv[2] : "serialization-roundtrip-test.vlox";
        return RunScriptSerializerRoundTripTest(outputPath);
    }

    Editor::Example exampe("VisualLox", argc, argv);

    if (exampe.Create())
        return exampe.Run();

    return 0;
}
