#ifndef loxcpp_vm_h
#define loxcpp_vm_h

#include <vector>
#include <list>
#include <array>
#include <string>
#include <functional>

#include "Chunk.h"
#include "Value.h"
#include "HashTable.h"
#include "Object.h"
#include "Compiler.h"

class Compiler;

enum class InterpretResult 
{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
};

using InstructonPointer = uint8_t*;

struct CallFrame
{
    ObjClosure* closure = nullptr;
    InstructonPointer ip = nullptr;
    Value* slots = nullptr;
};

struct NativeMethodDef
{
    const char* name;
    int arity;
    NativeFn function;
};

using ExternalMarkingFunc = std::function<void()>;

class VM
{
public:

    using GCObjList = std::list<Obj*>;

    VM();
    VM(VM const&) = delete;
    void operator=(VM const&) = delete;

    ~VM()
    {
        freeAllObjects();
    }

    static VM& getInstance()
    {
        static VM instance;
        return instance;
    }

    InterpretResult interpret(const std::string& source);

    Table& stringTable() { return strings; }
    Table& globalTable() { return globals; }

    // Memory. TODO: Separate from the VM
    void addObject(Obj* obj);
    void freeAllObjects();
    void collectGarbage();
    void markRoots();
    void traceReferences();
    void sweep();
    void markObject(Obj* object);
    void markValue(Value& value);
    void markArray(ValueArray& valArray);
    void markCompilerRoots();
    void blackenObject(Obj* object);
    void setExternalMarkingFunc(ExternalMarkingFunc func) { externalMarkingFunc = func; };

    void push(Value value);
    Value pop();
    Value& peek(int distance);

    bool callValue(const Value& callee, uint8_t argCount);

    InterpretResult run(int depth);

    size_t getFrameCount() const { return frameCount; }

    void defineNative(const char* name, uint8_t arity, NativeFn function);
    void defineNativeClass(const char* name, std::vector<NativeMethodDef>&& methods);

    Compiler& getCompiler() { return compiler; }

private:

    void resetStack();
    void runtimeError(const char* format, ...);
    bool validateBinaryOperator();
    void concatenate();

    bool call(ObjClosure* closure, uint8_t argCount);
    bool invokeFromClass(ObjClass* klass, ObjString* name, uint8_t argCount);
    bool invoke(ObjString* name, uint8_t argCount);
    bool bindMethod(ObjInstance* instance, ObjString* name);
    ObjUpvalue* captureUpvalue(Value* local);
    void closeUpvalues(Value* last);
    void defineMethod(ObjString* name);

    Value instanceToString(Value& instanceVal);

    static constexpr size_t STACK_MAX = 256;
    static constexpr size_t FRAMES_MAX = 255;

    std::array<CallFrame, FRAMES_MAX> frames;
    size_t frameCount;
    std::array<Value, STACK_MAX> stack;
    GCObjList objects;
    ObjUpvalue* openUpvalues; // Maybe this could also be a list?
    Value* stackTop;
    Table strings;
    Table globals;
    Compiler compiler;
    bool nativesDefined = false;

    ExternalMarkingFunc externalMarkingFunc;
    std::vector<Obj*> grayNodes;
    size_t bytesAllocated = 0;
    size_t nextGC = 256;
};

#endif