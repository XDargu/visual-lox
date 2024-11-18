#include "Chunk.h"

#include "Vm.h"

Chunk::Chunk()
{
#ifdef FORCE_LONG_OPS
    for (int i = 0; i < 300; ++i)
    {
        addConstant(Value((double)i));
    }
#endif
}

void Chunk::write(OpCode byte, int line)
{
    code.push_back(static_cast<uint8_t>(byte));
    lines.push_back(line);
}

void Chunk::write(uint8_t byte, int line)
{
    code.push_back(byte);
    lines.push_back(line);
}

uint32_t Chunk::addConstant(Value value)
{
    VM::getInstance().push(value);
    auto result = std::find(constants.values.begin(), constants.values.end(), value);
    if (result != constants.values.end())
    {
        VM::getInstance().pop();
        return static_cast<uint32_t>(std::distance(constants.values.begin(), result));
    }

    constants.values.push_back(value);
    VM::getInstance().pop();
    return static_cast<uint32_t>(constants.values.size() - 1);
}
