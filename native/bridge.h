// Shared between the render-event entry points and the upscaler backends.
#pragma once
#include <d3d11.h>

// Layout must match KuFrame in plugin/Native.cs.
struct KuFrame {
    ID3D11Resource* color;
    ID3D11Resource* depth;
    ID3D11Resource* motion;
    ID3D11Resource* output;
    int renderWidth, renderHeight, outputWidth, outputHeight;
    float jitterX, jitterY, mvScaleX, mvScaleY, sharpness;
    int reset;
    int quality;
    int createFlags;
    int backend;
    float cameraNear, cameraFar, fovY, frameTimeMs;
};

enum : int { KU_BACKEND_NGX = 0, KU_BACKEND_VULKAN = 1 };
enum : int { KU_STATE_IDLE = 0, KU_STATE_READY = 1, KU_STATE_FAILED = -1 };

void Log(const char* fmt, ...);
void Fail(const char* what, int code);

bool VkInit(ID3D11Device* device);
void VkEvaluate(const KuFrame* f);
void VkShutdown();
