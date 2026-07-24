#include "standardLibrary.h"

#include "../graphs/nodeRegistry.h"
#include "../native/nodes/append.h"
#include "../native/nodes/branch.h"
#include "../native/nodes/expressions.h"
#include "../native/nodes/for-in.h"
#include "../native/nodes/list.h"
#include "../native/nodes/math.h"
#include "../native/nodes/match.h"
#include "../native/nodes/print.h"
#include "../native/nodes/range.h"
#include "../native/nodes/repeat.h"
#include "../native/nodes/while.h"

void RegisterStandardLibrary(NodeRegistry& registry)
{
    VM& vm = VM::getInstance();
    const bool wasGcAllowed = vm.isGarbageCollectionAllowed();
    vm.allowGarbageCollection(false);

    registry.RegisterCompiledNode("Flow::Branch", &BuildBranchNode, {}, { { "Value", Value(false) } });
    registry.RegisterCompiledNode("Flow::For In", &BuildForInNode, {}, { { "Value", Value(0.0) } });
    registry.RegisterCompiledNode("Flow::While", &BuildWhileNode,
        { { "Condition", Value(false) } }, {});
    registry.RegisterCompiledNode("Flow::Repeat", &BuildRepeatNode,
        { { "Count", Value(1.0) } }, { { "Index", Value(0.0) } });
    registry.RegisterCompiledNode("Debug::Print", &BuildPrintNode, { { "Value", Value() } }, {});
    registry.RegisterCompiledNode("String::Append", &CreateAppendNode, { { "Value", Value(takeString("", 0)) } }, { { "Value", Value(takeString("", 0)) } }, NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Add", &CreateAddNode, { { "Value", Value(0.0) } }, { { "Value", Value(0.0) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Subtract", &CreateSubtractNode, { { "Value", Value(0.0) } }, { { "Value", Value(0.0) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Multiply", &CreateMultiplyNode, { { "Value", Value(0.0) } }, { { "Value", Value(0.0) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Divide", &CreateDivideNode, { { "Value", Value(0.0) } }, { { "Value", Value(0.0) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Greater Than", &CreateGreaterNode, { { "Value", Value(0.0) } }, { { "Value", Value(false) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Less Than", &CreateLessNode, { { "Value", Value(0.0) } }, { { "Value", Value(false) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Equals", &CreateEqualsNode,
        { { "A", Value() }, { "B", Value() } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Modulo", &CreateModuloNode, { { "Value", Value(0.0) } }, { { "Value", Value(0.0) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Negate", &BuildNegateNode,
        { { "Value", Value(0.0) } }, { { "Result", Value(0.0) } },
        NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Not Equals", &BuildNotEqualsNode,
        { { "A", Value() }, { "B", Value() } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Greater Or Equal", &BuildGreaterOrEqualNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Math::Less Or Equal", &BuildLessOrEqualNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Logic::Not", &BuildNotNode,
        { { "Value", Value(false) } }, { { "Result", Value(false) } },
        NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Logic::And", &BuildAndNode,
        { { "A", Value(false) }, { "B", Value(false) } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Logic::Or", &BuildOrNode,
        { { "A", Value(false) }, { "B", Value(false) } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Value::Is Nil", &BuildIsNilNode,
        { { "Value", Value() } }, { { "Result", Value(false) } },
        NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("Value::Coalesce", &BuildCoalesceNode,
        { { "A", Value() }, { "B", Value() } },
        { { "Result", Value() } }, NodeDefinitionFlags::Pure);

    registry.RegisterCompiledNode("Flow::Match", &BuildMatchFlowNode,
        { { "Value", Value() }, { "Pattern 1", Value(0.0) } }, {},
        NodeDefinitionFlags::DynamicInputs);
    registry.RegisterCompiledNode("Range::Make", &BuildRangeNode,
        { { "From", Value(0.0) }, { "To", Value(1.0) } },
        { { "Range", Value(newRange(0.0, 1.0)) } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("List::Get By Index", &BuildListGetByIndexNode,
        { { "List", Value(newList()) }, { "Index", Value(0.0) } },
        { { "Value", Value() } }, NodeDefinitionFlags::Pure);
    registry.RegisterCompiledNode("List::Set By Index", &BuildListSetByIndexNode,
        { { "List", Value(newList()) }, { "Index", Value(0.0) }, { "Value", Value() } },
        { { "Value", Value(newList()) } });

    registry.RegisterDefinitions();
    vm.allowGarbageCollection(wasGcAllowed);
}

void MarkNodeRegistryRoots(NodeRegistry& registry, VM& vm)
{
    for (NativeFunctionDef& definition : registry.nativeDefinitions)
    {
        for (BasicFunctionDef::Input& input : definition.functionDef->inputs)
            vm.markValue(input.value);
        for (BasicFunctionDef::Input& output : definition.functionDef->outputs)
            vm.markValue(output.value);
    }

    for (CompiledNodeDefPtr& definition : registry.compiledDefinitions)
    {
        for (BasicFunctionDef::Input& input : definition->functionDef->inputs)
            vm.markValue(input.value);
        for (BasicFunctionDef::Input& output : definition->functionDef->outputs)
            vm.markValue(output.value);
    }
}
