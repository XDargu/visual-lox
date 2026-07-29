#pragma once

#include <imgui.h>
#include <Value.h>

#include <functional>
#include <string>
#include <unordered_map>

class NodeRegistry;
class VM;
struct ObjList;

struct VisualApplicationTextureCallbacks
{
    std::function<ImTextureID(const void*, int, int)> create;
    std::function<void(ImTextureID)> destroy;
};

class VisualApplicationContext
{
public:
    explicit VisualApplicationContext(VisualApplicationTextureCallbacks textureCallbacks);
    ~VisualApplicationContext();

    VisualApplicationContext(const VisualApplicationContext&) = delete;
    VisualApplicationContext& operator=(const VisualApplicationContext&) = delete;

    void BeginFrame();
    void EndFrame();
    bool IsFrameActive() const;
    bool SetUpdateFunction(const Value& function);
    bool HasUpdateFunction() const;
    const Value& GetUpdateFunction() const;
    void MarkRoots(VM& vm);
    void DrawImage(const std::string& id, ObjList* values, int width, int height, double maximum, double revision, int scale = 1);

private:
    struct CachedTexture
    {
        ImTextureID texture = nullptr;
        ObjList* source = nullptr;
        int width = 0;
        int height = 0;
        double revision = 0.0;
    };

    VisualApplicationTextureCallbacks callbacks;
    std::unordered_map<std::string, CachedTexture> textures;
    Value updateFunction;
    bool frameActive = false;
};

void RegisterVisualApplicationLibrary(NodeRegistry& registry);
