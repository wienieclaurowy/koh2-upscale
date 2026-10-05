// Minimal NGX D3D11 interface: the subset of the DLSS SDK ABI this plugin calls.
// Values match DLSS SDK 310.x nvsdk_ngx_defs.h / nvsdk_ngx_params.h.
#pragma once
#include <d3d11.h>
#include <stdint.h>

typedef int NgxResult;
static inline bool NgxFailed(NgxResult r) { return ((unsigned)r & 0xFFF00000u) == 0xBAD00000u; }

enum : int { NGX_VERSION_API = 0x15, NGX_FEATURE_SUPERSAMPLING = 1 };

enum : int {
    NGX_PERFQ_MAXPERF = 0, NGX_PERFQ_BALANCED, NGX_PERFQ_MAXQUALITY,
    NGX_PERFQ_ULTRAPERF, NGX_PERFQ_ULTRAQUALITY, NGX_PERFQ_DLAA,
};

enum : int {
    NGX_DLSS_FLAG_IS_HDR = 1 << 0,
    NGX_DLSS_FLAG_MV_LOW_RES = 1 << 1,
    NGX_DLSS_FLAG_MV_JITTERED = 1 << 2,
    NGX_DLSS_FLAG_DEPTH_INVERTED = 1 << 3,
    NGX_DLSS_FLAG_AUTO_EXPOSURE = 1 << 6,
};

struct NgxHandle { unsigned int id; };

// NVSDK_NGX_Parameter is an MSVC-compiled C++ interface. MSVC groups overloaded virtuals and lays each
// group out in reverse declaration order, so this table differs from what g++ would build from the SDK
// header. NgxParams::SelfTest checks the guess at runtime before anything relies on it.
struct NgxParamVtbl {
    void (*SetVoidPtr)(void* self, const char* name, void* v);
    void (*SetD3D12)(void* self, const char* name, void* v);
    void (*SetD3D11)(void* self, const char* name, ID3D11Resource* v);
    void (*SetI)(void* self, const char* name, int v);
    void (*SetUI)(void* self, const char* name, unsigned int v);
    void (*SetD)(void* self, const char* name, double v);
    void (*SetF)(void* self, const char* name, float v);
    void (*SetULL)(void* self, const char* name, unsigned long long v);
    NgxResult (*GetVoidPtr)(void* self, const char* name, void** v);
    NgxResult (*GetD3D12)(void* self, const char* name, void** v);
    NgxResult (*GetD3D11)(void* self, const char* name, ID3D11Resource** v);
    NgxResult (*GetI)(void* self, const char* name, int* v);
    NgxResult (*GetUI)(void* self, const char* name, unsigned int* v);
    NgxResult (*GetD)(void* self, const char* name, double* v);
    NgxResult (*GetF)(void* self, const char* name, float* v);
    NgxResult (*GetULL)(void* self, const char* name, unsigned long long* v);
    void (*Reset)(void* self);
};

struct NgxParameter { const NgxParamVtbl* vtbl; };

typedef NgxResult (*PFN_NgxInit)(unsigned long long appId, const wchar_t* dataPath, ID3D11Device* device, const void* featureInfo, int sdkVersion);
typedef NgxResult (*PFN_NgxShutdown1)(ID3D11Device* device);
typedef NgxResult (*PFN_NgxGetParams)(NgxParameter** out);
typedef NgxResult (*PFN_NgxDestroyParams)(NgxParameter* params);
typedef NgxResult (*PFN_NgxCreateFeature)(ID3D11DeviceContext* ctx, int feature, NgxParameter* params, NgxHandle** out);
typedef NgxResult (*PFN_NgxReleaseFeature)(NgxHandle* handle);
typedef NgxResult (*PFN_NgxEvaluateFeature)(ID3D11DeviceContext* ctx, const NgxHandle* handle, const NgxParameter* params, void* callback);
