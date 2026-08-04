# pragma once

#include "nodeRegistry.h"

#include "graph.h"
#include "graphCompiler.h"
#include "../utilities/utils.h"
#include "../runtime/standardLibraryFunctions.h"
#include "../runtime/extendedStandardLibrary.h"

#include <Natives.h>
#include <VMUtils.h>

#include <string>
#include <string_view>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

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
    nativeClassDefinitions.clear();

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
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Multiplies a number by itself",
            { "The number to square" },
            { "The squared number" }
        }
    );

    RegisterNativeFunc("Math::Abs",
        { { "Value", Value(0.0) } }, { { "Result", Value(0.0) } },
        &MathAbs, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the distance of a number from zero",
            { "The number whose absolute value is needed" },
            { "The non-negative absolute value" }
        });
    RegisterNativeFunc("Math::Min",
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        &MathMin, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the smaller of two numbers",
            { "The first number to compare", "The second number to compare" },
            { "The smaller number" }
        });
    RegisterNativeFunc("Math::Max",
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        &MathMax, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the larger of two numbers",
            { "The first number to compare", "The second number to compare" },
            { "The larger number" }
        });
    RegisterNativeFunc("Math::Clamp",
        { { "Value", Value(0.0) }, { "Min", Value(0.0) },
          { "Max", Value(1.0) } },
        { { "Result", Value(0.0) } },
        &MathClamp, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Limits a number to a minimum and maximum value",
            { "The number to limit", "The lowest allowed value", "The highest allowed value" },
            { "The value constrained to the requested range" }
        });
    RegisterNativeFunc("Math::Power",
        { { "Base", Value(0.0) }, { "Exponent", Value(1.0) } },
        { { "Result", Value(0.0) } },
        &MathPower, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Raises a number to a power",
            { "The number to raise", "The power applied to the base" },
            { "The base raised to the exponent" }
        });
    RegisterNativeFunc("Math::Sqrt",
        { { "Value", Value(0.0) } }, { { "Result", Value(0.0) } },
        &MathSqrt, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the square root of a number",
            { "The number whose square root is needed" },
            { "The square root" }
        });
    RegisterNativeFunc("Math::Floor",
        { { "Value", Value(0.0) } }, { { "Result", Value(0.0) } },
        &MathFloor, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Rounds a number down to the nearest integer",
            { "The number to round down" },
            { "The rounded-down number" }
        });
    RegisterNativeFunc("Math::Ceil",
        { { "Value", Value(0.0) } }, { { "Result", Value(0.0) } },
        &MathCeil, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Rounds a number up to the nearest integer",
            { "The number to round up" },
            { "The rounded-up number" }
        });
    RegisterNativeFunc("Math::Round",
        { { "Value", Value(0.0) } }, { { "Result", Value(0.0) } },
        &MathRound, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Rounds a number to the nearest integer",
            { "The number to round" },
            { "The nearest integer" }
        });
    RegisterNativeFunc("Math::Random",
        { { "Min", Value(0.0) }, { "Max", Value(1.0) } },
        { { "Result", Value(0.0) } },
        &MathRandom, NodeDefinitionFlags::ReadOnly,
        NodeDocumentation{
            "Returns a random number between two bounds",
            { "The lowest possible value", "The highest possible value" },
            { "A random number between Min and Max" }
        });

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
        NodeDefinitionFlags::ReadOnly,
        NodeDocumentation{
            "Checks whether a file or directory exists",
            { "The path to check" },
            { "True when the path exists" }
        }
    );

    RegisterNativeFunc("String::Length",
        { { "Value", Value(copyString("", 0)) } },
        { { "Length", Value(0.0) } },
        &lengthOfIterable,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the number of characters in text",
            { "The text to measure" },
            { "The character count" }
        }
    );

    RegisterNativeFunc("String::Split",
        { { "String", Value(copyString("", 0)) }, { "Separator", Value(copyString("", 0))} },
        { { "List", Value(newList()), -1, TypeRef::List(PinType::String) } },
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
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Splits text wherever a separator occurs",
            { "The text to split", "The separator between parts" },
            { "The separated text parts" }
        }
    );

    RegisterNativeFunc("System::Clock",
        { },
        { { "Time", Value(0.0) } },
        &clock,
        NodeDefinitionFlags::ReadOnly,
        NodeDocumentation{
            "Returns the elapsed processor time in seconds",
            {  },
            { "The current clock value" }
        }
    );

    RegisterNativeFunc("List::MakeList",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        [] (int argCount, Value* args, VM* vm)
        {
            return Value(args[0]); // Result is already a list!
        },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::Pure,
        {
            1, 16, TypeRef::Variable("T"), Value(0.0)
        },
        NodeDocumentation{
            "Collects its inputs into a typed list",
            { "The packed values supplied by the dynamic pins" },
            { "A list containing the supplied values" },
            "A value to append to the list"
        },
        { { "T", "Type" } }
    );

    RegisterNativeFunc("List::Length",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        { { "Length", Value(0.0) } },
        &lengthOfIterable,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the number of values in a list",
            { "The list to measure" },
            { "The number of values" }
        }
    );

    RegisterNativeFunc("List::In Bounds",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) }, { "Index", Value(0.0) } },
        { { "Result", Value(false) } },
        &inBounds,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether an index is valid for a list",
            { "The list to inspect", "The index to check" },
            { "True when the index identifies a list value" }
        }
    );

    RegisterNativeFunc("List::Concat",
        { { "A", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) },
          { "B", Value(newList()), -1, TypeRef::List(TypeRef::Variable("T")) } },
        { { "Result", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        &concat,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Combines two lists in order",
            { "The first list", "The list appended after the first" },
            { "A new list containing both inputs" }
        }
    );

    RegisterNativeFunc("List::Erase",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) }, { "Index", Value(0.0) } },
        { },
        &erase,
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Removes the value at an index from a list",
            { "The list to modify", "The index to remove" },
            {  }
        }
    );

    RegisterNativeFunc("List::Push",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) },
          { "Value", Value(), -1, TypeRef::Variable("T") } },
        { { "Size", Value(0.0) } },
        &push,
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Adds a value to the end of a list",
            { "The list to modify", "The value to append" },
            { "The list size after appending" }
        }
    );

    RegisterNativeFunc("List::Pop",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        { { "Value", Value(), -1, TypeRef::Variable("T") } },
        &pop,
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Removes and returns the final value in a list",
            { "The list to modify" },
            { "The removed value" }
        }
    );

    RegisterNativeFunc("List::Insert",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) }, { "Index", Value(0.0) },
          { "Value", Value(), -1, TypeRef::Variable("T") } },
        { { "Size", Value(0.0) } },
        &ListInsert, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Inserts a value at an index in a list",
            { "The list to modify", "The position for the new value", "The value to insert" },
            { "The list size after insertion" }
        });
    RegisterNativeFunc("List::Clear",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        { { "Size", Value(0.0) } },
        &ListClear, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Removes every value from a list",
            { "The list to clear" },
            { "The list size after clearing" }
        });
    RegisterNativeFunc("List::Slice",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) }, { "Start", Value(0.0) },
          { "Count", Value(0.0) } },
        { { "Result", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        &ListSlice, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Copies a consecutive section of a list",
            { "The source list", "The first index to copy", "The maximum number of values to copy" },
            { "A new list containing the requested section" }
        });
    RegisterNativeFunc("List::Reverse",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        { { "Result", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        &ListReverse, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Creates a list with the values in reverse order",
            { "The source list" },
            { "A reversed copy of the list" }
        });
    RegisterNativeFunc("List::Sort",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        { { "Result", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        &ListSort, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Creates a list with its values in ascending order",
            { "The source list" },
            { "A sorted copy of the list" }
        });
    RegisterNativeFunc("List::Distinct",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        { { "Result", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        &ListDistinct, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Creates a list with duplicate values removed",
            { "The source list" },
            { "A copy containing the first occurrence of each value" }
        });
    RegisterNativeFunc("List::Enumerate",
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        { { "Result", Value(newList()), -1, TypeRef::List(TypeRef::Tuple(
            { PinType::Float, TypeRef::Variable("T") })) } },
        &ListEnumerate, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Pairs every list value with its index",
            { "The source list" },
            { "A list of (index, value) tuples" }
        });
    RegisterNativeFunc("List::Zip",
        { { "A", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) },
          { "B", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("U")) } },
        { { "Result", Value(newList()), -1, TypeRef::List(TypeRef::Tuple(
            { TypeRef::Variable("T"), TypeRef::Variable("U") })) } },
        &ListZip, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Pairs values from two lists at matching indices",
            { "The first list", "The second list" },
            { "A list of (A, B) tuples, limited by the shorter input" }
        });

    RegisterNativeFunc("Map::Make Map",
        {},
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) } },
        &MapMake, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Creates an empty typed map",
            {},
            { "A new empty map" }
        },
        { { "K", "Key Type" }, { "V", "Value Type" } });
    RegisterNativeFunc("Map::Length",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) } },
        { { "Length", Value(0.0) } },
        &MapLength, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the number of entries in a map",
            { "The map to measure" },
            { "The number of entries" }
        });
    RegisterNativeFunc("Map::Find",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) },
          { "Key", Value(), -1, TypeRef::Variable("K") } },
        { { "Found", Value(false) },
          { "Value", Value(), -1, TypeRef::Variable("V") } },
        &MapFind, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Finds a value by key and reports whether the key exists",
            { "The map to search", "The key to find" },
            { "True when the key exists", "The associated value, or nil when absent" }
        });
    RegisterNativeFunc("Map::Contains Key",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) },
          { "Key", Value(), -1, TypeRef::Variable("K") } },
        { { "Result", Value(false) } },
        &MapContainsKey, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether a map contains a key",
            { "The map to search", "The key to find" },
            { "True when the key exists" }
        });
    RegisterNativeFunc("Map::Set",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) },
          { "Key", Value(), -1, TypeRef::Variable("K") },
          { "Value", Value(), -1, TypeRef::Variable("V") } },
        { { "Added", Value(false) } },
        &MapSet, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Adds or replaces a map entry",
            { "The map to modify", "The key to set", "The value to associate with the key" },
            { "True when a new key was added; false when an existing value was replaced" }
        });
    RegisterNativeFunc("Map::Remove",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) },
          { "Key", Value(), -1, TypeRef::Variable("K") } },
        { { "Found", Value(false) },
          { "Value", Value(), -1, TypeRef::Variable("V") } },
        &MapRemove, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Removes an entry by key",
            { "The map to modify", "The key to remove" },
            { "True when an entry was removed", "The removed value, or nil when absent" }
        });
    RegisterNativeFunc("Map::Clear",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) } },
        { { "Size", Value(0.0) } },
        &MapClear, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Removes every entry from a map",
            { "The map to clear" },
            { "The map size after clearing" }
        });
    RegisterNativeFunc("Map::Keys",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) } },
        { { "Keys", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("K")) } },
        &MapKeys, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Copies the keys from a map in insertion order",
            { "The map to read" },
            { "A list containing the map keys" }
        });
    RegisterNativeFunc("Map::Values",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) } },
        { { "Values", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("V")) } },
        &MapValues, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Copies the values from a map in insertion order",
            { "The map to read" },
            { "A list containing the map values" }
        });
    RegisterNativeFunc("Map::Copy",
        { { "Map", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) } },
        { { "Result", Value(newMap()), -1,
            TypeRef::Map(TypeRef::Variable("K"), TypeRef::Variable("V")) } },
        &MapCopy, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Creates a shallow copy of a map",
            { "The map to copy" },
            { "A new map containing the same keys and values" }
        });

    RegisterNativeFunc("File::ReadFile",
        { { "File", Value(copyString("", 0)) } },
        { { "Content", Value(copyString("", 0)) } },
        &readFile,
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Reads all text from a file",
            { "The path of the file to read" },
            { "The file contents" }
        }
    );

    RegisterNativeFunc("File::WriteFile",
        { { "File", Value(copyString("", 0)) }, { "Content", Value(copyString("", 0)) } },
        { },
        &writeFile,
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Replaces a file with the supplied text",
            { "The path of the file to write", "The text to write" },
            {  }
        }
    );

    RegisterNativeFunc("File::Read Text",
        { { "File", Value(copyString("", 0)) } },
        { { "Content", Value(copyString("", 0)) }, { "Success", Value(false) },
          { "Error", Value(copyString("", 0)) } },
        &FileReadText, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Reads text from a file and reports failures without stopping the graph",
            { "The path of the file to read" },
            { "The text read from the file", "True when reading succeeded", "An error message when reading failed" }
        });
    RegisterNativeFunc("File::Write Text",
        { { "File", Value(copyString("", 0)) },
          { "Content", Value(copyString("", 0)) } },
        { { "Success", Value(false) }, { "Error", Value(copyString("", 0)) } },
        &FileWriteText, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Writes text to a file and reports whether it succeeded",
            { "The path of the file to write", "The text that replaces the file" },
            { "True when writing succeeded", "An error message when writing failed" }
        });
    RegisterNativeFunc("File::Append Text",
        { { "File", Value(copyString("", 0)) },
          { "Content", Value(copyString("", 0)) } },
        { { "Success", Value(false) }, { "Error", Value(copyString("", 0)) } },
        &FileAppendText, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Adds text to the end of a file and reports whether it succeeded",
            { "The path of the file to update", "The text to append" },
            { "True when appending succeeded", "An error message when appending failed" }
        });
    RegisterNativeFunc("File::List Directory",
        { { "Directory", Value(copyString("", 0)) } },
        { { "Entries", Value(newList()), -1, TypeRef::List(PinType::String) }, { "Success", Value(false) },
          { "Error", Value(copyString("", 0)) } },
        &FileListDirectory, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Lists the entries directly inside a directory",
            { "The directory path to inspect" },
            { "The paths found in the directory", "True when the directory was read successfully", "An error message when listing failed" }
        });
    RegisterNativeFunc("Path::Combine",
        { { "A", Value(copyString("", 0)) }, { "B", Value(copyString("", 0)) } },
        { { "Result", Value(copyString("", 0)) } },
        &PathCombine, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Combines two path segments using the platform separator",
            { "The first path segment", "The path segment to append" },
            { "The combined path" }
        });
    RegisterNativeFunc("Path::Extension",
        { { "Path", Value(copyString("", 0)) } },
        { { "Result", Value(copyString("", 0)) } },
        &PathExtension, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the extension at the end of a path",
            { "The path to inspect" },
            { "The extension, including its dot" }
        });
    RegisterNativeFunc("Path::Filename",
        { { "Path", Value(copyString("", 0)) } },
        { { "Result", Value(copyString("", 0)) } },
        &PathFilename, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the final file or directory name in a path",
            { "The path to inspect" },
            { "The final path component" }
        });
    RegisterNativeFunc("Path::Parent",
        { { "Path", Value(copyString("", 0)) } },
        { { "Result", Value(copyString("", 0)) } },
        &PathParent, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the directory containing a path",
            { "The path to inspect" },
            { "The parent directory path" }
        });
    RegisterNativeFunc("Console::Read Input",
        {},
        { { "Text", Value(copyString("", 0)) } },
        &readInput, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Waits for and returns one line of console input",
            {  },
            { "The line entered by the user" }
        });

    RegisterNativeFunc("List::Contains",
        { { "List", Value(newList()) }, { "Value", Value(0.0) } },
        { { "Result", Value(false) } },
        &contains,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether a list contains a value",
            { "The list to search", "The value to find" },
            { "True when the value occurs in the list" }
        }
    );

    RegisterNativeFunc("String::Contains",
        { { "Text", Value(copyString("", 0)) }, { "Value", Value(copyString("", 0)) } },
        { { "Result", Value(false) } },
        & contains,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether text contains a substring",
            { "The text to search", "The substring to find" },
            { "True when the substring occurs" }
        }
    );

    RegisterNativeFunc("List::IndexOf",
        { { "List", Value(newList()) }, { "Value", Value() } },
        { { "Result", Value(0.0) } },
        &indexOf,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Finds the first index of a value in a list",
            { "The list to search", "The value to find" },
            { "The first matching index, or -1 when absent" }
        }
    );

    RegisterNativeFunc("Range::Length",
        { { "Range", Value(newRange(0.0, 0.0)) } },
        { { "Length", Value(0.0) } },
        &lengthOfIterable,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the number of values produced by a range",
            { "The range to measure" },
            { "The number of produced values" }
        }
    );

    RegisterNativeFunc("Range::In Bounds",
        { { "Range", Value(newRange(0.0, 0.0)) }, { "Index", Value(0.0) } },
        { { "Result", Value(false) } },
        &inBounds,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether an index is valid for a range",
            { "The range to inspect", "The index to check" },
            { "True when the index identifies a range value" }
        }
    );

    RegisterNativeFunc("Range::Contains",
        { { "Range", Value(newRange(0.0, 0.0)) }, { "Value", Value(0.0) } },
        { { "Result", Value(false) } },
        &contains,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether a range produces a number",
            { "The range to inspect", "The number to find" },
            { "True when the range produces the number" }
        }
    );

    RegisterNativeFunc("Range::IndexOf",
        { { "Range", Value(newRange(0.0, 0.0)) }, { "Value", Value(0.0) } },
        { { "Result", Value(0.0) } },
        &indexOf,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Finds the index of a number in a range",
            { "The range to search", "The number to find" },
            { "The matching index, or -1 when absent" }
        }
    );

    RegisterNativeFunc("Range::Make Advanced",
        { { "From", Value(0.0) }, { "To", Value(1.0) },
          { "Step", Value(1.0) }, { "Include Start", Value(true) },
          { "Include End", Value(true) } },
        { { "Range", Value(newRange(0.0, 1.0)) } },
        &RangeMakeAdvanced,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Creates a numeric range with explicit step and endpoint rules",
            { "The first boundary", "The final boundary", "The amount added between values", "Whether the starting boundary is produced", "Whether the ending boundary may be produced" },
            { "The configured numeric range" }
        });

    RegisterNativeFunc("String::IndexOf",
        { { "Text", Value(copyString("", 0)) }, { "Value", Value(copyString("", 0)) } },
        { { "Result", Value(0.0) } },
        &indexOf,
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Finds the first character index of a substring",
            { "The text to search", "The substring to find" },
            { "The first matching index, or -1 when absent" }
        }
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
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Converts text to lowercase",
            { "The text to convert" },
            { "The lowercase text" }
        }
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
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Converts text to uppercase",
            { "The text to convert" },
            { "The uppercase text" }
        }
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
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Extracts a section of text",
            { "The source text", "The first character index", "The maximum character count" },
            { "The requested section of text" }
        }
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
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Finds the first character index of a substring",
            { "The text to search", "The substring to find" },
            { "The first matching index, or -1 when absent" }
        }
    );

    RegisterNativeFunc("String::Trim",
        { { "Text", Value(copyString("", 0)) } },
        { { "Result", Value(copyString("", 0)) } },
        &StringTrim, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Removes whitespace from both ends of text",
            { "The text to trim" },
            { "The trimmed text" }
        });
    RegisterNativeFunc("String::Replace",
        { { "Text", Value(copyString("", 0)) },
          { "Search", Value(copyString("", 0)) },
          { "Replacement", Value(copyString("", 0)) } },
        { { "Result", Value(copyString("", 0)) } },
        &StringReplace, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Replaces occurrences of one substring with another",
            { "The source text", "The substring to replace", "The text inserted for each match" },
            { "The text after replacement" }
        });
    RegisterNativeFunc("String::Join",
        { { "Values", Value(newList()) },
          { "Separator", Value(copyString("", 0)) } },
        { { "Result", Value(copyString("", 0)) } },
        &StringJoin, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Combines a list of values into text with a separator",
            { "The values to combine", "The text placed between values" },
            { "The joined text" }
        });
    RegisterNativeFunc("String::Starts With",
        { { "Text", Value(copyString("", 0)) },
          { "Prefix", Value(copyString("", 0)) } },
        { { "Result", Value(false) } },
        &StringStartsWith, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether text begins with a prefix",
            { "The text to inspect", "The required prefix" },
            { "True when Text starts with Prefix" }
        });
    RegisterNativeFunc("String::Ends With",
        { { "Text", Value(copyString("", 0)) },
          { "Suffix", Value(copyString("", 0)) } },
        { { "Result", Value(false) } },
        &StringEndsWith, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether text ends with a suffix",
            { "The text to inspect", "The required suffix" },
            { "True when Text ends with Suffix" }
        });
    RegisterNativeFunc("String::Format",
        { { "Template", Value(copyString("", 0)) }, { "Values", Value(newList()) } },
        { { "Result", Value(copyString("", 0)) } },
        &StringFormat, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Replaces numbered placeholders in a template with values",
            { "Text containing numbered placeholders", "The values used to fill the placeholders" },
            { "The formatted text" }
        });
    RegisterNativeFunc("String::Parse Number",
        { { "Text", Value(copyString("", 0)) } },
        { { "Value", Value(0.0) }, { "Success", Value(false) } },
        &StringParseNumber, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Attempts to read a number from text",
            { "The text to parse" },
            { "The parsed number, or zero on failure", "True when the entire text is a valid number" }
        });
    RegisterNativeFunc("String::Parse Bool",
        { { "Text", Value(copyString("", 0)) } },
        { { "Value", Value(false) }, { "Success", Value(false) } },
        &StringParseBool, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Attempts to read a boolean from text",
            { "The text to parse" },
            { "The parsed boolean, or false on failure", "True when the text is a recognized boolean" }
        });

    RegisterNativeFunc("Functional::FindIf",
        { { "Iterable", Value(), -1,
            TypeRef::Iterable(TypeRef::Variable("T")) },
          { "Function", Value(newFunction()), -1,
            TypeRef::Function({ TypeRef::Variable("T") }, { PinType::Bool }) } },
        { { "Result", Value(), -1, TypeRef::Variable("T") } },
        &findIf,
        NodeDefinitionFlags::ReadOnly,
        NodeDocumentation{
            "Returns the first iterable value accepted by a predicate",
            { "The values to search", "A function that returns true for an accepted value" },
            { "The first accepted value, or nil when none match" }
        }
    );

    RegisterNativeFunc("Functional::Map",
        { { "Iterable", Value(), -1,
            TypeRef::Iterable(TypeRef::Variable("T")) },
          { "Function", Value(newFunction()), -1,
            TypeRef::Function(
                { TypeRef::Variable("T") }, { TypeRef::Variable("U") }) } },
        { { "Result", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("U")) } },
        &map,
        NodeDefinitionFlags::ReadOnly,
        NodeDocumentation{
            "Transforms every iterable value with a function",
            { "The values to transform", "The function applied to each value" },
            { "A list containing each transformed value" }
        }
    );

    RegisterNativeFunc("Functional::Filter",
        { { "Iterable", Value(), -1,
            TypeRef::Iterable(TypeRef::Variable("T")) },
          { "Function", Value(newFunction()), -1,
            TypeRef::Function({ TypeRef::Variable("T") }, { PinType::Bool }) } },
        { { "Result", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } },
        &filter,
        NodeDefinitionFlags::ReadOnly,
        NodeDocumentation{
            "Keeps the iterable values accepted by a predicate",
            { "The values to test", "A function that returns true for values to keep" },
            { "A list containing the accepted values" }
        }
    );

    RegisterNativeFunc("Functional::Reduce",
        { { "Iterable", Value(), -1,
            TypeRef::Iterable(TypeRef::Variable("T")) },
          { "Function", Value(newFunction()), -1,
            TypeRef::Function(
                { TypeRef::Variable("U"), TypeRef::Variable("T") },
                { TypeRef::Variable("U") }) },
          { "Init", Value(), -1, TypeRef::Variable("U") } },
        { { "Result", Value(), -1, TypeRef::Variable("U") } },
        &reduce,
        NodeDefinitionFlags::ReadOnly,
        NodeDocumentation{
            "Combines iterable values into one accumulated result",
            { "The values to combine", "A function that combines the accumulator and next value", "The initial accumulator value" },
            { "The final accumulated value" }
        }
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
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Replaces the system clipboard text",
            { "The text to place on the clipboard" },
            {  }
        }
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
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Pauses execution for a duration",
            { "The number of seconds to wait" },
            {  }
        }
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
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Moves the mouse cursor to screen coordinates",
            { "The horizontal screen coordinate", "The vertical screen coordinate" },
            {  }
        }
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
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Clicks the primary mouse button at screen coordinates",
            { "The horizontal screen coordinate", "The vertical screen coordinate" },
            {  }
        }
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
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Drags the primary mouse button between two screen positions",
            { "The starting horizontal coordinate", "The starting vertical coordinate", "The ending horizontal coordinate", "The ending vertical coordinate" },
            {  }
        }
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
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Presses and releases one named keyboard key",
            { "The name of the key to press" },
            {  }
        }
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
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Presses a group of named keyboard keys together",
            { "The key names to press" },
            {  }
        }
    );

    RegisterNativeFunc("Functional::Call",
        { { "Function", Value(newFunction()) }, { "Params", Value() } },
        { { "Result", Value() } },
        [](int argCount, Value* args, VM* vm)
        {
            if (!isCallable(args[0]) || !isList(args[1]))
            {
                return Value();
            }

            ObjList* params = asList(args[1]);

            if (getCallableArity(args[0]) != params->items.size())
            {
                return Value();
            }

            // Same as callFunction
            vm->push(args[0]);
            for (Value& param : params->items)
            {
                vm->push(param);
            }
            vm->callValue(args[0], params->items.size());
            if (!isNative(args[0]))
                vm->run(vm->getFrameCount() - 1);

            return vm->pop();
        },
        NodeDefinitionFlags::None,
        NodeDocumentation{
            "Calls a function value with a list of arguments",
            { "The function to call", "The argument values in parameter order" },
            { "The value returned by the function" }
        }
    );

    RegisterExtendedStandardLibrary(*this);
}

namespace
{
std::string StableKey(std::string_view name)
{
    std::string result;
    bool needsSeparator = false;
    for (char character : name)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value))
        {
            if (needsSeparator && !result.empty())
                result.push_back('_');
            result.push_back(static_cast<char>(std::tolower(value)));
            needsSeparator = false;
        }
        else
        {
            needsSeparator = true;
        }
    }
    return result;
}

std::string StableDefinitionId(std::string_view kind, std::string_view name)
{
    std::string result = "vlox.std.";
    result += kind;
    result.push_back('.');

    for (size_t index = 0; index < name.size(); ++index)
    {
        if (index + 1 < name.size() && name[index] == ':' && name[index + 1] == ':')
        {
            result.push_back('.');
            ++index;
            continue;
        }

        const unsigned char value = static_cast<unsigned char>(name[index]);

        if (std::isalnum(value))
            result.push_back(static_cast<char>(std::tolower(value)));

        else if (!result.empty() && result.back() != '.' && result.back() != '_')
            result.push_back('_');
    }

    while (!result.empty() && result.back() == '_')
        result.pop_back();

    return result;
}

void ApplyStablePortKeys(BasicFunctionDef& definition)
{
    const auto apply = [&](std::vector<BasicFunctionDef::Input>& ports, const char* direction)
    {
        std::set<std::string> keys;
        for (size_t index = 0; index < ports.size(); ++index)
        {
            BasicFunctionDef::Input& port = ports[index];
            if (port.key.empty())
                port.key = StableKey(port.name);
            if (port.key.empty())
                throw std::invalid_argument(definition.name + " has an empty stable " + direction + " port key at index " + std::to_string(index));
            if (!keys.insert(port.key).second)
                throw std::invalid_argument(definition.name + " declares duplicate " + direction + " port key '" + port.key + "'");
        }
    };

    apply(definition.inputs, "input");
    apply(definition.outputs, "output");

    std::set<std::string> genericKeys;
    for (GenericTypeProperty& property : definition.genericTypeProperties)
    {
        if (property.key.empty())
            property.key = StableKey(property.variableName);

        if (property.key.empty() || !genericKeys.insert(property.key).second)
            throw std::invalid_argument(definition.name + " declares an empty or duplicate generic parameter key");
    }

    if (HasFlag(definition.flags, NodeDefinitionFlags::DynamicInputs))
    {
        if (definition.dynamicInputProps.familyKey.empty() || definition.dynamicInputProps.memberKey.empty() || definition.dynamicInputProps.orderingMemberKey.empty())
            throw std::invalid_argument(definition.name + " has an incomplete dynamic port identity");
    }
}

std::string ComputeDefinitionCompatibilityFingerprintImpl(const BasicFunctionDef& definition);

void ApplyStableDefinitionSchema(BasicFunctionDef& definition, std::string_view kind)
{
    if (definition.id.empty())
        definition.id = StableDefinitionId(kind, definition.name);

    if (definition.id.empty() || definition.revision == 0)
        throw std::invalid_argument(definition.name + " has an invalid stable definition schema");

    ApplyStablePortKeys(definition);
    definition.compatibilityFingerprint = ComputeDefinitionCompatibilityFingerprintImpl(definition);
}

std::string ComputeDefinitionCompatibilityFingerprintImpl(const BasicFunctionDef& definition)
{
    std::vector<std::string> components{ "id=" + definition.id, "flags=" + std::to_string(static_cast<int>(definition.flags)) };

    for (const BasicFunctionDef::Input& input : definition.inputs)
        components.push_back("input:" + input.key + ":" + input.type.ToString());

    for (const BasicFunctionDef::Input& output : definition.outputs)
        components.push_back("output:" + output.key + ":" + output.type.ToString());

    for (const GenericTypeProperty& generic : definition.genericTypeProperties)
        components.push_back("generic:" + generic.key);

    if (HasFlag(definition.flags, NodeDefinitionFlags::DynamicInputs))
    {
        const DynamicInputProps& dynamic = definition.dynamicInputProps;
        components.push_back("dynamic:" + dynamic.familyKey + ":" + dynamic.memberKey + ":" + dynamic.orderingMemberKey + ":" + dynamic.type.ToString() + ":" +
            std::to_string(dynamic.minInputs) + ":" + std::to_string(dynamic.maxInputs));
    }

    std::sort(components.begin() + 2, components.end());

    uint64_t hash = 1469598103934665603ull;
    for (const std::string& component : components)
    {
        for (const unsigned char byte : component)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
    }

    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

std::string DisplayDefinitionName(const std::string& name)
{
    const size_t separator = name.rfind("::");
    return separator == std::string::npos ? name : name.substr(separator + 2);
}

void ApplyDocumentation(BasicFunctionDef& definition,
                        const NodeDocumentation& documentation)
{
    const auto validate = [&](const char* description, const char* field)
    {
        if (!description || !*description)
            throw std::invalid_argument(
                definition.name + " has no " + field + " description");
        const std::string value(description);
        if (value.back() == '.')
            throw std::invalid_argument(
                definition.name + " has a " + field +
                " description ending in a period");
        return value;
    };

    if (documentation.inputs.size() != definition.inputs.size())
        throw std::invalid_argument(
            definition.name + " documents " +
            std::to_string(documentation.inputs.size()) + " inputs but declares " +
            std::to_string(definition.inputs.size()));
    if (documentation.outputs.size() != definition.outputs.size())
        throw std::invalid_argument(
            definition.name + " documents " +
            std::to_string(documentation.outputs.size()) + " outputs but declares " +
            std::to_string(definition.outputs.size()));

    definition.description = validate(documentation.description, "node");
    for (size_t index = 0; index < definition.inputs.size(); ++index)
        definition.inputs[index].description =
            validate(documentation.inputs[index], "input");
    for (size_t index = 0; index < definition.outputs.size(); ++index)
        definition.outputs[index].description =
            validate(documentation.outputs[index], "output");

    if (HasFlag(definition.flags, NodeDefinitionFlags::DynamicInputs))
        definition.dynamicInputProps.description =
            validate(documentation.dynamicInput, "dynamic input");
}

void ApplyGenericTypeProperties(BasicFunctionDef& definition, std::vector<GenericTypeProperty> genericTypeProperties)
{
    std::set<std::string> names;
    for (const GenericTypeProperty& property : genericTypeProperties)
    {
        if (property.variableName.empty() || property.label.empty())
            throw std::invalid_argument(definition.name + " has an incomplete generic type property");

        if (!names.insert(property.variableName).second)
            throw std::invalid_argument( definition.name + " exposes generic type '" + property.variableName + "' more than once");

        const auto containsVariable =
            [&](const BasicFunctionDef::Input& port)
            {
                return port.type.ContainsVariable(property.variableName);
            };

        const bool declaredOnPin = 
               std::any_of(definition.inputs.begin(), definition.inputs.end(), containsVariable)
            || std::any_of(definition.outputs.begin(), definition.outputs.end(), containsVariable);

        const bool declaredOnDynamicInput = HasFlag(definition.flags, NodeDefinitionFlags::DynamicInputs) && definition.dynamicInputProps.type.ContainsVariable(property.variableName);

        if (!declaredOnPin && !declaredOnDynamicInput)
            throw std::invalid_argument(definition.name + " exposes undeclared generic type '" + property.variableName + "'");
    }
    definition.genericTypeProperties = std::move(genericTypeProperties);
}

void ValidateDynamicInputProperties(const BasicFunctionDef& definition)
{
    if (!HasFlag(definition.flags, NodeDefinitionFlags::DynamicInputs))
        throw std::invalid_argument(definition.name + " declares dynamic input properties without the DynamicInputs flag");

    if (definition.dynamicInputProps.minInputs < 0)
        throw std::invalid_argument(definition.name + " declares a negative minimum input count");

    if (definition.dynamicInputProps.maxInputs < definition.dynamicInputProps.minInputs)
        throw std::invalid_argument(definition.name + " declares a maximum input count below its minimum");
}
}

std::string ComputeDefinitionCompatibilityFingerprint(const BasicFunctionDef& definition)
{
    return ComputeDefinitionCompatibilityFingerprintImpl(definition);
}

void NodeRegistry::RegisterNativeFunc(const char* name,
    std::vector<BasicFunctionDef::Input>&& inputs,
    std::vector<BasicFunctionDef::Input>&& outputs,
    NativeFn fun,
    NodeDefinitionFlags flags,
    NodeDocumentation documentation,
    std::vector<GenericTypeProperty> genericTypeProperties)
{
    BasicFunctionDefPtr nativeFunc  = std::make_shared<BasicFunctionDef>();
    nativeFunc->name = name;

    nativeFunc->inputs = inputs;
    nativeFunc->outputs = outputs;
    nativeFunc->flags = flags;

    ApplyDocumentation(*nativeFunc, documentation);
    ApplyGenericTypeProperties(*nativeFunc, std::move(genericTypeProperties));
    ApplyStableDefinitionSchema(*nativeFunc, "native");

    if (std::any_of(nativeDefinitions.begin(), nativeDefinitions.end(), [&](const NativeFunctionDef& existing) { return existing.functionDef && existing.functionDef->id == nativeFunc->id; }))
        throw std::invalid_argument("Duplicate native definition ID '" + nativeFunc->id + "'");

    nativeDefinitions.push_back({ nativeFunc, fun });
}

void NodeRegistry::RegisterNativeFunc(const char* name,
    std::vector<BasicFunctionDef::Input>&& outputs, NativeFn fun,
    NodeDefinitionFlags flags,
    BasicFunctionDef::DynamicInputProps&& dynamicProps,
    NodeDocumentation documentation,
    std::vector<GenericTypeProperty> genericTypeProperties)
{
    BasicFunctionDefPtr nativeFunc = std::make_shared<BasicFunctionDef>();
    nativeFunc->name = name;

    // The VM receives all dynamic values packed in this single list argument.
    nativeFunc->inputs = {
        { "Values", Value(2.0), -1, TypeRef(PinType::Any),
          "The values to include in the list" }
    };
    nativeFunc->outputs = outputs;
    nativeFunc->flags = flags;
    nativeFunc->dynamicInputProps = dynamicProps;

    ApplyDocumentation(*nativeFunc, documentation);
    ApplyGenericTypeProperties(*nativeFunc, std::move(genericTypeProperties));
    ApplyStableDefinitionSchema(*nativeFunc, "native");

    if (std::any_of(nativeDefinitions.begin(), nativeDefinitions.end(), [&](const NativeFunctionDef& existing) { return existing.functionDef && existing.functionDef->id == nativeFunc->id; }))
        throw std::invalid_argument("Duplicate native definition ID '" + nativeFunc->id + "'");

    nativeDefinitions.push_back({ nativeFunc, fun });
}

void NodeRegistry::RegisterNativeClass(const char* name, std::vector<NativeMethodDef> methods)
{
    if (!name || !*name)
        throw std::invalid_argument("Native class names cannot be empty");
    if (std::any_of(nativeClassDefinitions.begin(), nativeClassDefinitions.end(), [name](const NativeClassDefinition& definition) { return definition.name == name; }))
        throw std::invalid_argument("Duplicate native class name '" + std::string(name) + "'");
    nativeClassDefinitions.push_back({ name, std::move(methods) });
}

void NodeRegistry::RegisterNatives(VM& vm)
{
    for (const NativeClassDefinition& definition : nativeClassDefinitions)
        vm.defineNativeClass(definition.name.c_str(), std::vector<NativeMethodDef>(definition.methods));
    for (NativeFunctionDef& def : nativeDefinitions)
    {
        vm.defineNative(def.functionDef->name.c_str(), def.functionDef->inputs.size(), def.nativeFun);
    }
}

void NodeRegistry::RegisterCompiledNode(const char* name, NodeCreationFun creationFunc,
    std::vector<BasicFunctionDef::Input>&& inputs,
    std::vector<BasicFunctionDef::Input>&& outputs,
    NodeDefinitionFlags flags,
    NodeDocumentation documentation,
    std::vector<GenericTypeProperty> genericTypeProperties)
{
    CompiledNodeDefPtr compiledNodeDef = std::make_shared<CompiledNodeDef>();
    compiledNodeDef->nodeCreationFunc = creationFunc;
    compiledNodeDef->name = name;

    BasicFunctionDefPtr funtionDef = std::make_shared<BasicFunctionDef>();
    funtionDef->name = name;

    funtionDef->inputs = inputs;
    funtionDef->outputs = outputs;
    funtionDef->flags = flags;

    ApplyDocumentation(*funtionDef, documentation);
    ApplyGenericTypeProperties(*funtionDef, std::move(genericTypeProperties));
    ApplyStableDefinitionSchema(*funtionDef, "compiled");

    if (std::any_of(compiledDefinitions.begin(), compiledDefinitions.end(), [&](const CompiledNodeDefPtr& existing)
        { return existing && existing->id == funtionDef->id; }))
        throw std::invalid_argument("Duplicate compiled definition ID '" + funtionDef->id + "'");

    compiledNodeDef->functionDef = funtionDef;
    compiledNodeDef->id = funtionDef->id;
    compiledNodeDef->revision = funtionDef->revision;

    compiledDefinitions.push_back(compiledNodeDef);
}

void NodeRegistry::RegisterCompiledNode(const char* name, NodeCreationFun creationFunc,
    std::vector<BasicFunctionDef::Input>&& inputs,
    std::vector<BasicFunctionDef::Input>&& outputs,
    NodeDefinitionFlags flags,
    BasicFunctionDef::DynamicInputProps&& dynamicProps,
    NodeDocumentation documentation,
    std::vector<GenericTypeProperty> genericTypeProperties)
{
    CompiledNodeDefPtr compiledNodeDef = std::make_shared<CompiledNodeDef>();
    compiledNodeDef->nodeCreationFunc = creationFunc;
    compiledNodeDef->name = name;

    BasicFunctionDefPtr functionDef = std::make_shared<BasicFunctionDef>();
    functionDef->name = name;
    functionDef->inputs = std::move(inputs);
    functionDef->outputs = std::move(outputs);
    functionDef->flags = flags;
    functionDef->dynamicInputProps = std::move(dynamicProps);

    ValidateDynamicInputProperties(*functionDef);
    ApplyDocumentation(*functionDef, documentation);
    ApplyGenericTypeProperties(*functionDef, std::move(genericTypeProperties));
    ApplyStableDefinitionSchema(*functionDef, "compiled");

    if (std::any_of(compiledDefinitions.begin(), compiledDefinitions.end(), [&](const CompiledNodeDefPtr& existing) { return existing && existing->id == functionDef->id; }))
        throw std::invalid_argument("Duplicate compiled definition ID '" + functionDef->id + "'");

    compiledNodeDef->id = functionDef->id;
    compiledNodeDef->revision = functionDef->revision;
    compiledNodeDef->functionDef = std::move(functionDef);
    compiledDefinitions.push_back(std::move(compiledNodeDef));
}

NodePtr CompiledNodeDef::MakeNode(IDGenerator& IDGenerator)
{
    NodePtr node = nodeCreationFunc(IDGenerator);
    node->SerializationType = "compiled";
    node->DefinitionId = id;
    node->DefinitionRevision = revision;
    node->DefinitionFlags = functionDef->flags;
    node->Description = functionDef->description;
    node->GenericTypeProperties = functionDef->genericTypeProperties;

    if (HasFlag(functionDef->flags, NodeDefinitionFlags::DynamicInputs))
        node->ConfigureDynamicInputs(functionDef->dynamicInputProps);

    size_t inputIndex = 0;

    for (Pin& pin : node->Inputs)
    {
        if (pin.Type == PinType::Flow)
        {
            if (pin.Identity.kind == PortIdentityKind::None) pin.Identity = PortIdentity::Fixed("execute");
            if (pin.Description.empty())
                pin.Description = "Execution input for " +
                    DisplayDefinitionName(name);
            continue;
        }

        if (pin.Identity.kind == PortIdentityKind::None && !HasFlag(functionDef->flags, NodeDefinitionFlags::DynamicInputs) && inputIndex < functionDef->inputs.size())
            pin.Identity = PortIdentity::Fixed(functionDef->inputs[inputIndex].key);
        else if (pin.Identity.kind == PortIdentityKind::None && HasFlag(functionDef->flags, NodeDefinitionFlags::DynamicInputs))
            pin.Identity = PortIdentity::Dynamic(functionDef->dynamicInputProps.familyKey, DynamicSlotId::New(), functionDef->dynamicInputProps.memberKey);

        if (pin.Description.empty())
        {
            if (inputIndex < functionDef->inputs.size())
                pin.Description = functionDef->inputs[inputIndex].description;
            else if (HasFlag(functionDef->flags,
                             NodeDefinitionFlags::DynamicInputs))
                pin.Description =
                    functionDef->dynamicInputProps.description;
            else
                pin.Description = "Input " + std::to_string(inputIndex + 1) +
                    " for " + DisplayDefinitionName(name);
        }

        ++inputIndex;
    }

    const size_t flowOutputCount = static_cast<size_t>(std::count_if(node->Outputs.begin(), node->Outputs.end(), [](const Pin& pin) { return pin.Type == PinType::Flow; }));
    size_t flowOutputIndex = 0;
    size_t outputIndex = 0;

    for (Pin& pin : node->Outputs)
    {
        if (pin.Type == PinType::Flow)
        {
            if (pin.Identity.kind == PortIdentityKind::None)
                pin.Identity = PortIdentity::Fixed(flowOutputCount == 1 ? "then" : "branch_" + std::to_string(flowOutputIndex));

            ++flowOutputIndex;

            if (pin.Description.empty())
                pin.Description = "Execution output from " + DisplayDefinitionName(name);
            continue;
        }

        if (pin.Identity.kind == PortIdentityKind::None && outputIndex < functionDef->outputs.size())
            pin.Identity = PortIdentity::Fixed(functionDef->outputs[outputIndex].key);

        if (pin.Description.empty())
        {
            if (outputIndex < functionDef->outputs.size())
                pin.Description = functionDef->outputs[outputIndex].description;
            else
                pin.Description = "Output " + std::to_string(outputIndex + 1) + " from " + DisplayDefinitionName(name);
        }
        ++outputIndex;
    }
    return node;
}

const NativeFunctionDef* NodeRegistry::FindNative(const std::string& name) const
{
    for (const NativeFunctionDef& definition : nativeDefinitions)
    {
        if (definition.functionDef && (definition.functionDef->name == name || definition.functionDef->id == name))
            return &definition;
    }

    return nullptr;
}

CompiledNodeDefPtr NodeRegistry::FindCompiled(const std::string& name) const
{
    for (const CompiledNodeDefPtr& definition : compiledDefinitions)
    {
        if (definition && (definition->name == name || definition->id == name))
            return definition;
    }

    return nullptr;
}
