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
#include "../native/nodes/switch.h"
#include "../native/nodes/while.h"

void RegisterStandardLibrary(NodeRegistry& registry)
{
    VM& vm = VM::getInstance();
    const bool wasGcAllowed = vm.isGarbageCollectionAllowed();
    vm.allowGarbageCollection(false);

    registry.RegisterCompiledNode("Flow::Branch", &BuildBranchNode,
        { { "Condition", Value(false) } }, {}, NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Chooses one of two execution paths from a condition",
            { "The condition that selects True or False" },
            {}
        });
    registry.RegisterCompiledNode("Flow::For In", &BuildForInNode,
        { { "Iterable", Value(newList()), -1,
            TypeRef::Iterable(TypeRef::Variable("T")),
            "The list, range, or string to iterate." } },
        { { "Value", Value(), -1, TypeRef::Variable("T"),
            "The value at the current iteration." } },
        NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Runs a body once for every value in a list, range, or string",
            { "The list, range, or string to iterate" },
            { "The value for the current iteration" }
        });
    registry.RegisterCompiledNode("Flow::While", &BuildWhileNode,
        { { "Condition", Value(false) } }, {}, NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Repeats a body while a condition remains true",
            { "The condition checked before each iteration" },
            {}
        });
    registry.RegisterCompiledNode("Flow::Repeat", &BuildRepeatNode,
        { { "Count", Value(1.0) } }, { { "Index", Value(0.0) } },
        NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Runs a body a fixed number of times",
            { "How many times to run the body" },
            { "The zero-based index of the current iteration" }
        });
    registry.RegisterCompiledNode("Debug::Print", &BuildPrintNode, { { "Value", Value() } }, {}, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Writes a value to the program output",
            { "The value to display" },
            {}
        });
    registry.RegisterCompiledNode("String::Append", &CreateAppendNode,
        { { "A", Value(takeString("", 0)) },
          { "B", Value(takeString("", 0)) } },
        { { "Result", Value(takeString("", 0)) } },
        NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::ReadOnly |
            NodeDefinitionFlags::Pure,
        { 2, 16, PinType::Any, Value(takeString("", 0)) },
        NodeDocumentation{
            "Combines two or more strings in order",
            { "The first piece of text", "The next piece of text" },
            { "The combined text" },
            "Another piece of text to append"
        });
    registry.RegisterCompiledNode("Math::Add", &CreateAddNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        { 2, 16, PinType::Float, Value(0.0) },
        NodeDocumentation{
            "Adds two or more numbers",
            { "The first addend", "The second addend" },
            { "The sum" },
            "Another number to add"
        });
    registry.RegisterCompiledNode("Math::Subtract", &CreateSubtractNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        { 2, 16, PinType::Float, Value(0.0) },
        NodeDocumentation{
            "Subtracts each following number from the running result",
            { "The number to subtract from", "The first number to subtract" },
            { "The difference" },
            "Another number to subtract"
        });
    registry.RegisterCompiledNode("Math::Multiply", &CreateMultiplyNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        { 2, 16, PinType::Float, Value(1.0) },
        NodeDocumentation{
            "Multiplies two or more numbers",
            { "The first factor", "The second factor" },
            { "The product" },
            "Another factor to multiply"
        });
    registry.RegisterCompiledNode("Math::Divide", &CreateDivideNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Divides the first number by the second",
            { "The dividend", "The divisor" },
            { "The quotient" }
        });
    registry.RegisterCompiledNode("Math::Min", &CreateMinNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        { 2, 16, PinType::Float, Value(0.0) },
        NodeDocumentation{
            "Returns the smallest of two or more numbers",
            { "The first number to compare", "The second number to compare" },
            { "The smallest number" },
            "Another number to compare"
        });
    registry.RegisterCompiledNode("Math::Max", &CreateMaxNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        { 2, 16, PinType::Float, Value(0.0) },
        NodeDocumentation{
            "Returns the largest of two or more numbers",
            { "The first number to compare", "The second number to compare" },
            { "The largest number" },
            "Another number to compare"
        });
    registry.RegisterCompiledNode("Math::Greater Than", &CreateGreaterNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(false) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether the first number is greater than the second",
            { "The first number", "The number compared against A" },
            { "True when A is greater than B" }
        });
    registry.RegisterCompiledNode("Math::Less Than", &CreateLessNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(false) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether the first number is less than the second",
            { "The first number", "The number compared against A" },
            { "True when A is less than B" }
        });
    registry.RegisterCompiledNode("Math::Equals", &CreateEqualsNode,
        { { "A", Value(), -1, TypeRef::Variable("T"), "The first value." },
          { "B", Value(), -1, TypeRef::Variable("T"), "The value to compare with A." } },
        { { "Result", Value(false), -1, "True when A and B are equal." } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether two values are equal",
            { "The first value", "The value compared with A" },
            { "True when A and B are equal" }
        });
    registry.RegisterCompiledNode("Math::Modulo", &CreateModuloNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(0.0) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the remainder after dividing two numbers",
            { "The dividend", "The divisor" },
            { "The remainder" }
        });
    registry.RegisterCompiledNode("Math::Negate", &BuildNegateNode,
        { { "Value", Value(0.0) } }, { { "Result", Value(0.0) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Reverses the sign of a number",
            { "The number to negate" },
            { "The negated number" }
        });
    registry.RegisterCompiledNode("Math::Not Equals", &BuildNotEqualsNode,
        { { "A", Value(), -1, TypeRef::Variable("T") },
          { "B", Value(), -1, TypeRef::Variable("T") } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether two values are different",
            { "The first value", "The value compared with A" },
            { "True when A and B are different" }
        });
    registry.RegisterCompiledNode("Math::Greater Or Equal", &BuildGreaterOrEqualNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether the first number is at least the second",
            { "The first number", "The number compared against A" },
            { "True when A is greater than or equal to B" }
        });
    registry.RegisterCompiledNode("Math::Less Or Equal", &BuildLessOrEqualNode,
        { { "A", Value(0.0) }, { "B", Value(0.0) } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether the first number is at most the second",
            { "The first number", "The number compared against A" },
            { "True when A is less than or equal to B" }
        });
    registry.RegisterCompiledNode("Logic::Not", &BuildNotNode,
        { { "Value", Value(false) } }, { { "Result", Value(false) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Reverses a boolean value",
            { "The boolean to reverse" },
            { "The opposite boolean" }
        });
    registry.RegisterCompiledNode("Logic::And", &BuildAndNode,
        { { "A", Value(false) }, { "B", Value(false) } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        { 2, 16, PinType::Bool, Value(true) },
        NodeDocumentation{
            "Returns true only when every input is true",
            { "The first condition", "The second condition" },
            { "True when every condition is true" },
            "Another condition that must be true"
        });
    registry.RegisterCompiledNode("Logic::Or", &BuildOrNode,
        { { "A", Value(false) }, { "B", Value(false) } },
        { { "Result", Value(false) } }, NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        { 2, 16, PinType::Bool, Value(false) },
        NodeDocumentation{
            "Returns true when any input is true",
            { "The first condition", "The second condition" },
            { "True when any condition is true" },
            "Another condition that may be true"
        });
    registry.RegisterCompiledNode("Value::Is Nil", &BuildIsNilNode,
        { { "Value", Value() } }, { { "Result", Value(false) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Checks whether a value is nil",
            { "The value to inspect" },
            { "True when Value is nil" }
        });
    registry.RegisterCompiledNode("String::ToString", &BuildToStringNode,
        { { "Value", Value() } }, { { "Result", Value(copyString("", 0)) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{ "Converts any value to text", { "The value to convert" }, { "The text representation of Value" } });
    registry.RegisterCompiledNode("Value::Coalesce", &BuildCoalesceNode,
        { { "Value", Value(), -1, TypeRef::Variable("T") },
          { "Fallback", Value(), -1, TypeRef::Variable("T") } },
        { { "Result", Value(), -1, TypeRef::Variable("T") } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Uses a fallback value when the preferred value is nil",
            { "The preferred value", "The value used when Value is nil" },
            { "Value when non-nil; otherwise Fallback" }
        });

    registry.RegisterCompiledNode("Flow::Match", &BuildMatchFlowNode,
        { { "Value", Value() }, { "Pattern 1", Value(0.0) } }, {},
        NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Chooses the first execution path whose pattern equals a value",
            { "The value to compare", "The first pattern to test" },
            {},
            "Another pattern to compare with Value"
        });
    registry.RegisterCompiledNode("Flow::Switch", &BuildSwitchFlowNode,
        { { "Condition 1", Value(false) } }, {},
        NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Chooses the first execution path whose condition is true",
            { "The first condition to evaluate" },
            {},
            "Another condition to evaluate in order"
        });
    registry.RegisterCompiledNode("Range::Make", &BuildRangeNode,
        { { "From", Value(0.0) }, { "To", Value(1.0) } },
        { { "Range", Value(newRange(0.0, 1.0)) } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Creates an inclusive numeric range between two bounds",
            { "The starting number", "The ending number" },
            { "The resulting range" }
        });
    registry.RegisterCompiledNode("List::Get By Index", &BuildListGetByIndexNode,
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) }, { "Index", Value(0.0) } },
        { { "Value", Value(), -1, TypeRef::Variable("T") } },
        NodeDefinitionFlags::ReadOnly | NodeDefinitionFlags::Pure,
        NodeDocumentation{
            "Returns the list value at an index",
            { "The list to read", "The zero-based index to read" },
            { "The value at Index, or nil when the index is invalid" }
        });
    registry.RegisterCompiledNode("List::Set By Index", &BuildListSetByIndexNode,
        { { "List", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) }, { "Index", Value(0.0) },
          { "Value", Value(), -1, TypeRef::Variable("T") } },
        { { "Value", Value(newList()), -1,
            TypeRef::List(TypeRef::Variable("T")) } }, NodeDefinitionFlags::None,
        NodeDocumentation{
            "Replaces the list value at an index",
            { "The list to modify", "The zero-based index to update", "The replacement value" },
            { "The modified list" }
        });

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
        if (HasFlag(definition.functionDef->flags, NodeDefinitionFlags::DynamicInputs))
            vm.markValue(definition.functionDef->dynamicInputProps.defaultValue);
    }

    for (CompiledNodeDefPtr& definition : registry.compiledDefinitions)
    {
        for (BasicFunctionDef::Input& input : definition->functionDef->inputs)
            vm.markValue(input.value);
        for (BasicFunctionDef::Input& output : definition->functionDef->outputs)
            vm.markValue(output.value);
        if (HasFlag(definition->functionDef->flags, NodeDefinitionFlags::DynamicInputs))
            vm.markValue(definition->functionDef->dynamicInputProps.defaultValue);
    }
}
