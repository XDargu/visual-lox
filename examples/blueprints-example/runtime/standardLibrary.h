#pragma once

#include <functional>

class NodeRegistry;
class VM;

// Registers the built-in compiled nodes and native functions shared by the
// editor, CLI, and tests.
void RegisterStandardLibrary(NodeRegistry& registry);

// Keeps values owned by node definitions alive during VM garbage collection.
void MarkNodeRegistryRoots(NodeRegistry& registry, VM& vm);

// Timer callbacks are dispatched on the VM thread. Visual applications pump
// once per frame; command-line programs can run the loop until no timers remain.
void MarkStandardLibraryTimerRoots(VM& vm);
bool PumpStandardLibraryTimers(VM& vm);
bool HasPendingStandardLibraryTimers(VM& vm);
double SecondsUntilNextStandardLibraryTimer(VM& vm);
bool RunStandardLibraryTimers(VM& vm, const std::function<bool()>& shouldStop = {});
void ClearStandardLibraryTimers(VM& vm);
