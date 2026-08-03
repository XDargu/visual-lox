#ifndef loxcpp_vm_h
#define loxcpp_vm_h

#include <vector>
#include <list>
#include <array>
#include <atomic>
#include <string>
#include <functional>

#include "Chunk.h"
#include "Value.h"
#include "HashTable.h"
#include "Object.h"
#include "Compiler.h"

class Compiler;
class ScopedGcRoot;

enum class InterpretResult 
{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
    INTERPRET_PAUSED
};

struct VmDebugCallFrame
{
    std::string functionName;
    std::string qualifiedName;
    std::string functionIdentity;
    size_t instructionOffset = 0;
};

class VM;

class VmDebugHandler
{
public:
    virtual ~VmDebugHandler() = default;
    bool WantsBreakpoints() const { return wantsBreakpoints.load(std::memory_order_relaxed); }
    bool WantsValues() const { return wantsValues.load(std::memory_order_relaxed); }
    virtual bool OnBreakpoint(uint32_t probeId, const VM& vm) = 0;
    virtual void OnValue(uint32_t probeId, const Value& value) = 0;

protected:
    void SetWantsBreakpoints(bool enabled) { wantsBreakpoints.store(enabled, std::memory_order_relaxed); }
    void SetWantsValues(bool enabled) { wantsValues.store(enabled, std::memory_order_relaxed); }

private:
    std::atomic<bool> wantsBreakpoints{ false };
    std::atomic<bool> wantsValues{ false };
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
    void addObject(Obj* obj, size_t allocationSize);
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
    void allowGarbageCollection(bool isAllowed) { canCollectGarbage = isAllowed; }
    bool isGarbageCollectionAllowed() const { return canCollectGarbage; }

    void push(Value value);
    Value pop();
    Value& peek(int distance);

    bool callValue(const Value& callee, uint8_t argCount);

    InterpretResult run(int depth, bool allowPause = true);

    size_t getFrameCount() const { return frameCount; }
    size_t getStackSize() const { return static_cast<size_t>(stackTop - stack.data()); }
    size_t getAllocatedBytes() const { return bytesAllocated; }
    std::vector<VmDebugCallFrame> getDebugCallStack() const;
    void setDebugHandler(VmDebugHandler* handler) { debugHandler = handler; }
    VmDebugHandler* getDebugHandler() const { return debugHandler; }
    void requestStop() { stopRequested.store(true, std::memory_order_relaxed); }
    void clearStopRequest() { stopRequested.store(false, std::memory_order_relaxed); }

    void defineNative(const char* name, uint8_t arity, NativeFn function);
    void defineNativeClass(const char* name, std::vector<NativeMethodDef>&& methods);

    Compiler& getCompiler() { return compiler; }

    void resetStack();
private:
    friend class ScopedGcRoot;

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

    Value instanceToString(const Value& instanceVal);
    ObjString* valueToStringWithOverrides(const Value& value);
    void printValueWithOverrides(const Value& value);
    void pushTemporaryRoot(Value value);
    void popTemporaryRoot();

    static constexpr size_t STACK_MAX = 256;
    static constexpr size_t FRAMES_MAX = 255;
    static constexpr size_t MINIMUM_GC_THRESHOLD = 1024 * 1024;

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
    bool canCollectGarbage = true;
    VmDebugHandler* debugHandler = nullptr;
    bool debugPausePending = false;
    std::atomic<bool> stopRequested{ false };

    ExternalMarkingFunc externalMarkingFunc;
    std::vector<Obj*> grayNodes;
    std::vector<Value> temporaryRoots;
    size_t bytesAllocated = 0;
    size_t nextGC = MINIMUM_GC_THRESHOLD;
};

class ScopedGcRoot
{
public:
    ScopedGcRoot(VM& vm, Value value);
    ~ScopedGcRoot();

    ScopedGcRoot(const ScopedGcRoot&) = delete;
    ScopedGcRoot& operator=(const ScopedGcRoot&) = delete;
    ScopedGcRoot(ScopedGcRoot&&) = delete;
    ScopedGcRoot& operator=(ScopedGcRoot&&) = delete;

private:
    VM& vm;
};

#endif
