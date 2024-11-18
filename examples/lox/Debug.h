#ifndef loxcpp_debug_h
#define loxcpp_debug_h

#include "Chunk.h"

void disassembleChunk(const Chunk& chunk, const char* name);
size_t disassembleInstruction(const Chunk& chunk, size_t offset);

#endif
