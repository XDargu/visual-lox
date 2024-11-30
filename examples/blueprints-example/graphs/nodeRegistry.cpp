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
#include <map>
#include <set>

#include <locale>
#include <codecvt>
#include <string>

#ifdef _WIN32
#include <windows.h>

bool IsExtendedKey(WORD key)
{
    static const std::set<WORD> values{
        VK_PRIOR ,
        VK_NEXT  ,
        VK_END   ,
        VK_HOME  ,
        VK_LEFT  ,
        VK_UP    ,
        VK_RIGHT ,
        VK_DOWN  ,
        VK_INSERT,
        VK_DELETE,
    };

    return values.find(key) != values.end();
}

WORD GetSpecialKey(const std::string& name)
{
    static const std::map<std::string, WORD> values {
        { "BACK"        , VK_BACK       },
        { "TAB"         , VK_TAB        },
        { "CLEAR"       , VK_CLEAR      },
        { "RETURN"      , VK_RETURN     },
        { "SHIFT"       , VK_SHIFT      },
        { "CONTROL"     , VK_CONTROL    },
        { "MENU"        , VK_MENU       },
        { "PAUSE"       , VK_PAUSE      },
        { "CAPITAL"     , VK_CAPITAL    },
        { "KANA"        , VK_KANA       },
        { "HANGEUL"     , VK_HANGEUL    },
        { "HANGUL"      , VK_HANGUL     },
        { "IME_ON"      , VK_IME_ON     },
        { "JUNJA"       , VK_JUNJA      },
        { "FINAL"       , VK_FINAL      },
        { "HANJA"       , VK_HANJA      },
        { "KANJI"       , VK_KANJI      },
        { "IME_OFF"     , VK_IME_OFF    },
        { "ESCAPE"      , VK_ESCAPE     },
        { "CONVERT"     , VK_CONVERT    },
        { "NONCONVERT"  , VK_NONCONVERT },
        { "ACCEPT"      , VK_ACCEPT     },
        { "MODECHANGE"  , VK_MODECHANGE },
        { "SPACE"       , VK_SPACE      },
        { "PRIOR"       , VK_PRIOR      },
        { "NEXT"        , VK_NEXT       },
        { "END"         , VK_END        },
        { "HOME"        , VK_HOME       },
        { "LEFT"        , VK_LEFT       },
        { "UP"          , VK_UP         },
        { "RIGHT"       , VK_RIGHT      },
        { "DOWN"        , VK_DOWN       },
        { "SELECT"      , VK_SELECT     },
        { "PRINT"       , VK_PRINT      },
        { "EXECUTE"     , VK_EXECUTE    },
        { "SNAPSHOT"    , VK_SNAPSHOT   },
        { "INSERT"      , VK_INSERT     },
        { "DELETE"      , VK_DELETE     },
        { "HELP"        , VK_HELP       },
        { "LWIN"        , VK_LWIN       },
        { "RWIN"        , VK_RWIN       },
        { "APPS"        , VK_APPS       },
        { "NUMPAD0"     , VK_NUMPAD0    },
        { "NUMPAD1"     , VK_NUMPAD1    },
        { "NUMPAD2"     , VK_NUMPAD2    },
        { "NUMPAD3"     , VK_NUMPAD3    },
        { "NUMPAD4"     , VK_NUMPAD4    },
        { "NUMPAD5"     , VK_NUMPAD5    },
        { "NUMPAD6"     , VK_NUMPAD6    },
        { "NUMPAD7"     , VK_NUMPAD7    },
        { "NUMPAD8"     , VK_NUMPAD8    },
        { "NUMPAD9"     , VK_NUMPAD9    },
        { "MULTIPLY"    , VK_MULTIPLY   },
        { "ADD"         , VK_ADD        },
        { "SEPARATOR"   , VK_SEPARATOR  },
        { "SUBTRACT"    , VK_SUBTRACT   },
        { "DECIMAL"     , VK_DECIMAL    },
        { "DIVIDE"      , VK_DIVIDE     },
        { "F1"          , VK_F1         },
        { "F2"          , VK_F2         },
        { "F3"          , VK_F3         },
        { "F4"          , VK_F4         },
        { "F5"          , VK_F5         },
        { "F6"          , VK_F6         },
        { "F7"          , VK_F7         },
        { "F8"          , VK_F8         },
        { "F9"          , VK_F9         },
        { "F10"         , VK_F10        },
        { "F11"         , VK_F11        },
        { "F12"         , VK_F12        },
        { "F13"         , VK_F13        },
        { "F14"         , VK_F14        },
        { "F15"         , VK_F15        },
        { "F16"         , VK_F16        },
        { "F17"         , VK_F17        },
        { "F18"         , VK_F18        },
        { "F19"         , VK_F19        },
        { "F20"         , VK_F20        },
        { "F21"         , VK_F21        },
        { "F22"         , VK_F22        },
        { "F23"         , VK_F23        },
        { "F24"         , VK_F24        },
    };

    auto it = values.find(name);
    if (it != values.end())
    {
        return it->second;
    }

    return 0;
}

WORD GetKeyFromName(const std::string& name)
{
    if (name.size() != 1)
    {
        return GetSpecialKey(name);
    }
    else
    {
        // Conver to wide string
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wide = converter.from_bytes(name);

        return wide[0];
    }
}

void PressKey(WORD key)
{
    INPUT inputs[1] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = key;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE | (IsExtendedKey(key) ? KEYEVENTF_EXTENDEDKEY : 0);

    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
}

void ReleaseKey(WORD key)
{
    INPUT inputs[1] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = key;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP | (IsExtendedKey(key) ? KEYEVENTF_EXTENDEDKEY : 0);

    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
}

void PressReleaseKey(WORD key)
{
    INPUT inputs[2] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = key;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE | (IsExtendedKey(key) ? KEYEVENTF_EXTENDEDKEY : 0);

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = key;
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP | (IsExtendedKey(key) ? KEYEVENTF_EXTENDEDKEY : 0);

    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
}

void PressReleaseKeys(const std::vector<WORD>& keys)
{
    std::vector<INPUT> inputs;
    inputs.reserve(keys.size());
    //ZeroMemory(&inputs[0], static_cast<UINT>(inputs.size()));

    for (WORD key : keys)
    {
        INPUT input;
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = key;
        input.ki.dwFlags = (IsExtendedKey(key) ? KEYEVENTF_EXTENDEDKEY : 0);

        inputs.push_back(input);
    }

    SendInput(static_cast<UINT>(inputs.size()), &inputs[0], sizeof(INPUT));

    Sleep(10);

    inputs.clear();
    //ZeroMemory(&inputs[0], static_cast<UINT>(inputs.size()));

    for (WORD key : keys)
    {
        INPUT input;
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = key;
        input.ki.dwFlags = KEYEVENTF_KEYUP | (IsExtendedKey(key) ? KEYEVENTF_EXTENDEDKEY : 0);

        inputs.push_back(input);
    }

    SendInput(static_cast<UINT>(inputs.size()), &inputs[0], sizeof(INPUT));
}


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

#ifdef _WIN32

            const WORD key = GetKeyFromName(str->chars);
            if (key == 0)
                return Value();
            
            PressReleaseKey(key);
    #endif
            return Value();

        },
        NodeFlags::None
    );

    RegisterNativeFunc("System::PressKeys",
        { { "Keys", Value(newList())} },
        { },
        [](int argCount, Value* args, VM* vm)
    {
        if (!isList(args[0]))
            return Value();

        ObjList* list = asList(args[0]);

#ifdef _WIN32

        std::vector<WORD> keys;
        keys.reserve(list->items.size());

        for (const Value& keyVal : list->items)
        {
            // Ignore non-strings
            if (isString(keyVal))
            {
                ObjString* str = asString(keyVal);
                const WORD key = GetKeyFromName(str->chars);
                keys.push_back(key);
            }
        }

        PressReleaseKeys(keys);
        
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
