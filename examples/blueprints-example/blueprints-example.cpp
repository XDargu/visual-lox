#define IMGUI_DEFINE_MATH_OPERATORS

#include "editor.h"

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
    Example exampe("VisualLox", argc, argv);

    if (exampe.Create())
        return exampe.Run();

    return 0;
}