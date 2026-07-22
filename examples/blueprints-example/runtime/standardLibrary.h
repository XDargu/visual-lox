#pragma once

class NodeRegistry;
class VM;

// Registers the built-in compiled nodes and native functions shared by the
// editor, CLI, and tests.
void RegisterStandardLibrary(NodeRegistry& registry);

// Keeps values owned by node definitions alive during VM garbage collection.
void MarkNodeRegistryRoots(NodeRegistry& registry, VM& vm);
