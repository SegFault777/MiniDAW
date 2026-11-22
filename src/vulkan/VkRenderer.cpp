// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#define VK_NO_PROTOTYPES
#include "VkRenderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <vulkan/vulkan.h>

namespace vkui {

// Every Vulkan function the renderer calls, grouped by how it is looked up.
#define VK_INSTANCE_FUNCS(X)                                                                                                                    \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties)             \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkEnumerateDeviceExtensionProperties) X(vkDestroySurfaceKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR)               \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkCreateWin32SurfaceKHR)
#define VK_DEVICE_FUNCS(X)                                                                                                                      \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR)    \
    X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkQueueSubmit) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateRenderPass)           \
    X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateShaderModule) X(vkDestroyShaderModule)                    \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout)                       \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets)       \
    X(vkUpdateDescriptorSets) X(vkCreateSampler) X(vkDestroySampler) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements)      \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements)           \
    X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers)         \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) \
    X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdPushConstants) X(vkCmdSetViewport) X(vkCmdSetScissor) \
    X(vkCmdDrawIndexed) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateFence)        \
    X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences)

struct VkRenderer::Impl {
#define X(name) PFN_##name name = nullptr;
    VK_INSTANCE_FUNCS(X)
    VK_DEVICE_FUNCS(X)
#undef X
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    void* lib = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps{};
    std::string gpuNameStr;
    uint32_t family = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{0, 0};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> renderDone;
    VkRenderPass renderPass = VK_NULL_HANDLE;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImage atlas = VK_NULL_HANDLE;
    VkDeviceMemory atlasMem = VK_NULL_HANDLE;
    VkImageView atlasView = VK_NULL_HANDLE;
    bool atlasReady = false;
    std::uint64_t atlasRevision = 0;
    std::string shaderDir;

    struct Buf { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; void* map = nullptr; size_t cap = 0; };
    static constexpr int kFrames = 2;
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore imageAvail = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Buf vertices, indices, staging;
    } frames[kFrames];
    int frameIndex = 0;
    int wantW = 0, wantH = 0;
    bool swapDirty = true;

    bool loadLibrary(std::string& err);
    bool createInstanceAndDevice(void* hinstance, void* hwnd, std::string& err);
    bool createSwapchain(std::string& err);
    void destroySwapchain();
    bool createPipeline(std::string& err);
    bool createAtlas(std::string& err);
    bool createFrames(std::string& err);
    uint32_t findMemory(uint32_t bits, VkMemoryPropertyFlags want) const;
    bool makeBuffer(Buf& b, size_t size, VkBufferUsageFlags usage, std::string& err);
    void freeBuffer(Buf& b);
    bool readFile(const std::string& path, std::vector<char>& out) const;
};

VkRenderer::VkRenderer() : m(std::make_unique<Impl>()) {}
VkRenderer::~VkRenderer() { shutdown(); }
std::string VkRenderer::gpuName() const { return m->gpuNameStr; }
int VkRenderer::width() const { return int(m->extent.width); }
int VkRenderer::height() const { return int(m->extent.height); }
void VkRenderer::resize(int w, int h) { if (w != m->wantW || h != m->wantH) { m->wantW = w; m->wantH = h; m->swapDirty = true; } }

// --------------------------------------------------------------------------------------------------------------- setup

bool VkRenderer::Impl::loadLibrary(std::string& err) {
#ifdef _WIN32
    lib = (void*)LoadLibraryA("vulkan-1.dll");
    if (lib) getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress((HMODULE)lib, "vkGetInstanceProcAddr");
#else
    lib = dlopen("libvulkan.so.1", RTLD_NOW);
    if (lib) getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
#endif
    if (!lib || !getInstanceProcAddr) { err = "Vulkan is not installed: vulkan-1.dll was not found. Update the graphics driver, or use MiniDAW.exe (the Native version)."; return false; }
    createInstance = (PFN_vkCreateInstance)getInstanceProcAddr(nullptr, "vkCreateInstance");
    if (!createInstance) { err = "The Vulkan loader does not provide vkCreateInstance."; return false; }
    return true;
}

uint32_t VkRenderer::Impl::findMemory(uint32_t bits, VkMemoryPropertyFlags want) const {
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0xFFFFFFFFu;
}

bool VkRenderer::Impl::readFile(const std::string& path, std::vector<char>& out) const {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    out.resize(size_t(f.tellg()));
    f.seekg(0);
    f.read(out.data(), std::streamsize(out.size()));
    return !out.empty() && out.size() % 4 == 0;
}

bool VkRenderer::Impl::createInstanceAndDevice(void* hinstance, void* hwnd, std::string& err) {
    const char* instExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "MiniDAW";
    app.applicationVersion = 12;
    app.pEngineName = "MiniDAW Vulkan UI";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instExt;
    if (createInstance(&ici, nullptr, &instance) != VK_SUCCESS) { err = "Could not create a Vulkan instance (is a Vulkan 1.0 driver installed?)."; return false; }
#define X(name) name = (PFN_##name)getInstanceProcAddr(instance, #name);
    VK_INSTANCE_FUNCS(X)
#undef X
    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = (HINSTANCE)hinstance;
    sci.hwnd = (HWND)hwnd;
    if (vkCreateWin32SurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS) { err = "Could not create the Vulkan window surface."; return false; }

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    if (n == 0) { err = "No Vulkan capable graphics card was found."; return false; }
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(instance, &n, gpus.data());
    int bestScore = -1;
    for (VkPhysicalDevice g : gpus) {
        uint32_t ext = 0;
        vkEnumerateDeviceExtensionProperties(g, nullptr, &ext, nullptr);
        std::vector<VkExtensionProperties> exts(ext);
        vkEnumerateDeviceExtensionProperties(g, nullptr, &ext, exts.data());
        bool swap = false;
        for (const auto& e : exts) if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) swap = true;
        if (!swap) continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, qf.data());
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(g, i, surface, &present);
            if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(g, &props);
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestScore) { bestScore = score; gpu = g; family = i; gpuNameStr = props.deviceName; }
            break;
        }
    }
    if (!gpu) { err = "No graphics card can show a Vulkan window."; return false; }
    vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char* devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExt;
    if (vkCreateDevice(gpu, &dci, nullptr, &device) != VK_SUCCESS) { err = "Could not create the Vulkan device."; return false; }
#define X(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name);
    VK_DEVICE_FUNCS(X)
#undef X
    vkGetDeviceQueue(device, family, 0, &queue);
    return true;
}

bool VkRenderer::Impl::createSwapchain(std::string& err) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {
        ext.width = std::clamp<uint32_t>(uint32_t(std::max(1, wantW)), caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = std::clamp<uint32_t>(uint32_t(std::max(1, wantH)), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (ext.width == 0 || ext.height == 0) return false;   // minimised
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fn, fmts.data());
    VkSurfaceFormatKHR chosen = fmts.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : fmts[0];
    for (const auto& f : fmts) if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) chosen = f;   // colours are authored in plain 8 bit values
    format = chosen.format;
    extent = ext;
    uint32_t count = std::max(caps.minImageCount + 1, 2u);
    if (caps.maxImageCount > 0) count = std::min(count, caps.maxImageCount);
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface;
    sci.minImageCount = count;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = ext;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // vsync, always available
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS) { err = "Could not create the swap chain."; return false; }
    uint32_t in = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &in, nullptr);
    images.resize(in);
    vkGetSwapchainImagesKHR(device, swapchain, &in, images.data());

    if (renderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription att{};
        att.format = format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rci.attachmentCount = 1;
        rci.pAttachments = &att;
        rci.subpassCount = 1;
        rci.pSubpasses = &sub;
        rci.dependencyCount = 1;
        rci.pDependencies = &dep;
        if (vkCreateRenderPass(device, &rci, nullptr, &renderPass) != VK_SUCCESS) { err = "Could not create the render pass."; return false; }
    }
    views.resize(in);
    framebuffers.resize(in);
    renderDone.resize(in);
    for (uint32_t i = 0; i < in; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &vi, nullptr, &views[i]) != VK_SUCCESS) { err = "Could not create an image view."; return false; }
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = renderPass;
        fi.attachmentCount = 1;
        fi.pAttachments = &views[i];
        fi.width = ext.width;
        fi.height = ext.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(device, &fi, nullptr, &framebuffers[i]) != VK_SUCCESS) { err = "Could not create a framebuffer."; return false; }
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(device, &si, nullptr, &renderDone[i]) != VK_SUCCESS) { err = "Could not create a semaphore."; return false; }
    }
    return true;
}

void VkRenderer::Impl::destroySwapchain() {
    if (!device) return;
    vkDeviceWaitIdle(device);
    for (auto s : renderDone) if (s) vkDestroySemaphore(device, s, nullptr);
    for (auto f : framebuffers) if (f) vkDestroyFramebuffer(device, f, nullptr);
    for (auto v : views) if (v) vkDestroyImageView(device, v, nullptr);
    renderDone.clear(); framebuffers.clear(); views.clear(); images.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

bool VkRenderer::Impl::createPipeline(std::string& err) {
    std::vector<char> vs, fs;
    if (!readFile(shaderDir + "/ui.vert.spv", vs) || !readFile(shaderDir + "/ui.frag.spv", fs)) {
        err = "The shader files ui.vert.spv / ui.frag.spv are missing in '" + shaderDir + "'. Rebuild with build.sh vulkan (needs glslc or glslangValidator).";
        return false;
    }
    auto makeModule = [&](const std::vector<char>& code, VkShaderModule& out) {
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = code.size();
        mi.pCode = reinterpret_cast<const uint32_t*>(code.data());
        return vkCreateShaderModule(device, &mi, nullptr, &out) == VK_SUCCESS;
    };
    VkShaderModule vm = VK_NULL_HANDLE, fm = VK_NULL_HANDLE;
    if (!makeModule(vs, vm) || !makeModule(fs, fm)) { err = "The graphics driver rejected the shaders."; return false; }

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 1;
    dl.pBindings = &b;
    vkCreateDescriptorSetLayout(device, &dl, nullptr, &setLayout);
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(device, &pl, nullptr, &pipelineLayout);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vm;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fm;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, hx)};
    attrs[3] = {3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, rgba)};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = pipelineLayout;
    gp.renderPass = renderPass;
    gp.subpass = 0;
    const VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline);
    vkDestroyShaderModule(device, vm, nullptr);
    vkDestroyShaderModule(device, fm, nullptr);
    if (res != VK_SUCCESS) { err = "Could not create the graphics pipeline."; return false; }
    return true;
}

bool VkRenderer::Impl::makeBuffer(Buf& b, size_t size, VkBufferUsageFlags usage, std::string& err) {
    freeBuffer(b);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, nullptr, &b.buf) != VK_SUCCESS) { err = "Could not create a buffer."; return false; }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, b.buf, &mr);
    const uint32_t type = findMemory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == 0xFFFFFFFFu) { err = "No host visible memory for the buffers."; return false; }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(device, &ai, nullptr, &b.mem) != VK_SUCCESS) { err = "Out of graphics memory."; return false; }
    vkBindBufferMemory(device, b.buf, b.mem, 0);
    vkMapMemory(device, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map);
    b.cap = size;
    return true;
}

void VkRenderer::Impl::freeBuffer(Buf& b) {
    if (!device) return;
    if (b.map) vkUnmapMemory(device, b.mem);
    if (b.buf) vkDestroyBuffer(device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(device, b.mem, nullptr);
    b = Buf{};
}

bool VkRenderer::Impl::createAtlas(std::string& err) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8_UNORM;
    ii.extent = {Fonts::kAtlas, Fonts::kAtlas, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ii, nullptr, &atlas) != VK_SUCCESS) { err = "Could not create the glyph atlas."; return false; }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, atlas, &mr);
    const uint32_t type = findMemory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == 0xFFFFFFFFu) { err = "No device memory for the glyph atlas."; return false; }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(device, &ai, nullptr, &atlasMem) != VK_SUCCESS) { err = "Out of graphics memory."; return false; }
    vkBindImageMemory(device, atlas, atlasMem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = atlas;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device, &vi, nullptr, &atlasView);
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &si, nullptr, &sampler);
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCreateDescriptorPool(device, &dpi, nullptr, &descPool);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &setLayout;
    vkAllocateDescriptorSets(device, &dai, &descSet);
    VkDescriptorImageInfo dii{sampler, atlasView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = descSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    return true;
}

bool VkRenderer::Impl::createFrames(std::string& err) {
    for (Frame& f : frames) {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = family;
        if (vkCreateCommandPool(device, &ci, nullptr, &f.pool) != VK_SUCCESS) { err = "Could not create a command pool."; return false; }
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &ai, &f.cmd);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device, &si, nullptr, &f.imageAvail);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &fi, nullptr, &f.fence);
        if (!makeBuffer(f.vertices, size_t(1) << 22, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, err)) return false;
        if (!makeBuffer(f.indices, size_t(1) << 21, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, err)) return false;
        if (!makeBuffer(f.staging, size_t(Fonts::kAtlas) * Fonts::kAtlas, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, err)) return false;
    }
    return true;
}

bool VkRenderer::init(void* hinstance, void* hwnd, const std::string& shaderDir, std::string& error) {
    m->shaderDir = shaderDir;
    if (!m->loadLibrary(error) || !m->createInstanceAndDevice(hinstance, hwnd, error)) { shutdown(); return false; }
    m->swapDirty = true;
    if (m->wantW == 0) { m->wantW = 1280; m->wantH = 800; }
    if (!m->createSwapchain(error)) { if (error.empty()) error = "The window has no size yet."; shutdown(); return false; }
    if (!m->createPipeline(error) || !m->createAtlas(error) || !m->createFrames(error)) { shutdown(); return false; }
    m->swapDirty = false;
    return true;
}

void VkRenderer::shutdown() {
    Impl& s = *m;
    if (s.device) {
        s.vkDeviceWaitIdle(s.device);
        for (auto& f : s.frames) {
            s.freeBuffer(f.vertices); s.freeBuffer(f.indices); s.freeBuffer(f.staging);
            if (f.fence) s.vkDestroyFence(s.device, f.fence, nullptr);
            if (f.imageAvail) s.vkDestroySemaphore(s.device, f.imageAvail, nullptr);
            if (f.pool) s.vkDestroyCommandPool(s.device, f.pool, nullptr);
            f = Impl::Frame{};
        }
        s.destroySwapchain();
        if (s.pipeline) s.vkDestroyPipeline(s.device, s.pipeline, nullptr);
        if (s.pipelineLayout) s.vkDestroyPipelineLayout(s.device, s.pipelineLayout, nullptr);
        if (s.descPool) s.vkDestroyDescriptorPool(s.device, s.descPool, nullptr);
        if (s.setLayout) s.vkDestroyDescriptorSetLayout(s.device, s.setLayout, nullptr);
        if (s.sampler) s.vkDestroySampler(s.device, s.sampler, nullptr);
        if (s.atlasView) s.vkDestroyImageView(s.device, s.atlasView, nullptr);
        if (s.atlas) s.vkDestroyImage(s.device, s.atlas, nullptr);
        if (s.atlasMem) s.vkFreeMemory(s.device, s.atlasMem, nullptr);
        if (s.renderPass) s.vkDestroyRenderPass(s.device, s.renderPass, nullptr);
        s.vkDestroyDevice(s.device, nullptr);
    }
    if (s.instance) {
        if (s.surface) s.vkDestroySurfaceKHR(s.instance, s.surface, nullptr);
        s.vkDestroyInstance(s.instance, nullptr);
    }
    s.device = VK_NULL_HANDLE; s.instance = VK_NULL_HANDLE; s.surface = VK_NULL_HANDLE;
    s.pipeline = VK_NULL_HANDLE; s.pipelineLayout = VK_NULL_HANDLE; s.descPool = VK_NULL_HANDLE; s.setLayout = VK_NULL_HANDLE;
    s.sampler = VK_NULL_HANDLE; s.atlasView = VK_NULL_HANDLE; s.atlas = VK_NULL_HANDLE; s.atlasMem = VK_NULL_HANDLE; s.renderPass = VK_NULL_HANDLE;
    s.atlasReady = false;
    s.atlasRevision = 0;
    if (s.lib) {
#ifdef _WIN32
        FreeLibrary((HMODULE)s.lib);
#else
        dlclose(s.lib);
#endif
        s.lib = nullptr;
    }
}

// --------------------------------------------------------------------------------------------------------------- frame

bool VkRenderer::render(const DrawList& list, const Fonts& fonts, Color clear) {
    Impl& s = *m;
    if (!s.device) return false;
    if (s.swapDirty || s.swapchain == VK_NULL_HANDLE) {
        s.destroySwapchain();
        std::string err;
        if (!s.createSwapchain(err)) return false;   // minimised: try again later
        s.swapDirty = false;
    }
    Impl::Frame& f = s.frames[s.frameIndex];
    s.vkWaitForFences(s.device, 1, &f.fence, VK_TRUE, UINT64_MAX);
    uint32_t image = 0;
    VkResult r = s.vkAcquireNextImageKHR(s.device, s.swapchain, UINT64_MAX, f.imageAvail, VK_NULL_HANDLE, &image);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { s.swapDirty = true; return false; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
    s.vkResetFences(s.device, 1, &f.fence);

    // geometry into this frame's buffers (grown when a frame is bigger than anything before)
    const size_t vbytes = list.vertices().size() * sizeof(Vertex), ibytes = list.indices().size() * sizeof(std::uint32_t);
    std::string err;
    if (vbytes > f.vertices.cap || ibytes > f.indices.cap) {
        s.vkDeviceWaitIdle(s.device);
        if (vbytes > f.vertices.cap && !s.makeBuffer(f.vertices, std::max(vbytes, f.vertices.cap * 2), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, err)) return false;
        if (ibytes > f.indices.cap && !s.makeBuffer(f.indices, std::max(ibytes, f.indices.cap * 2), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, err)) return false;
    }
    if (vbytes) std::memcpy(f.vertices.map, list.vertices().data(), vbytes);
    if (ibytes) std::memcpy(f.indices.map, list.indices().data(), ibytes);
    const bool uploadAtlas = fonts.revision() != s.atlasRevision;
    if (uploadAtlas) std::memcpy(f.staging.map, fonts.atlas().data(), fonts.atlas().size());

    VkCommandBuffer cmd = f.cmd;
    s.vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    s.vkBeginCommandBuffer(cmd, &bi);
    if (uploadAtlas) {
        VkImageMemoryBarrier to{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        to.oldLayout = s.atlasReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        to.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to.srcQueueFamilyIndex = to.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to.image = s.atlas;
        to.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        to.srcAccessMask = s.atlasReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        to.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        s.vkCmdPipelineBarrier(cmd, s.atlasReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {Fonts::kAtlas, Fonts::kAtlas, 1};
        s.vkCmdCopyBufferToImage(cmd, f.staging.buf, s.atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier from = to;
        from.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        from.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        from.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        from.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        s.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &from);
        s.atlasReady = true;
        s.atlasRevision = fonts.revision();
    }
    VkClearValue cv{};
    cv.color.float32[0] = float((clear >> 16) & 255u) / 255.f;
    cv.color.float32[1] = float((clear >> 8) & 255u) / 255.f;
    cv.color.float32[2] = float(clear & 255u) / 255.f;
    cv.color.float32[3] = 1.f;
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = s.renderPass;
    rp.framebuffer = s.framebuffers[image];
    rp.renderArea = {{0, 0}, s.extent};
    rp.clearValueCount = 1;
    rp.pClearValues = &cv;
    s.vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (!list.indices().empty()) {
        s.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipeline);
        s.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 0, 1, &s.descSet, 0, nullptr);
        const VkDeviceSize off = 0;
        s.vkCmdBindVertexBuffers(cmd, 0, 1, &f.vertices.buf, &off);
        s.vkCmdBindIndexBuffer(cmd, f.indices.buf, 0, VK_INDEX_TYPE_UINT32);
        const float vp[2] = {float(s.extent.width), float(s.extent.height)};
        s.vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof vp, vp);
        VkViewport viewport{0.f, 0.f, vp[0], vp[1], 0.f, 1.f};
        s.vkCmdSetViewport(cmd, 0, 1, &viewport);
        for (const Batch& b : list.batches()) {
            if (b.count == 0 || b.w <= 0 || b.h <= 0) continue;
            VkRect2D sc;
            sc.offset = {std::clamp(b.x, 0, int(s.extent.width) - 1), std::clamp(b.y, 0, int(s.extent.height) - 1)};
            sc.extent = {uint32_t(std::min(b.w, int(s.extent.width) - sc.offset.x)), uint32_t(std::min(b.h, int(s.extent.height) - sc.offset.y))};
            s.vkCmdSetScissor(cmd, 0, 1, &sc);
            s.vkCmdDrawIndexed(cmd, b.count, 1, b.first, 0, 0);
        }
    }
    s.vkCmdEndRenderPass(cmd);
    s.vkEndCommandBuffer(cmd);

    const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.imageAvail;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &s.renderDone[image];
    s.vkQueueSubmit(s.queue, 1, &si, f.fence);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &s.renderDone[image];
    pi.swapchainCount = 1;
    pi.pSwapchains = &s.swapchain;
    pi.pImageIndices = &image;
    r = s.vkQueuePresentKHR(s.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) s.swapDirty = true;
    s.frameIndex = (s.frameIndex + 1) % Impl::kFrames;
    return true;
}

}  // namespace vkui
