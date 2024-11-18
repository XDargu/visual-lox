#ifndef loxcpp_memory_h
#define loxcpp_memory_h

#include "Common.h"

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

void* reallocate(void* pointer, size_t oldSize, size_t newSize)
{
    if (newSize == 0)
    {
        free(pointer);
        return nullptr;
    }

    void* result = realloc(pointer, newSize);
    return result;
}

#endif