# pragma once

#include "nodeRegistry.h"

#include "graph.h"
#include "graphCompiler.h"
#include "../utilities/utils.h"

#include <Natives.h>

#include <string>
#include <string_view>
#include <filesystem>


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
