project(lox)

set(_Lox_Sources
    Chunk.h
    Chunk.cpp
    Common.h
    Compiler.h
    Compiler.cpp
    Debug.h
    Debug.cpp
    HashTable.h
    HashTable.cpp
    Memory.h
    Natives.h
    Natives.cpp
    Object.h
    Object.cpp
    Scanner.h
    Scanner.cpp
    Value.h
    Value.cpp
    Vm.h
    Vm.cpp
    VMUtils.h
)

add_library(lox STATIC)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_Lox_Sources})

target_sources(lox PRIVATE ${_Lox_Sources})

target_include_directories(lox PUBLIC ${CMAKE_CURRENT_LIST_DIR})

set_property(TARGET lox PROPERTY FOLDER "examples")