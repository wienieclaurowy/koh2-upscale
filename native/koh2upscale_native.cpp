// Render-thread bridge from the BepInEx plugin to an NGX (DLSS-style) provider in the process, which in
// practice is OptiScaler turning the calls into FSR. All NGX calls run on Unity's render thread through
// CommandBuffer.IssuePluginEventAndData.
#include <windows.h>
#include <psapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ngx_min.h"

#define EXPORT extern "C" __declspec(dllexport)

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
};

enum : int { KU_EVENT_INIT = 1, KU_EVENT_EVALUATE = 2, KU_EVENT_SHUTDOWN = 3 };
enum : int { KU_STATE_IDLE = 0, KU_STATE_READY = 1, KU_STATE_FAILED = -1 };

static CRITICAL_SECTION g_logLock;
static char g_log[16384];
static size_t g_logLen;

static void Log(const char* fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof line - 2)
        n = sizeof line - 2;
    line[n++] = '\n';
    EnterCriticalSection(&g_logLock);
    if (g_logLen + n < sizeof g_log) {
        memcpy(g_log + g_logLen, line, n);
        g_logLen += n;
    }
    LeaveCriticalSection(&g_logLock);
}

static volatile LONG g_state = KU_STATE_IDLE;
static ID3D11Device* g_device;
static ID3D11DeviceContext* g_context;
static NgxParameter* g_params;
static NgxHandle* g_feature;
static int g_featureKey[6];

static PFN_NgxInit pInit;
static PFN_NgxShutdown1 pShutdown1;
static PFN_NgxGetParams pGetCapabilityParameters;
static PFN_NgxGetParams pAllocateParameters;
static PFN_NgxDestroyParams pDestroyParameters;
static PFN_NgxCreateFeature pCreateFeature;
static PFN_NgxReleaseFeature pReleaseFeature;
static PFN_NgxEvaluateFeature pEvaluateFeature;

static void Fail(const char* what, NgxResult r)
{
    Log("failed: %s (0x%08X); upscaling disabled", what, (unsigned)r);
    InterlockedExchange(&g_state, KU_STATE_FAILED);
}

static HMODULE FindNgxProvider()
{
    HMODULE modules[1024];
    DWORD bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof modules, &bytes))
        return nullptr;
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); i++) {
        if (GetProcAddress(modules[i], "NVSDK_NGX_D3D11_EvaluateFeature")
            && GetProcAddress(modules[i], "NVSDK_NGX_D3D11_Init")) {
            char path[MAX_PATH] = {};
            GetModuleFileNameA(modules[i], path, sizeof path);
            Log("NGX provider: %s", path);
            return modules[i];
        }
    }
    return nullptr;
}

template <typename T>
static bool Resolve(HMODULE m, T& fn, const char* name)
{
    fn = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(m, name)));
    if (!fn)
        Log("missing export %s", name);
    return fn != nullptr;
}

// Every Get writes through a 16-byte zeroed buffer, so a wrong slot guess cannot overrun the stack.
static bool SelfTestParams(NgxParameter* p)
{
    const NgxParamVtbl* v = p->vtbl;
    alignas(16) unsigned char buf[16];
    bool ok = true;

    v->SetI(p, "KoH2Upscale.Test.I", -7);
    memset(buf, 0, sizeof buf);
    v->GetI(p, "KoH2Upscale.Test.I", reinterpret_cast<int*>(buf));
    ok &= *reinterpret_cast<int*>(buf) == -7;

    v->SetF(p, "KoH2Upscale.Test.F", 1.5f);
    memset(buf, 0, sizeof buf);
    v->GetF(p, "KoH2Upscale.Test.F", reinterpret_cast<float*>(buf));
    ok &= *reinterpret_cast<float*>(buf) == 1.5f;

    v->SetULL(p, "KoH2Upscale.Test.ULL", 0x1122334455667788ull);
    memset(buf, 0, sizeof buf);
    v->GetULL(p, "KoH2Upscale.Test.ULL", reinterpret_cast<unsigned long long*>(buf));
    ok &= *reinterpret_cast<unsigned long long*>(buf) == 0x1122334455667788ull;

    v->SetD(p, "KoH2Upscale.Test.D", 2.25);
    memset(buf, 0, sizeof buf);
    v->GetD(p, "KoH2Upscale.Test.D", reinterpret_cast<double*>(buf));
    ok &= *reinterpret_cast<double*>(buf) == 2.25;

    void* marker = reinterpret_cast<void*>(0x12345678abcull);
    v->SetVoidPtr(p, "KoH2Upscale.Test.P", marker);
    memset(buf, 0, sizeof buf);
    v->GetVoidPtr(p, "KoH2Upscale.Test.P", reinterpret_cast<void**>(buf));
    ok &= *reinterpret_cast<void**>(buf) == marker;

    Log("parameter vtable self-test: %s", ok ? "pass" : "FAIL");
    return ok;
}

static void Init(KuFrame* f)
{
    if (!f || !f->color) {
        Fail("init without a texture", 0);
        return;
    }
    f->color->GetDevice(&g_device);
    g_device->GetImmediateContext(&g_context);

    HMODULE ngx = FindNgxProvider();
    if (!ngx) {
        Fail("no module exports NVSDK_NGX_D3D11_*; is OptiScaler loaded?", 0);
        return;
    }
    if (!Resolve(ngx, pInit, "NVSDK_NGX_D3D11_Init") || !Resolve(ngx, pShutdown1, "NVSDK_NGX_D3D11_Shutdown1")
        || !Resolve(ngx, pGetCapabilityParameters, "NVSDK_NGX_D3D11_GetCapabilityParameters")
        || !Resolve(ngx, pAllocateParameters, "NVSDK_NGX_D3D11_AllocateParameters")
        || !Resolve(ngx, pDestroyParameters, "NVSDK_NGX_D3D11_DestroyParameters")
        || !Resolve(ngx, pCreateFeature, "NVSDK_NGX_D3D11_CreateFeature")
        || !Resolve(ngx, pReleaseFeature, "NVSDK_NGX_D3D11_ReleaseFeature")
        || !Resolve(ngx, pEvaluateFeature, "NVSDK_NGX_D3D11_EvaluateFeature")) {
        Fail("resolve NGX exports", 0);
        return;
    }

    NgxResult r = pInit(0x4B6F48325570ull, L".", g_device, nullptr, NGX_VERSION_API);
    if (NgxFailed(r)) {
        Fail("NVSDK_NGX_D3D11_Init", r);
        return;
    }
    NgxParameter* caps = nullptr;
    r = pGetCapabilityParameters(&caps);
    if (NgxFailed(r) || !caps) {
        Fail("GetCapabilityParameters", r);
        return;
    }
    if (!SelfTestParams(caps)) {
        Fail("parameter vtable layout", 0);
        return;
    }
    int available = 0;
    caps->vtbl->GetI(caps, "SuperSampling.Available", &available);
    Log("SuperSampling.Available = %d", available);
    if (!available) {
        Fail("super sampling not available", 0);
        return;
    }
    r = pAllocateParameters(&g_params);
    if (NgxFailed(r) || !g_params) {
        Fail("AllocateParameters", r);
        return;
    }
    InterlockedExchange(&g_state, KU_STATE_READY);
    Log("NGX ready");
}

static bool EnsureFeature(const KuFrame* f)
{
    int key[6] = { f->renderWidth, f->renderHeight, f->outputWidth, f->outputHeight, f->quality, f->createFlags };
    if (g_feature && memcmp(key, g_featureKey, sizeof key) == 0)
        return true;
    if (g_feature) {
        pReleaseFeature(g_feature);
        g_feature = nullptr;
    }
    const NgxParamVtbl* v = g_params->vtbl;
    v->SetUI(g_params, "CreationNodeMask", 1);
    v->SetUI(g_params, "VisibilityNodeMask", 1);
    v->SetUI(g_params, "Width", f->renderWidth);
    v->SetUI(g_params, "Height", f->renderHeight);
    v->SetUI(g_params, "OutWidth", f->outputWidth);
    v->SetUI(g_params, "OutHeight", f->outputHeight);
    v->SetI(g_params, "PerfQualityValue", f->quality);
    v->SetI(g_params, "DLSS.Feature.Create.Flags", f->createFlags);
    NgxResult r = pCreateFeature(g_context, NGX_FEATURE_SUPERSAMPLING, g_params, &g_feature);
    if (NgxFailed(r) || !g_feature) {
        g_feature = nullptr;
        Fail("CreateFeature", r);
        return false;
    }
    memcpy(g_featureKey, key, sizeof key);
    Log("feature created: %dx%d -> %dx%d, quality %d, flags 0x%X", f->renderWidth, f->renderHeight,
        f->outputWidth, f->outputHeight, f->quality, f->createFlags);
    return true;
}

static void Evaluate(KuFrame* f)
{
    if (g_state != KU_STATE_READY || !f || !EnsureFeature(f))
        return;
    const NgxParamVtbl* v = g_params->vtbl;
    v->SetD3D11(g_params, "Color", f->color);
    v->SetD3D11(g_params, "Output", f->output);
    v->SetD3D11(g_params, "Depth", f->depth);
    v->SetD3D11(g_params, "MotionVectors", f->motion);
    v->SetF(g_params, "Jitter.Offset.X", f->jitterX);
    v->SetF(g_params, "Jitter.Offset.Y", f->jitterY);
    v->SetF(g_params, "MV.Scale.X", f->mvScaleX);
    v->SetF(g_params, "MV.Scale.Y", f->mvScaleY);
    v->SetF(g_params, "Sharpness", f->sharpness);
    v->SetI(g_params, "Reset", f->reset);
    v->SetUI(g_params, "DLSS.Render.Subrect.Dimensions.Width", f->renderWidth);
    v->SetUI(g_params, "DLSS.Render.Subrect.Dimensions.Height", f->renderHeight);
    NgxResult r = pEvaluateFeature(g_context, g_feature, g_params, nullptr);
    if (NgxFailed(r))
        Fail("EvaluateFeature", r);
}

static void Shutdown()
{
    if (g_feature && pReleaseFeature)
        pReleaseFeature(g_feature);
    g_feature = nullptr;
    if (g_params && pDestroyParameters)
        pDestroyParameters(g_params);
    g_params = nullptr;
    if (g_device && pShutdown1 && g_state == KU_STATE_READY)
        pShutdown1(g_device);
    if (g_context)
        g_context->Release();
    if (g_device)
        g_device->Release();
    g_context = nullptr;
    g_device = nullptr;
    InterlockedExchange(&g_state, KU_STATE_IDLE);
    Log("NGX shut down");
}

static void __stdcall OnRenderEvent(int eventId, void* data)
{
    KuFrame* f = static_cast<KuFrame*>(data);
    switch (eventId) {
    case KU_EVENT_INIT:
        if (g_state == KU_STATE_IDLE)
            Init(f);
        break;
    case KU_EVENT_EVALUATE:
        Evaluate(f);
        break;
    case KU_EVENT_SHUTDOWN:
        Shutdown();
        break;
    }
}

EXPORT void* KU_GetRenderEventFunc() { return reinterpret_cast<void*>(&OnRenderEvent); }

EXPORT int KU_GetState() { return g_state; }

// Copies queued log lines into buf and clears the queue; returns the byte count.
EXPORT int KU_PopLog(char* buf, int size)
{
    EnterCriticalSection(&g_logLock);
    int n = (int)g_logLen < size ? (int)g_logLen : size;
    memcpy(buf, g_log, n);
    memmove(g_log, g_log + n, g_logLen - n);
    g_logLen -= n;
    LeaveCriticalSection(&g_logLock);
    return n;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        InitializeCriticalSection(&g_logLock);
    return TRUE;
}
