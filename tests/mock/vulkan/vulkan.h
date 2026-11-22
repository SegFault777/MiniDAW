// A hand written stand-in for the parts of <vulkan/vulkan.h> that src/vulkan/VkRenderer.cpp uses. It lets the renderer be
// compile-checked on machines without the Vulkan SDK (CI uses the real Khronos headers). Names and layouts follow the real API.
#pragma once
#include <cstddef>
#include <cstdint>
typedef void* HINSTANCE;
typedef void* HWND;
#define VKAPI_CALL
#define VKAPI_PTR
typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
#define VK_DEFINE_HANDLE(o) typedef struct o##_T* o;
VK_DEFINE_HANDLE(VkInstance) VK_DEFINE_HANDLE(VkPhysicalDevice) VK_DEFINE_HANDLE(VkDevice) VK_DEFINE_HANDLE(VkQueue) VK_DEFINE_HANDLE(VkCommandBuffer)
#define VK_NON_DISPATCHABLE_HANDLE(o) typedef struct o##_T* o;
VK_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR) VK_NON_DISPATCHABLE_HANDLE(VkSwapchainKHR) VK_NON_DISPATCHABLE_HANDLE(VkImage) VK_NON_DISPATCHABLE_HANDLE(VkImageView)
VK_NON_DISPATCHABLE_HANDLE(VkFramebuffer) VK_NON_DISPATCHABLE_HANDLE(VkRenderPass) VK_NON_DISPATCHABLE_HANDLE(VkSemaphore) VK_NON_DISPATCHABLE_HANDLE(VkFence)
VK_NON_DISPATCHABLE_HANDLE(VkDescriptorSetLayout) VK_NON_DISPATCHABLE_HANDLE(VkPipelineLayout) VK_NON_DISPATCHABLE_HANDLE(VkPipeline) VK_NON_DISPATCHABLE_HANDLE(VkShaderModule)
VK_NON_DISPATCHABLE_HANDLE(VkDescriptorPool) VK_NON_DISPATCHABLE_HANDLE(VkDescriptorSet) VK_NON_DISPATCHABLE_HANDLE(VkSampler) VK_NON_DISPATCHABLE_HANDLE(VkDeviceMemory)
VK_NON_DISPATCHABLE_HANDLE(VkBuffer) VK_NON_DISPATCHABLE_HANDLE(VkCommandPool)
#define VK_NULL_HANDLE nullptr
#define VK_WHOLE_SIZE (~0ULL)
#define VK_QUEUE_FAMILY_IGNORED (~0U)
#define VK_SUBPASS_EXTERNAL (~0U)
#define VK_API_VERSION_1_0 0x400000u
#define VK_KHR_SURFACE_EXTENSION_NAME "VK_KHR_surface"
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"
#define VK_KHR_SWAPCHAIN_EXTENSION_NAME "VK_KHR_swapchain"
#define VK_TRUE 1
#define VK_FALSE 0
#define VK_MAX_EXTENSION_NAME_SIZE 256
#define VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
#define VK_MAX_MEMORY_TYPES 32
#define VK_MAX_MEMORY_HEAPS 16
#define VK_UUID_SIZE 16

enum VkResult { VK_SUCCESS = 0, VK_SUBOPTIMAL_KHR = 1000001003, VK_ERROR_OUT_OF_DATE_KHR = -1000001004 };
enum VkStructureType {
    VK_STRUCTURE_TYPE_APPLICATION_INFO, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    VK_STRUCTURE_TYPE_SUBMIT_INFO, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR
};
enum VkFormat { VK_FORMAT_UNDEFINED = 0, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
enum VkColorSpaceKHR { VK_COLOR_SPACE_SRGB_NONLINEAR_KHR = 0 };
enum VkPresentModeKHR { VK_PRESENT_MODE_FIFO_KHR = 2 };
enum VkImageLayout { VK_IMAGE_LAYOUT_UNDEFINED = 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
enum VkImageType { VK_IMAGE_TYPE_2D };
enum VkImageViewType { VK_IMAGE_VIEW_TYPE_2D };
enum VkImageTiling { VK_IMAGE_TILING_OPTIMAL };
enum VkSharingMode { VK_SHARING_MODE_EXCLUSIVE };
enum VkPhysicalDeviceType { VK_PHYSICAL_DEVICE_TYPE_OTHER, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU };
enum VkAttachmentLoadOp { VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_DONT_CARE };
enum VkAttachmentStoreOp { VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_STORE_OP_DONT_CARE };
enum VkPipelineBindPoint { VK_PIPELINE_BIND_POINT_GRAPHICS };
enum VkDescriptorType { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1 };
enum VkVertexInputRate { VK_VERTEX_INPUT_RATE_VERTEX };
enum VkPrimitiveTopology { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
enum VkPolygonMode { VK_POLYGON_MODE_FILL };
enum VkFrontFace { VK_FRONT_FACE_CLOCKWISE };
enum VkBlendFactor { VK_BLEND_FACTOR_ONE = 1, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA };
enum VkBlendOp { VK_BLEND_OP_ADD };
enum VkDynamicState { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
enum VkFilter { VK_FILTER_LINEAR };
enum VkSamplerAddressMode { VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE };
enum VkCommandBufferLevel { VK_COMMAND_BUFFER_LEVEL_PRIMARY };
enum VkSubpassContents { VK_SUBPASS_CONTENTS_INLINE };
enum VkIndexType { VK_INDEX_TYPE_UINT32 };

typedef VkFlags VkSampleCountFlags, VkImageUsageFlags, VkBufferUsageFlags, VkMemoryPropertyFlags, VkQueueFlags, VkShaderStageFlags, VkPipelineStageFlags, VkAccessFlags;
typedef VkFlags VkCompositeAlphaFlagBitsKHR, VkSurfaceTransformFlagBitsKHR, VkCullModeFlags, VkColorComponentFlags, VkCommandPoolCreateFlags, VkFenceCreateFlags, VkCommandBufferUsageFlags;
enum { VK_SAMPLE_COUNT_1_BIT = 1 };
enum { VK_IMAGE_USAGE_TRANSFER_DST_BIT = 2, VK_IMAGE_USAGE_SAMPLED_BIT = 4, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 16 };
enum { VK_BUFFER_USAGE_TRANSFER_SRC_BIT = 1, VK_BUFFER_USAGE_INDEX_BUFFER_BIT = 64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT = 128 };
enum { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 2, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 4 };
enum { VK_QUEUE_GRAPHICS_BIT = 1 };
enum { VK_SHADER_STAGE_VERTEX_BIT = 1, VK_SHADER_STAGE_FRAGMENT_BIT = 16 };
enum { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT = 1, VK_PIPELINE_STAGE_TRANSFER_BIT = 4096, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT = 128, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT = 1024 };
enum { VK_ACCESS_SHADER_READ_BIT = 32, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT = 256, VK_ACCESS_TRANSFER_WRITE_BIT = 4096 };
enum { VK_IMAGE_ASPECT_COLOR_BIT = 1 };
enum { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR = 1 };
enum { VK_CULL_MODE_NONE = 0 };
enum { VK_COLOR_COMPONENT_R_BIT = 1, VK_COLOR_COMPONENT_G_BIT = 2, VK_COLOR_COMPONENT_B_BIT = 4, VK_COLOR_COMPONENT_A_BIT = 8 };
enum { VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT = 2 };
enum { VK_FENCE_CREATE_SIGNALED_BIT = 1 };
enum { VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 1 };

struct VkExtent2D { uint32_t width, height; };
struct VkExtent3D { uint32_t width, height, depth; };
struct VkOffset2D { int32_t x, y; };
struct VkRect2D { VkOffset2D offset; VkExtent2D extent; };
struct VkViewport { float x, y, width, height, minDepth, maxDepth; };
struct VkApplicationInfo { VkStructureType sType; const void* pNext; const char* pApplicationName; uint32_t applicationVersion; const char* pEngineName; uint32_t engineVersion; uint32_t apiVersion; };
struct VkInstanceCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; const VkApplicationInfo* pApplicationInfo; uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames; uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; };
struct VkWin32SurfaceCreateInfoKHR { VkStructureType sType; const void* pNext; VkFlags flags; HINSTANCE hinstance; HWND hwnd; };
struct VkExtensionProperties { char extensionName[VK_MAX_EXTENSION_NAME_SIZE]; uint32_t specVersion; };
struct VkQueueFamilyProperties { VkQueueFlags queueFlags; uint32_t queueCount; uint32_t timestampValidBits; VkExtent3D minImageTransferGranularity; };
struct VkPhysicalDeviceLimits { uint32_t dummy; };
struct VkPhysicalDeviceSparseProperties { VkBool32 dummy; };
struct VkPhysicalDeviceProperties { uint32_t apiVersion, driverVersion, vendorID, deviceID; VkPhysicalDeviceType deviceType; char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE]; uint8_t pipelineCacheUUID[VK_UUID_SIZE]; VkPhysicalDeviceLimits limits; VkPhysicalDeviceSparseProperties sparseProperties; };
struct VkMemoryType { VkMemoryPropertyFlags propertyFlags; uint32_t heapIndex; };
struct VkMemoryHeap { VkDeviceSize size; VkFlags flags; };
struct VkPhysicalDeviceMemoryProperties { uint32_t memoryTypeCount; VkMemoryType memoryTypes[VK_MAX_MEMORY_TYPES]; uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[VK_MAX_MEMORY_HEAPS]; };
struct VkDeviceQueueCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t queueFamilyIndex; uint32_t queueCount; const float* pQueuePriorities; };
struct VkDeviceCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos; uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames; uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; const void* pEnabledFeatures; };
struct VkSurfaceCapabilitiesKHR { uint32_t minImageCount, maxImageCount; VkExtent2D currentExtent, minImageExtent, maxImageExtent; uint32_t maxImageArrayLayers; VkFlags supportedTransforms; VkSurfaceTransformFlagBitsKHR currentTransform; VkFlags supportedCompositeAlpha, supportedUsageFlags; };
struct VkSurfaceFormatKHR { VkFormat format; VkColorSpaceKHR colorSpace; };
struct VkSwapchainCreateInfoKHR { VkStructureType sType; const void* pNext; VkFlags flags; VkSurfaceKHR surface; uint32_t minImageCount; VkFormat imageFormat; VkColorSpaceKHR imageColorSpace; VkExtent2D imageExtent; uint32_t imageArrayLayers; VkImageUsageFlags imageUsage; VkSharingMode imageSharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; VkSurfaceTransformFlagBitsKHR preTransform; VkCompositeAlphaFlagBitsKHR compositeAlpha; VkPresentModeKHR presentMode; VkBool32 clipped; VkSwapchainKHR oldSwapchain; };
struct VkComponentMapping { int r, g, b, a; };
struct VkImageSubresourceRange { VkFlags aspectMask; uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount; };
struct VkImageSubresourceLayers { VkFlags aspectMask; uint32_t mipLevel, baseArrayLayer, layerCount; };
struct VkImageViewCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkImage image; VkImageViewType viewType; VkFormat format; VkComponentMapping components; VkImageSubresourceRange subresourceRange; };
struct VkAttachmentDescription { VkFlags flags; VkFormat format; VkFlags samples; VkAttachmentLoadOp loadOp; VkAttachmentStoreOp storeOp; VkAttachmentLoadOp stencilLoadOp; VkAttachmentStoreOp stencilStoreOp; VkImageLayout initialLayout, finalLayout; };
struct VkAttachmentReference { uint32_t attachment; VkImageLayout layout; };
struct VkSubpassDescription { VkFlags flags; VkPipelineBindPoint pipelineBindPoint; uint32_t inputAttachmentCount; const VkAttachmentReference* pInputAttachments; uint32_t colorAttachmentCount; const VkAttachmentReference* pColorAttachments; const VkAttachmentReference* pResolveAttachments; const VkAttachmentReference* pDepthStencilAttachment; uint32_t preserveAttachmentCount; const uint32_t* pPreserveAttachments; };
struct VkSubpassDependency { uint32_t srcSubpass, dstSubpass; VkPipelineStageFlags srcStageMask, dstStageMask; VkAccessFlags srcAccessMask, dstAccessMask; VkFlags dependencyFlags; };
struct VkRenderPassCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t attachmentCount; const VkAttachmentDescription* pAttachments; uint32_t subpassCount; const VkSubpassDescription* pSubpasses; uint32_t dependencyCount; const VkSubpassDependency* pDependencies; };
struct VkFramebufferCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkRenderPass renderPass; uint32_t attachmentCount; const VkImageView* pAttachments; uint32_t width, height, layers; };
struct VkSemaphoreCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; };
struct VkFenceCreateInfo { VkStructureType sType; const void* pNext; VkFenceCreateFlags flags; };
struct VkShaderModuleCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; size_t codeSize; const uint32_t* pCode; };
struct VkDescriptorSetLayoutBinding { uint32_t binding; VkDescriptorType descriptorType; uint32_t descriptorCount; VkShaderStageFlags stageFlags; const VkSampler* pImmutableSamplers; };
struct VkDescriptorSetLayoutCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t bindingCount; const VkDescriptorSetLayoutBinding* pBindings; };
struct VkPushConstantRange { VkShaderStageFlags stageFlags; uint32_t offset, size; };
struct VkPipelineLayoutCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t setLayoutCount; const VkDescriptorSetLayout* pSetLayouts; uint32_t pushConstantRangeCount; const VkPushConstantRange* pPushConstantRanges; };
struct VkPipelineShaderStageCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkShaderStageFlags stage; VkShaderModule module; const char* pName; const void* pSpecializationInfo; };
struct VkVertexInputBindingDescription { uint32_t binding, stride; VkVertexInputRate inputRate; };
struct VkVertexInputAttributeDescription { uint32_t location, binding; VkFormat format; uint32_t offset; };
struct VkPipelineVertexInputStateCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t vertexBindingDescriptionCount; const VkVertexInputBindingDescription* pVertexBindingDescriptions; uint32_t vertexAttributeDescriptionCount; const VkVertexInputAttributeDescription* pVertexAttributeDescriptions; };
struct VkPipelineInputAssemblyStateCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkPrimitiveTopology topology; VkBool32 primitiveRestartEnable; };
struct VkPipelineViewportStateCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t viewportCount; const VkViewport* pViewports; uint32_t scissorCount; const VkRect2D* pScissors; };
struct VkPipelineRasterizationStateCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkBool32 depthClampEnable, rasterizerDiscardEnable; VkPolygonMode polygonMode; VkCullModeFlags cullMode; VkFrontFace frontFace; VkBool32 depthBiasEnable; float depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor, lineWidth; };
struct VkPipelineMultisampleStateCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkFlags rasterizationSamples; VkBool32 sampleShadingEnable; float minSampleShading; const uint32_t* pSampleMask; VkBool32 alphaToCoverageEnable, alphaToOneEnable; };
struct VkPipelineColorBlendAttachmentState { VkBool32 blendEnable; VkBlendFactor srcColorBlendFactor, dstColorBlendFactor; VkBlendOp colorBlendOp; VkBlendFactor srcAlphaBlendFactor, dstAlphaBlendFactor; VkBlendOp alphaBlendOp; VkColorComponentFlags colorWriteMask; };
struct VkPipelineColorBlendStateCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkBool32 logicOpEnable; int logicOp; uint32_t attachmentCount; const VkPipelineColorBlendAttachmentState* pAttachments; float blendConstants[4]; };
struct VkPipelineDynamicStateCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t dynamicStateCount; const VkDynamicState* pDynamicStates; };
struct VkGraphicsPipelineCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t stageCount; const VkPipelineShaderStageCreateInfo* pStages; const VkPipelineVertexInputStateCreateInfo* pVertexInputState; const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState; const void* pTessellationState; const VkPipelineViewportStateCreateInfo* pViewportState; const VkPipelineRasterizationStateCreateInfo* pRasterizationState; const VkPipelineMultisampleStateCreateInfo* pMultisampleState; const void* pDepthStencilState; const VkPipelineColorBlendStateCreateInfo* pColorBlendState; const VkPipelineDynamicStateCreateInfo* pDynamicState; VkPipelineLayout layout; VkRenderPass renderPass; uint32_t subpass; VkPipeline basePipelineHandle; int32_t basePipelineIndex; };
struct VkBufferCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkDeviceSize size; VkBufferUsageFlags usage; VkSharingMode sharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; };
struct VkMemoryRequirements { VkDeviceSize size, alignment; uint32_t memoryTypeBits; };
struct VkMemoryAllocateInfo { VkStructureType sType; const void* pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; };
struct VkImageCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkImageType imageType; VkFormat format; VkExtent3D extent; uint32_t mipLevels, arrayLayers; VkFlags samples; VkImageTiling tiling; VkImageUsageFlags usage; VkSharingMode sharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; VkImageLayout initialLayout; };
struct VkSamplerCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkFilter magFilter, minFilter; int mipmapMode; VkSamplerAddressMode addressModeU, addressModeV, addressModeW; float mipLodBias; VkBool32 anisotropyEnable; float maxAnisotropy; VkBool32 compareEnable; int compareOp; float minLod, maxLod; int borderColor; VkBool32 unnormalizedCoordinates; };
struct VkDescriptorPoolSize { VkDescriptorType type; uint32_t descriptorCount; };
struct VkDescriptorPoolCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; uint32_t maxSets, poolSizeCount; const VkDescriptorPoolSize* pPoolSizes; };
struct VkDescriptorSetAllocateInfo { VkStructureType sType; const void* pNext; VkDescriptorPool descriptorPool; uint32_t descriptorSetCount; const VkDescriptorSetLayout* pSetLayouts; };
struct VkDescriptorImageInfo { VkSampler sampler; VkImageView imageView; VkImageLayout imageLayout; };
struct VkWriteDescriptorSet { VkStructureType sType; const void* pNext; VkDescriptorSet dstSet; uint32_t dstBinding, dstArrayElement, descriptorCount; VkDescriptorType descriptorType; const VkDescriptorImageInfo* pImageInfo; const void* pBufferInfo; const void* pTexelBufferView; };
struct VkCommandPoolCreateInfo { VkStructureType sType; const void* pNext; VkCommandPoolCreateFlags flags; uint32_t queueFamilyIndex; };
struct VkCommandBufferAllocateInfo { VkStructureType sType; const void* pNext; VkCommandPool commandPool; VkCommandBufferLevel level; uint32_t commandBufferCount; };
struct VkCommandBufferBeginInfo { VkStructureType sType; const void* pNext; VkCommandBufferUsageFlags flags; const void* pInheritanceInfo; };
struct VkImageMemoryBarrier { VkStructureType sType; const void* pNext; VkAccessFlags srcAccessMask, dstAccessMask; VkImageLayout oldLayout, newLayout; uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex; VkImage image; VkImageSubresourceRange subresourceRange; };
struct VkBufferImageCopy { VkDeviceSize bufferOffset; uint32_t bufferRowLength, bufferImageHeight; VkImageSubresourceLayers imageSubresource; int imageOffset[3]; VkExtent3D imageExtent; };
union VkClearColorValue { float float32[4]; int32_t int32[4]; uint32_t uint32[4]; };
struct VkClearDepthStencilValue { float depth; uint32_t stencil; };
union VkClearValue { VkClearColorValue color; VkClearDepthStencilValue depthStencil; };
struct VkRenderPassBeginInfo { VkStructureType sType; const void* pNext; VkRenderPass renderPass; VkFramebuffer framebuffer; VkRect2D renderArea; uint32_t clearValueCount; const VkClearValue* pClearValues; };
struct VkSubmitInfo { VkStructureType sType; const void* pNext; uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores; const VkPipelineStageFlags* pWaitDstStageMask; uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers; uint32_t signalSemaphoreCount; const VkSemaphore* pSignalSemaphores; };
struct VkPresentInfoKHR { VkStructureType sType; const void* pNext; uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores; uint32_t swapchainCount; const VkSwapchainKHR* pSwapchains; const uint32_t* pImageIndices; VkResult* pResults; };
struct VkAllocationCallbacks;
typedef void (*PFN_vkVoidFunction)(void);

#define VKFN(ret, name, args) ret name args; typedef ret (*PFN_##name) args;
VKFN(PFN_vkVoidFunction, vkGetInstanceProcAddr, (VkInstance, const char*))
VKFN(PFN_vkVoidFunction, vkGetDeviceProcAddr, (VkDevice, const char*))
VKFN(VkResult, vkCreateInstance, (const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*))
VKFN(void, vkDestroyInstance, (VkInstance, const VkAllocationCallbacks*))
VKFN(VkResult, vkEnumeratePhysicalDevices, (VkInstance, uint32_t*, VkPhysicalDevice*))
VKFN(void, vkGetPhysicalDeviceProperties, (VkPhysicalDevice, VkPhysicalDeviceProperties*))
VKFN(void, vkGetPhysicalDeviceQueueFamilyProperties, (VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*))
VKFN(void, vkGetPhysicalDeviceMemoryProperties, (VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*))
VKFN(VkResult, vkCreateDevice, (VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*))
VKFN(VkResult, vkEnumerateDeviceExtensionProperties, (VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*))
VKFN(void, vkDestroySurfaceKHR, (VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*))
VKFN(VkResult, vkGetPhysicalDeviceSurfaceSupportKHR, (VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*))
VKFN(VkResult, vkGetPhysicalDeviceSurfaceCapabilitiesKHR, (VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*))
VKFN(VkResult, vkGetPhysicalDeviceSurfaceFormatsKHR, (VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*))
VKFN(VkResult, vkGetPhysicalDeviceSurfacePresentModesKHR, (VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*))
VKFN(VkResult, vkCreateWin32SurfaceKHR, (VkInstance, const VkWin32SurfaceCreateInfoKHR*, const VkAllocationCallbacks*, VkSurfaceKHR*))
VKFN(void, vkDestroyDevice, (VkDevice, const VkAllocationCallbacks*))
VKFN(void, vkGetDeviceQueue, (VkDevice, uint32_t, uint32_t, VkQueue*))
VKFN(VkResult, vkDeviceWaitIdle, (VkDevice))
VKFN(VkResult, vkCreateSwapchainKHR, (VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*))
VKFN(void, vkDestroySwapchainKHR, (VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*))
VKFN(VkResult, vkGetSwapchainImagesKHR, (VkDevice, VkSwapchainKHR, uint32_t*, VkImage*))
VKFN(VkResult, vkAcquireNextImageKHR, (VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*))
VKFN(VkResult, vkQueuePresentKHR, (VkQueue, const VkPresentInfoKHR*))
VKFN(VkResult, vkQueueSubmit, (VkQueue, uint32_t, const VkSubmitInfo*, VkFence))
VKFN(VkResult, vkCreateImageView, (VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*))
VKFN(void, vkDestroyImageView, (VkDevice, VkImageView, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreateRenderPass, (VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*))
VKFN(void, vkDestroyRenderPass, (VkDevice, VkRenderPass, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreateFramebuffer, (VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*))
VKFN(void, vkDestroyFramebuffer, (VkDevice, VkFramebuffer, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreateShaderModule, (VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*))
VKFN(void, vkDestroyShaderModule, (VkDevice, VkShaderModule, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreateDescriptorSetLayout, (VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*))
VKFN(void, vkDestroyDescriptorSetLayout, (VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreatePipelineLayout, (VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*))
VKFN(void, vkDestroyPipelineLayout, (VkDevice, VkPipelineLayout, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreateGraphicsPipelines, (VkDevice, void*, uint32_t, const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*))
VKFN(void, vkDestroyPipeline, (VkDevice, VkPipeline, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreateDescriptorPool, (VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*))
VKFN(void, vkDestroyDescriptorPool, (VkDevice, VkDescriptorPool, const VkAllocationCallbacks*))
VKFN(VkResult, vkAllocateDescriptorSets, (VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*))
VKFN(void, vkUpdateDescriptorSets, (VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*))
VKFN(VkResult, vkCreateSampler, (VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler*))
VKFN(void, vkDestroySampler, (VkDevice, VkSampler, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreateImage, (VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*))
VKFN(void, vkDestroyImage, (VkDevice, VkImage, const VkAllocationCallbacks*))
VKFN(void, vkGetImageMemoryRequirements, (VkDevice, VkImage, VkMemoryRequirements*))
VKFN(VkResult, vkAllocateMemory, (VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*))
VKFN(void, vkFreeMemory, (VkDevice, VkDeviceMemory, const VkAllocationCallbacks*))
VKFN(VkResult, vkBindImageMemory, (VkDevice, VkImage, VkDeviceMemory, VkDeviceSize))
VKFN(VkResult, vkCreateBuffer, (VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*))
VKFN(void, vkDestroyBuffer, (VkDevice, VkBuffer, const VkAllocationCallbacks*))
VKFN(void, vkGetBufferMemoryRequirements, (VkDevice, VkBuffer, VkMemoryRequirements*))
VKFN(VkResult, vkBindBufferMemory, (VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize))
VKFN(VkResult, vkMapMemory, (VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**))
VKFN(void, vkUnmapMemory, (VkDevice, VkDeviceMemory))
VKFN(VkResult, vkCreateCommandPool, (VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*))
VKFN(void, vkDestroyCommandPool, (VkDevice, VkCommandPool, const VkAllocationCallbacks*))
VKFN(VkResult, vkAllocateCommandBuffers, (VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*))
VKFN(VkResult, vkBeginCommandBuffer, (VkCommandBuffer, const VkCommandBufferBeginInfo*))
VKFN(VkResult, vkEndCommandBuffer, (VkCommandBuffer))
VKFN(VkResult, vkResetCommandBuffer, (VkCommandBuffer, VkFlags))
VKFN(void, vkCmdBeginRenderPass, (VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents))
VKFN(void, vkCmdEndRenderPass, (VkCommandBuffer))
VKFN(void, vkCmdBindPipeline, (VkCommandBuffer, VkPipelineBindPoint, VkPipeline))
VKFN(void, vkCmdBindDescriptorSets, (VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*))
VKFN(void, vkCmdBindVertexBuffers, (VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*))
VKFN(void, vkCmdBindIndexBuffer, (VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType))
VKFN(void, vkCmdPushConstants, (VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*))
VKFN(void, vkCmdSetViewport, (VkCommandBuffer, uint32_t, uint32_t, const VkViewport*))
VKFN(void, vkCmdSetScissor, (VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*))
VKFN(void, vkCmdDrawIndexed, (VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t))
VKFN(void, vkCmdPipelineBarrier, (VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkFlags, uint32_t, const void*, uint32_t, const void*, uint32_t, const VkImageMemoryBarrier*))
VKFN(void, vkCmdCopyBufferToImage, (VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy*))
VKFN(VkResult, vkCreateSemaphore, (VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*))
VKFN(void, vkDestroySemaphore, (VkDevice, VkSemaphore, const VkAllocationCallbacks*))
VKFN(VkResult, vkCreateFence, (VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence*))
VKFN(void, vkDestroyFence, (VkDevice, VkFence, const VkAllocationCallbacks*))
VKFN(VkResult, vkWaitForFences, (VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t))
VKFN(VkResult, vkResetFences, (VkDevice, uint32_t, const VkFence*))
