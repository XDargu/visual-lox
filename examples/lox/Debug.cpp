#include <iostream>
#include <iomanip>

#include "Debug.h"
#include "Value.h"
#include "Object.h"

uint32_t longConstant(const Chunk& chunk, size_t offset)
{
    const uint8_t* constantStart = &chunk.code[offset + 1];
    // Interpret the constant as the next 4 elements in the vector
    return *reinterpret_cast<const uint32_t*>(constantStart);
}

size_t simpleInstruction(const std::string& name, size_t offset)
{
    std::cout << name << std::endl;
    return offset + 1;
}

size_t byteInstruction(const char* name, const Chunk& chunk, size_t offset)
{
    const uint8_t slot = chunk.code[offset + 1];
    std::cout << name << " " << +slot << std::endl;
    return offset + 2;
}

size_t dwordInstruction(const char* name, const Chunk& chunk, size_t offset)
{
    const uint32_t slot = longConstant(chunk, offset);

    std::cout << name << " " << +slot << std::endl;
    return offset + 2;
}

size_t jumpInstruction(const char* name, int sign, const Chunk& chunk, size_t offset)
{
    const uint8_t* jumpStart = &chunk.code[offset + 1];
    // Interpret the constant as the next 2 elements in the vector
    uint16_t jump = *reinterpret_cast<const uint16_t*>(jumpStart);

    jump |= chunk.code[offset + 2];

    std::cout << name << " " << +offset << " -> " << (offset + 3 + sign * jump) << std::endl;

    return offset + 3;
}

size_t constantInstruction(const std::string& name, const Chunk& chunk, size_t offset)
{
    const uint8_t constant = chunk.code[offset + 1];

    std::cout << name << "  " << +constant << "  ";
    const Value value = chunk.constants.values[constant];
    printValue(value);

    std::cout << std::endl;
    return offset + 2;
}

size_t constantLongInstruction(const std::string& name, const Chunk& chunk, size_t offset)
{
    const uint32_t constant = longConstant(chunk, offset);

    std::cout << name << "  " << +constant << "  ";
    printValue(chunk.constants.values[constant]);

    std::cout << std::endl;
    return offset + 5;
}

size_t invokeInstruction(const std::string& name, const Chunk& chunk, size_t offset)
{
    uint8_t constant = chunk.code[offset + 1];
    uint8_t argCount = chunk.code[offset + 2];
    std::cout << name << " (" << +argCount << " args) " << +constant << " '";
    printValue(chunk.constants.values[constant]);
    std::cout << "'" << std::endl;
    return offset + 3;
}

size_t invokeLongInstruction(const std::string& name, const Chunk& chunk, size_t offset)
{
    const uint32_t constant = longConstant(chunk, offset);
    uint8_t argCount = chunk.code[offset + 5];
    std::cout << name << " (" << +argCount << " args) " << +constant << " '";
    printValue(chunk.constants.values[constant]);
    std::cout << "'" << std::endl;
    return offset + 6;
}

void disassembleChunk(const Chunk& chunk, const char* name)
{
    std::cout << "==" << name << "==" << std::endl;

    int idx = 0;
    for (const Value& constant : chunk.constants.values)
    {
        std::cout << idx << ": ";
        printValue(constant);
        std::cout << std::endl;
        idx++;
    }

    for (size_t offset = 0; offset < chunk.code.size();)
    {
        offset = disassembleInstruction(chunk, offset);
    }
}

size_t disassembleInstruction(const Chunk& chunk, size_t offset)
{
    std::cout << std::setfill('0') << std::setw(4) << offset << " ";

    if (offset > 0 && chunk.lines[offset] == chunk.lines[offset - 1])
    {
        std::cout << "   | ";
    }
    else
    {
        std::cout << std::setfill('0') << std::setw(4) << chunk.lines[offset] << " ";
    }

    const OpCode instruction = static_cast<OpCode>(chunk.code[offset]);
    switch (instruction)
    {
    case OpCode::OP_CONSTANT:
        return constantInstruction("OP_CONSTANT", chunk, offset);
    case OpCode::OP_CONSTANT_LONG:
        return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
    case OpCode::OP_NIL:
        return simpleInstruction("OP_NIL", offset);
    case OpCode::OP_TRUE:
        return simpleInstruction("OP_TRUE", offset);
    case OpCode::OP_FALSE:
        return simpleInstruction("OP_FALSE", offset);
    case OpCode::OP_POP:
        return simpleInstruction("OP_POP", offset);
    case OpCode::OP_GET_LOCAL:
        return byteInstruction("OP_GET_LOCAL", chunk, offset);
    case OpCode::OP_SET_LOCAL:
        return byteInstruction("OP_SET_LOCAL", chunk, offset);
    case OpCode::OP_GET_LOCAL_LONG:
        return dwordInstruction("OP_GET_LOCAL_LONG", chunk, offset);
    case OpCode::OP_SET_LOCAL_LONG:
        return dwordInstruction("OP_SET_LOCAL_LONG", chunk, offset);
    case OpCode::OP_GET_GLOBAL:
        return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OpCode::OP_DEFINE_GLOBAL:
        return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OpCode::OP_SET_GLOBAL:
        return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case OpCode::OP_GET_GLOBAL_LONG:
        return constantLongInstruction("OP_GET_GLOBAL_LONG", chunk, offset);
    case OpCode::OP_DEFINE_GLOBAL_LONG:
        return constantLongInstruction("OP_DEFINE_GLOBAL_LONG", chunk, offset);
    case OpCode::OP_SET_GLOBAL_LONG:
        return constantLongInstruction("OP_SET_GLOBAL_LONG", chunk, offset);
    case OpCode::OP_GET_UPVALUE:
        return byteInstruction("OP_GET_UPVALUE", chunk, offset);
    case OpCode::OP_SET_UPVALUE:
        return byteInstruction("OP_SET_UPVALUE", chunk, offset);
    case OpCode::OP_GET_PROPERTY:
        return constantInstruction("OP_GET_PROPERTY", chunk, offset);
    case OpCode::OP_SET_PROPERTY:
        return constantInstruction("OP_SET_PROPERTY", chunk, offset);
    case OpCode::OP_GET_PROPERTY_LONG:
        return constantLongInstruction("OP_GET_PROPERTY_LONG", chunk, offset);
    case OpCode::OP_SET_PROPERTY_LONG:
        return constantLongInstruction("OP_SET_PROPERTY_LONG", chunk, offset);
    case OpCode::OP_EQUAL:
        return simpleInstruction("OP_EQUAL", offset);
    case OpCode::OP_MATCH:
        return simpleInstruction("OP_MATCH", offset);
    case OpCode::OP_GREATER:
        return simpleInstruction("OP_GREATER", offset);
    case OpCode::OP_LESS:
        return simpleInstruction("OP_LESS", offset);
    case OpCode::OP_NEGATE:
        return simpleInstruction("OP_NEGATE", offset);
    case OpCode::OP_ADD:
        return simpleInstruction("OP_ADD", offset);
    case OpCode::OP_SUBTRACT:
        return simpleInstruction("OP_SUBTRACT", offset);
    case OpCode::OP_MULTIPLY:
        return simpleInstruction("OP_MULTIPLY", offset);
    case OpCode::OP_DIVIDE:
        return simpleInstruction("OP_DIVIDE", offset);
    case OpCode::OP_MODULO:
        return simpleInstruction("OP_MODULO", offset);
    case OpCode::OP_INCREMENT:
        return simpleInstruction("OP_INCREMENT", offset);
    case OpCode::OP_BUILD_RANGE:
        return simpleInstruction("OP_BUILD_RANGE", offset);
    case OpCode::OP_BUILD_LIST:
        return byteInstruction("OP_BUILD_LIST", chunk, offset);
    case OpCode::OP_INDEX_SUBSCR:
        return simpleInstruction("OP_INDEX_SUBSCR", offset);
    case OpCode::OP_STORE_SUBSCR:
        return simpleInstruction("OP_STORE_SUBSCR", offset);
    case OpCode::OP_RANGE_IN_BOUNDS:
        return simpleInstruction("OP_RANGE_IN_BOUNDS", offset);
    case OpCode::OP_NOT:
        return simpleInstruction("OP_NOT", offset);
    case OpCode::OP_PRINT:
        return simpleInstruction("OP_PRINT", offset);
    case OpCode::OP_JUMP:
        return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case OpCode::OP_JUMP_IF_FALSE:
        return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OpCode::OP_LOOP:
        return jumpInstruction("OP_LOOP", -1, chunk, offset);
    case OpCode::OP_RETURN:
        return simpleInstruction("OP_RETURN", offset);
    case OpCode::OP_CALL:
        return byteInstruction("OP_CALL", chunk, offset);
    case OpCode::OP_INVOKE:
        return invokeInstruction("OP_INVOKE", chunk, offset);
    case OpCode::OP_INVOKE_LONG:
        return invokeLongInstruction("OP_INVOKE_LONG", chunk, offset);
    case OpCode::OP_CLOSURE:
    {
        offset++;
        const uint8_t constant = chunk.code[offset++];
        //std::cout << +constant << " OP_CLOSURE" << std::endl;
        printf("%-16s %4d ", "OP_CLOSURE", constant);
        printValue(chunk.constants.values[constant]);
        printf("\n");

        ObjFunction* function = asFunction(chunk.constants.values[constant]);

        for (size_t j = 0; j < function->upvalueCount; j++)
        {
            const uint8_t isLocal = chunk.code[offset++];
            const uint8_t index = chunk.code[offset++];
            printf("%04d      |                     %s %d\n",
                offset - 2, isLocal ? "local" : "upvalue", index);
        }

        return offset;
    }
    case OpCode::OP_CLOSURE_LONG:
    {
        offset++;
        const uint32_t constant = longConstant(chunk, offset);

        //std::cout << +constant << " OP_CLOSURE" << std::endl;
        printf("%-16s %4d ", "OP_CLOSURE", constant);
        printValue(chunk.constants.values[constant]);
        printf("\n");

        ObjFunction* function = asFunction(chunk.constants.values[constant]);

        for (size_t j = 0; j < function->upvalueCount; j++)
        {
            const uint8_t isLocal = chunk.code[offset++];
            const uint8_t index = chunk.code[offset++];
            printf("%04d      |                     %s %d\n",
                offset - 2, isLocal ? "local" : "upvalue", index);
        }

        return offset;
    }
    case OpCode::OP_CLOSE_UPVALUE:
        return simpleInstruction("OP_CLOSE_UPVALUE", offset);
    case OpCode::OP_CLASS:
        return constantInstruction("OP_CLASS", chunk, offset);
    case OpCode::OP_CLASS_LONG:
        return constantLongInstruction("OP_CLASS_LONG", chunk, offset);
    case OpCode::OP_METHOD:
        return constantInstruction("OP_METHOD", chunk, offset);
    case OpCode::OP_METHOD_LONG:
        return constantLongInstruction("OP_METHOD_LONG", chunk, offset);
    default:
        std::cout << "Unknown opcode " << static_cast<uint8_t>(instruction) << std::endl;
        return offset + 1;
    }

    static_assert(static_cast<int>(OpCode::COUNT) == 54, "Missing operations in the Debug");
}