// FSR 3.1 on DXVK's own Vulkan device and queue, through DXVK's IDXGIVkInteropDevice. Unlike a
// D3D11-on-D3D12 bridge there is no second device, so no cross-device fences or texture copies.
#include <windows.h>
#include <string.h>
#include <wchar.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "third_party/ffx_api/ffx_api.h"
#include "third_party/ffx_api/ffx_upscale.h"
#include "third_party/ffx_api/vk/ffx_api_vk.h"
#include "bridge.h"

// DXVK interop interfaces (dxvk src/dxgi/dxgi_interfaces.h). Plain COM, so g++ vtables match.
struct IDXGIVkInteropDevice;

struct IDXGIVkInteropSurface : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDXGIVkInteropDevice** device) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(VkImage* image, VkImageLayout* layout, VkImageCreateInfo* info) = 0;
};

struct IDXGIVkInteropDevice : public IUnknown {
    virtual void STDMETHODCALLTYPE GetVulkanHandles(VkInstance* instance, VkPhysicalDevice* physical, VkDevice* device) = 0;
    virtual void STDMETHODCALLTYPE GetSubmissionQueue(VkQueue* queue, uint32_t* family) = 0;
    virtual void STDMETHODCALLTYPE TransitionSurfaceLayout(IDXGIVkInteropSurface* surface,
        const VkImageSubresourceRange* range, VkImageLayout oldLayout, VkImageLayout newLayout) = 0;
    virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;
    virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;
    virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;
};

static const GUID IID_InteropSurface = { 0x5546cf8c, 0x77e7, 0x4341, { 0xb0, 0x5d, 0x8d, 0x4d, 0x50, 0x00, 0xe7, 0x7d } };
static const GUID IID_InteropDevice = { 0xe2ef5fa5, 0xdc21, 0x4af7, { 0x90, 0xc4, 0xf6, 0x7e, 0xf6, 0xa0, 0x93, 0x23 } };

namespace {

const int Slots = 3;

struct {
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
} vk;

struct {
    PfnFfxCreateContext CreateContext;
    PfnFfxDestroyContext DestroyContext;
    PfnFfxDispatch Dispatch;
} ffx;

IDXGIVkInteropDevice* interop;
VkPhysicalDevice physical;
VkDevice device;
VkQueue queue;
uint32_t queueFamily;
VkCommandPool pool;
VkCommandBuffer cmds[Slots];
VkFence fences[Slots];
unsigned frame;
ffxContext context;
int contextKey[5];

struct Image {
    IDXGIVkInteropSurface* surface;
    VkImage image;
    VkImageLayout layout;
    VkImageCreateInfo info;
};

// FFX asks for KHR aliases such as vkGetBufferMemoryRequirements2KHR and calls them unchecked; DXVK
// does not enable those extensions, so the loader returns NULL. The core 1.1+ entry points are identical.
PFN_vkVoidFunction VKAPI_CALL DeviceProcAddrWithCoreFallback(VkDevice dev, const char* name)
{
    PFN_vkVoidFunction fn = vk.GetDeviceProcAddr(dev, name);
    size_t len = strlen(name);
    if (!fn && len > 3 && len < 128 && strcmp(name + len - 3, "KHR") == 0) {
        char core[128];
        memcpy(core, name, len - 3);
        core[len - 3] = 0;
        fn = vk.GetDeviceProcAddr(dev, core);
    }
    if (!fn)
        Log("Vulkan function %s unavailable", name);
    return fn;
}

void FfxMessage(uint32_t type, const wchar_t* message)
{
    char text[512];
    WideCharToMultiByte(CP_UTF8, 0, message, -1, text, sizeof text, nullptr, nullptr);
    Log("ffx %s: %s", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", text);
}

template <typename T>
bool DeviceFn(T& fn, const char* name)
{
    fn = reinterpret_cast<T>(vk.GetDeviceProcAddr(device, name));
    if (!fn)
        Log("missing Vulkan function %s", name);
    return fn != nullptr;
}

template <typename T>
bool ModuleFn(HMODULE m, T& fn, const char* name)
{
    fn = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(m, name)));
    if (!fn)
        Log("missing export %s", name);
    return fn != nullptr;
}

// amd_fidelityfx_vk.dll ships next to this DLL.
HMODULE LoadFfx()
{
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LoadFfx), &self);
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    while (n > 0 && path[n - 1] != L'\\' && path[n - 1] != L'/')
        n--;
    path[n] = 0;
    wcsncat(path, L"amd_fidelityfx_vk.dll", MAX_PATH - n - 1);
    return LoadLibraryW(path);
}

bool GetImage(ID3D11Resource* resource, Image& out)
{
    out = {};
    if (!resource || FAILED(resource->QueryInterface(IID_InteropSurface, reinterpret_cast<void**>(&out.surface))))
        return false;
    out.info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    return SUCCEEDED(out.surface->GetVulkanImageInfo(&out.image, &out.layout, &out.info));
}

// Built by hand: ffxApiGetImageResourceDescriptionVK treats every mutable-format image as sRGB, and
// DXVK makes Unity's typeless render textures mutable.
FfxApiResource Resource(const Image& img, uint32_t usage)
{
    FfxApiResourceDescription desc = {};
    desc.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    desc.format = ffxApiGetSurfaceFormatVK(img.info.format);
    desc.width = img.info.extent.width;
    desc.height = img.info.extent.height;
    desc.depth = 1;
    desc.mipCount = 1;
    desc.flags = FFX_API_RESOURCE_FLAGS_NONE;
    desc.usage = usage;
    return ffxApiGetResourceVK(reinterpret_cast<void*>(img.image), desc, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
}

void Transition(Image& img, VkImageLayout from, VkImageLayout to)
{
    if (from == to)
        return;
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    interop->TransitionSurfaceLayout(img.surface, &range, from, to);
}

void Barrier(VkCommandBuffer cmd)
{
    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void WaitAll()
{
    if (device && fences[0])
        vk.WaitForFences(device, Slots, fences, VK_TRUE, 1000000000ull);
}

bool EnsureContext(const KuFrame* f)
{
    int key[5] = { f->renderWidth, f->renderHeight, f->outputWidth, f->outputHeight, f->createFlags };
    if (context && memcmp(key, contextKey, sizeof key) == 0)
        return true;
    if (context) {
        WaitAll();
        ffx.DestroyContext(&context, nullptr);
        context = nullptr;
    }
    ffxCreateBackendVKDesc backend = {};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.vkDevice = device;
    backend.vkPhysicalDevice = physical;
    backend.vkDeviceProcAddr = DeviceProcAddrWithCoreFallback;

    ffxCreateContextDescUpscale desc = {};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.header.pNext = &backend.header;
    desc.flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
    desc.maxRenderSize = { (uint32_t)f->renderWidth, (uint32_t)f->renderHeight };
    desc.maxUpscaleSize = { (uint32_t)f->outputWidth, (uint32_t)f->outputHeight };
    desc.fpMessage = FfxMessage;
    ffxReturnCode_t r = ffx.CreateContext(&context, &desc.header, nullptr);
    if (r != FFX_API_RETURN_OK) {
        context = nullptr;
        Fail("ffxCreateContext (upscale, Vulkan)", (int)r);
        return false;
    }
    memcpy(contextKey, key, sizeof key);
    Log("FSR 3.1 Vulkan context: %dx%d -> %dx%d", f->renderWidth, f->renderHeight, f->outputWidth, f->outputHeight);
    return true;
}

} // namespace

bool VkInit(ID3D11Device* d3d)
{
    if (FAILED(d3d->QueryInterface(IID_InteropDevice, reinterpret_cast<void**>(&interop)))) {
        Fail("the D3D11 device is not DXVK (no IDXGIVkInteropDevice)", 0);
        return false;
    }
    VkInstance instance;
    interop->GetVulkanHandles(&instance, &physical, &device);
    interop->GetSubmissionQueue(&queue, &queueFamily);

    HMODULE vulkan = GetModuleHandleA("vulkan-1.dll");
    if (!vulkan)
        vulkan = LoadLibraryA("vulkan-1.dll");
    if (!vulkan || !ModuleFn(vulkan, vk.GetDeviceProcAddr, "vkGetDeviceProcAddr")) {
        Fail("vulkan-1.dll", 0);
        return false;
    }
    if (!DeviceFn(vk.CreateCommandPool, "vkCreateCommandPool") || !DeviceFn(vk.DestroyCommandPool, "vkDestroyCommandPool")
        || !DeviceFn(vk.AllocateCommandBuffers, "vkAllocateCommandBuffers") || !DeviceFn(vk.ResetCommandBuffer, "vkResetCommandBuffer")
        || !DeviceFn(vk.BeginCommandBuffer, "vkBeginCommandBuffer") || !DeviceFn(vk.EndCommandBuffer, "vkEndCommandBuffer")
        || !DeviceFn(vk.CmdPipelineBarrier, "vkCmdPipelineBarrier") || !DeviceFn(vk.QueueSubmit, "vkQueueSubmit")
        || !DeviceFn(vk.CreateFence, "vkCreateFence") || !DeviceFn(vk.DestroyFence, "vkDestroyFence")
        || !DeviceFn(vk.WaitForFences, "vkWaitForFences") || !DeviceFn(vk.ResetFences, "vkResetFences")) {
        Fail("resolve Vulkan device functions", 0);
        return false;
    }

    HMODULE ffxDll = LoadFfx();
    if (!ffxDll || !ModuleFn(ffxDll, ffx.CreateContext, "ffxCreateContext")
        || !ModuleFn(ffxDll, ffx.DestroyContext, "ffxDestroyContext") || !ModuleFn(ffxDll, ffx.Dispatch, "ffxDispatch")) {
        Fail("load amd_fidelityfx_vk.dll next to KoH2UpscaleNative.dll", (int)GetLastError());
        return false;
    }

    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (vk.CreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        Fail("vkCreateCommandPool", 0);
        return false;
    }
    VkCommandBufferAllocateInfo alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = Slots;
    vk.AllocateCommandBuffers(device, &alloc, cmds);
    VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < Slots; i++)
        vk.CreateFence(device, &fenceInfo, nullptr, &fences[i]);
    Log("Vulkan backend ready (DXVK device, queue family %u)", queueFamily);
    return true;
}

void VkEvaluate(const KuFrame* f)
{
    if (!EnsureContext(f))
        return;
    Image color, depth, motion, output;
    Image* images[] = { &color, &depth, &motion, &output };
    if (!GetImage(f->color, color) || !GetImage(f->depth, depth) || !GetImage(f->motion, motion) || !GetImage(f->output, output)) {
        Fail("IDXGIVkInteropSurface on an input texture", 0);
        for (Image* img : images)
            if (img->surface)
                img->surface->Release();
        return;
    }
    for (Image* img : images)
        Transition(*img, img->layout, VK_IMAGE_LAYOUT_GENERAL);
    interop->FlushRenderingCommands();

    unsigned slot = frame++ % Slots;
    VkCommandBuffer cmd = cmds[slot];
    vk.WaitForFences(device, 1, &fences[slot], VK_TRUE, 1000000000ull);
    vk.ResetFences(device, 1, &fences[slot]);
    vk.ResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk.BeginCommandBuffer(cmd, &begin);
    Barrier(cmd);

    ffxDispatchDescUpscale d = {};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = cmd;
    d.color = Resource(color, FFX_API_RESOURCE_USAGE_READ_ONLY);
    d.depth = Resource(depth, FFX_API_RESOURCE_USAGE_READ_ONLY);
    d.motionVectors = Resource(motion, FFX_API_RESOURCE_USAGE_READ_ONLY);
    d.output = Resource(output, FFX_API_RESOURCE_USAGE_UAV);
    d.jitterOffset = { f->jitterX, f->jitterY };
    d.motionVectorScale = { f->mvScaleX, f->mvScaleY };
    d.renderSize = { (uint32_t)f->renderWidth, (uint32_t)f->renderHeight };
    d.upscaleSize = { (uint32_t)f->outputWidth, (uint32_t)f->outputHeight };
    d.enableSharpening = f->sharpness > 0.0f;
    d.sharpness = f->sharpness;
    d.frameTimeDelta = f->frameTimeMs;
    d.preExposure = 1.0f;
    d.reset = f->reset != 0;
    // Inverted depth: FSR wants near and far swapped, as in FSR3Unity.
    d.cameraNear = f->cameraFar;
    d.cameraFar = f->cameraNear;
    d.cameraFovAngleVertical = f->fovY;
    d.viewSpaceToMetersFactor = 1.0f;
    ffxReturnCode_t r = ffx.Dispatch(&context, &d.header);

    Barrier(cmd);
    vk.EndCommandBuffer(cmd);
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    interop->LockSubmissionQueue();
    VkResult vr = vk.QueueSubmit(queue, 1, &submit, fences[slot]);
    interop->ReleaseSubmissionQueue();

    for (Image* img : images) {
        Transition(*img, VK_IMAGE_LAYOUT_GENERAL, img->layout);
        img->surface->Release();
    }
    if (r != FFX_API_RETURN_OK)
        Fail("ffxDispatch (upscale)", (int)r);
    else if (vr != VK_SUCCESS)
        Fail("vkQueueSubmit", (int)vr);
}

void VkShutdown()
{
    WaitAll();
    if (context)
        ffx.DestroyContext(&context, nullptr);
    context = nullptr;
    for (int i = 0; i < Slots; i++) {
        if (fences[i])
            vk.DestroyFence(device, fences[i], nullptr);
        fences[i] = VK_NULL_HANDLE;
    }
    if (pool)
        vk.DestroyCommandPool(device, pool, nullptr);
    pool = VK_NULL_HANDLE;
    if (interop)
        interop->Release();
    interop = nullptr;
}
