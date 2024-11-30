# pragma once

#include "nodeRegistry.h"

#include "graph.h"
#include "graphCompiler.h"
#include "../utilities/utils.h"

#include <Natives.h>

#include <string>
#include <string_view>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

#include <locale>
#include <codecvt>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif


void NodeRegistry::RegisterDefinitions()
{
    nativeDefinitions.clear();

    RegisterNativeFunc("Math::Square",
        { { "Value", Value(0.0) } },
        { { "Result", Value(0.0) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (isNumber(args[0]))
            {
                double number = asNumber(args[0]);
                return Value(number * number);
            }

            return Value(0.0);
        },
        NodeFlags::ReadOnly | NodeFlags::CanConstFold
    );

    RegisterNativeFunc("File::FileExists",
        { { "File", Value(copyString("", 0))}},
        { { "Exists", Value(false) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (isString(args[0]))
            {
                ObjString* fileName = asString(args[0]);
                return Value(std::filesystem::exists(fileName->chars));
            }

            return Value(false);
        },
        NodeFlags::ReadOnly
    );

    RegisterNativeFunc("String::Split",
        { { "String", Value(copyString("", 0)) }, { "Separator", Value(copyString("", 0))} },
        { { "List", Value(newList()) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (isString(args[0]) && isString(args[1]))
            {
                ObjString* data = asString(args[0]);
                ObjString* separator = asString(args[1]);

                std::vector<std::string> split = Utils::split(data->chars, separator->chars);

                ObjList* list = newList();
                vm->push(Value(list));

                for (std::string& s : split)
                {
                    list->append(Value(copyString(s.c_str(), s.length())));
                }

                vm->pop();

                return Value(list);
            }

            return Value(newList());
        },
        NodeFlags::ReadOnly | NodeFlags::CanConstFold
    );

    RegisterNativeFunc("System::Clock",
        { },
        { { "Time", Value(0.0) } },
        &clock,
        NodeFlags::ReadOnly
    );

    RegisterNativeFunc("List::MakeList",
        { { "List", Value(newList()) } },
        [] (int argCount, Value* args, VM* vm)
        {
            return Value(args[0]); // Result is already a list!
        },
        NodeFlags::ReadOnly | NodeFlags::DynamicInputs | NodeFlags::CanConstFold,
        {
            1, 16, PinType::Any, Value(0.0)
        }
    );

    RegisterNativeFunc("File::ReadFile",
        { { "File", Value(copyString("", 0)) } },
        { { "Content", Value(copyString("", 0)) } },
        &readFile,
        NodeFlags::None
    );

    RegisterNativeFunc("File::WriteFile",
        { { "File", Value(copyString("", 0)) }, { "Content", Value(copyString("", 0)) } },
        { },
        &writeFile,
        NodeFlags::None
    );

    RegisterNativeFunc("List::Contains",
        { { "List", Value(newList()) }, { "Value", Value(0.0) } },
        { { "Result", Value(false) } },
        &contains,
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("String::Contains",
        { { "Text", Value(copyString("", 0)) }, { "Value", Value(copyString("", 0)) } },
        { { "Result", Value(false) } },
        & contains,
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("List::IndexOf",
        { { "List", Value(newList()) }, { "Value", Value(0.0) } },
        { { "Result", Value(0.0) } },
        &indexOf,
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("String::IndexOf",
        { { "Text", Value(copyString("", 0)) }, { "Value", Value(copyString("", 0)) } },
        { { "Result", Value(0.0) } },
        &indexOf,
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("String::ToLower",
        { { "Text", Value(copyString("", 0)) } },
        { { "Lowercase", Value(copyString("", 0)) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isString(args[0]))
                return Value(takeString("", 0));

            ObjString* text = asString(args[0]);

            std::string result = Utils::to_lower(text->chars);

            return Value(takeString(result.c_str(), result.length())); // Result is already a list!
        },
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("String::ToUpper",
        { { "Text", Value(copyString("", 0)) } },
        { { "Uppercase", Value(copyString("", 0)) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isString(args[0]))
                return Value(takeString("", 0));

            ObjString* text = asString(args[0]);

            std::string result = Utils::to_upper(text->chars);

            return Value(takeString(result.c_str(), result.length())); // Result is already a list!
        },
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("String::Substring",
        { { "Text", Value(copyString("", 0)) }, { "Start", Value(0.0) }, { "Count", Value(0.0) } },
        { { "Result", Value(copyString("", 0)) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isString(args[0]))
                return Value();

            if (!isNumber(args[1]))
                return Value();

            if (!isNumber(args[2]))
                return Value();

            ObjString* text = asString(args[0]);

            const int start = (int)asNumber(args[1]);
            int count = (int)asNumber(args[2]);

            // Bounds checking
            if (start >= text->chars.length())
                return Value(copyString("", 0));

            if (start + count > text->chars.length())
                count = text->chars.length() - start;

            std::string result = text->chars.substr(start, count);

            return Value(takeString(result.c_str(), result.length()));
        },
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("String::Find",
        { { "Text", Value(copyString("", 0)) }, { "Search", Value(copyString("", 0)) } },
        { { "Index", Value(-1.0) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isString(args[0]))
                return Value();

            if (!isString(args[1]))
                return Value();

            ObjString* str = asString(args[0]);
            ObjString* substr = asString(args[1]);

            const size_t result = str->chars.find(substr->chars);

            if (result == std::string::npos)
            {
                return Value(-1.0);
            }

            return Value((double)result);
        },
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("Functional::FindIf",
        { { "Iterable", Value() }, { "Function", Value(newFunction()) } },
        { { "Result", Value() } },
        &findIf,
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("Functional::Map",
        { { "Iterable", Value() }, { "Function", Value(newFunction()) } },
        { { "Result", Value(newList()) } },
        &map,
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("Functional::Filter",
        { { "Iterable", Value() }, { "Function", Value(newFunction()) } },
        { { "Result", Value(newList()) } },
        &filter,
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("Functional::Reduce",
        { { "Iterable", Value() }, { "Function", Value(newFunction()) }, { "Init", Value() } },
        { { "Result", Value(newList()) } },
        &reduce,
        NodeFlags::CanConstFold | NodeFlags::ReadOnly
    );

    RegisterNativeFunc("System::CopyToClipboard",
        { { "Text", Value(copyString("", 0)) } },
        { },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isString(args[0]))
                return Value();

            ObjString* str = asString(args[0]);
#ifdef _WIN32
            const char* output = str->chars.c_str();
            const size_t len = strlen(output) + 1;
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
            memcpy(GlobalLock(hMem), output, len);
            GlobalUnlock(hMem);
            OpenClipboard(0);
            EmptyClipboard();
            SetClipboardData(CF_TEXT, hMem);
            CloseClipboard();
#endif
            return Value();
        },
        NodeFlags::None
    );

    RegisterNativeFunc("System::RunCommand",
        { { "Command", Value(copyString("", 0)) } },
        { },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isString(args[0]))
                return Value();

            ObjString* str = asString(args[0]);

#ifdef _WIN32
            system(("start " + str->chars).c_str());
#endif

            return Value();
        },
        NodeFlags::None
    );

    RegisterNativeFunc("System::Sleep",
        { { "Seconds", Value(0.0) } },
        { },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isNumber(args[0]))
                return Value();

            const double seconds = asNumber(args[0]);

            std::this_thread::sleep_for(std::chrono::milliseconds((int)(seconds * 1000.0)));

            return Value();
        },
        NodeFlags::None
    );

    RegisterNativeFunc("System::SetCursorPos",
        { { "X", Value(0.0) }, { "Y", Value(0.0) } },
        { },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isNumber(args[0]) || !isNumber(args[1]))
                return Value();

            const double x = asNumber(args[0]);
            const double y = asNumber(args[1]);

#ifdef _WIN32
            SetCursorPos((int)x, (int)y);
#endif
            return Value();
    
        },
        NodeFlags::None
    );

    RegisterNativeFunc("System::ClickMouse",
        { { "X", Value(0.0) }, { "Y", Value(0.0) } },
        { },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isNumber(args[0]) || !isNumber(args[1]))
                return Value();

            const double x = asNumber(args[0]);
            const double y = asNumber(args[1]);

    #ifdef _WIN32

            const double XSCALEFACTOR = 65535 / (GetSystemMetrics(SM_CXSCREEN) - 1);
            const double YSCALEFACTOR = 65535 / (GetSystemMetrics(SM_CYSCREEN) - 1);

            POINT cursorPos;
            GetCursorPos(&cursorPos);

            double cx = cursorPos.x * XSCALEFACTOR;
            double cy = cursorPos.y * YSCALEFACTOR;

            double nx = x * XSCALEFACTOR;
            double ny = y * YSCALEFACTOR;

            INPUT Input = { 0 };
            Input.type = INPUT_MOUSE;

            Input.mi.dx = (LONG)nx;
            Input.mi.dy = (LONG)ny;

            Input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;

            SendInput(1, &Input, sizeof(INPUT));

            Input.mi.dx = (LONG)cx;
            Input.mi.dy = (LONG)cy;

            Input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

            SendInput(1, &Input, sizeof(INPUT));
    #endif
            return Value();

        },
        NodeFlags::None
    );

    RegisterNativeFunc("System::DragMouse",
        { { "SourceX", Value(0.0) }, { "SourceY", Value(0.0) }, { "TargetX", Value(0.0) }, { "TargetY", Value(0.0) } },
        { },
        [](int argCount, Value* args, VM* vm)
    {
        if (!isNumber(args[0]) || !isNumber(args[1]) || !isNumber(args[2]) || !isNumber(args[3]))
            return Value();

        const double sx = asNumber(args[0]);
        const double sy = asNumber(args[1]);

        const double tx = asNumber(args[2]);
        const double ty = asNumber(args[3]);

#ifdef _WIN32

        const double XSCALEFACTOR = 65535 / (GetSystemMetrics(SM_CXSCREEN) - 1);
        const double YSCALEFACTOR = 65535 / (GetSystemMetrics(SM_CYSCREEN) - 1);

        POINT cursorPos;
        GetCursorPos(&cursorPos);

        double cx = cursorPos.x * XSCALEFACTOR;
        double cy = cursorPos.y * YSCALEFACTOR;

        INPUT Input = { 0 };
        Input.type = INPUT_MOUSE;

        Input.mi.dx = (LONG)(sx * XSCALEFACTOR);
        Input.mi.dy = (LONG)(sy * XSCALEFACTOR);

        Input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

        SendInput(1, &Input, sizeof(INPUT));

        Input.mi.dx = (LONG)(sx * XSCALEFACTOR);
        Input.mi.dy = (LONG)(sy * XSCALEFACTOR);

        Input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTDOWN;

        SendInput(1, &Input, sizeof(INPUT));

        Input.mi.dx = (LONG)(tx * XSCALEFACTOR);
        Input.mi.dy = (LONG)(ty * XSCALEFACTOR);

        Input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

        SendInput(1, &Input, sizeof(INPUT));

        Input.mi.dx = (LONG)(tx * XSCALEFACTOR);
        Input.mi.dy = (LONG)(ty * XSCALEFACTOR);

        Input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP;

        SendInput(1, &Input, sizeof(INPUT));


        // Restore mouse pos
        Input.mi.dx = (LONG)cx;
        Input.mi.dy = (LONG)cy;

        Input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP;

        SendInput(1, &Input, sizeof(INPUT));
#endif
        return Value();

    },
        NodeFlags::None
    );


    RegisterNativeFunc("System::PressKey",
        { { "Key", Value(takeString("", 0))} },
        { },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isString(args[0]))
                return Value();

            ObjString* str = asString(args[0]);

            if (str->chars.size() != 1)
                return Value();

            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            std::wstring wide = converter.from_bytes(str->chars);

    #ifdef _WIN32

            INPUT inputs[2] = {};

            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wScan = wide[0];
            inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wScan = wide[0];
            inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

            SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
    #endif
            return Value();

        },
        NodeFlags::None
    );
}

void NodeRegistry::RegisterNativeFunc(const char* name, std::vector<BasicFunctionDef::Input>&& inputs, std::vector<BasicFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags)
{
    BasicFunctionDefPtr nativeFunc  = std::make_shared<BasicFunctionDef>();
    nativeFunc->name = name;

    nativeFunc->inputs = inputs;
    nativeFunc->outputs = outputs;
    nativeFunc->flags = flags;

    nativeDefinitions.push_back({ nativeFunc, fun });
}

void NodeRegistry::RegisterNativeFunc(const char* name, std::vector<BasicFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags, BasicFunctionDef::DynamicInputProps&& dynamicProps)
{
    BasicFunctionDefPtr nativeFunc = std::make_shared<BasicFunctionDef>();
    nativeFunc->name = name;

    nativeFunc->inputs = { { "Dumy", Value(2.0) }}; // Single input, input will be a list!
    nativeFunc->outputs = outputs;
    nativeFunc->flags = flags;
    nativeFunc->dynamicInputProps = dynamicProps;

    nativeDefinitions.push_back({ nativeFunc, fun });
}

void NodeRegistry::RegisterNatives(VM& vm)
{
    for (NativeFunctionDef& def : nativeDefinitions)
    {
        vm.defineNative(def.functionDef->name.c_str(), def.functionDef->inputs.size(), def.nativeFun);
    }
}

void NodeRegistry::RegisterCompiledNode(const char* name, NodeCreationFun creationFunc)
{
    CompiledNodeDefPtr compiledNodeDef = std::make_shared<CompiledNodeDef>();
    compiledNodeDef->nodeCreationFunc = creationFunc;
    compiledNodeDef->name = name;

    compiledDefinitions.push_back(compiledNodeDef);
}

NodePtr CompiledNodeDef::MakeNode(IDGenerator& IDGenerator)
{
    return nodeCreationFunc(IDGenerator);
}
