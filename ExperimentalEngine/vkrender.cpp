#define VMA_IMPLEMENTATION
#include "PCH.hpp"
#include "Engine.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "imgui_impl_vulkan.h"
#include "physfs.hpp"
#include "Transform.hpp"
#include "tracy/Tracy.hpp"
#include "tracy/TracyC.h"
#include "tracy/TracyVulkan.hpp"
#include "XRInterface.hpp"
#include "RenderPasses.hpp"
#include "Input.hpp"
#include "OpenVRInterface.hpp"
#include <sajson.h>
#include "Fatal.hpp"
#include <unordered_set>
#include "Log.hpp"
#include "ObjModelLoader.hpp"
#include "Render.hpp"
#include "SourceModelLoader.hpp"

using namespace worlds;

const bool vrValidationLayers = false;

uint32_t findPresentQueue(vk::PhysicalDevice pd, vk::SurfaceKHR surface) {
    auto qprops = pd.getQueueFamilyProperties();
    for (uint32_t qi = 0; qi != qprops.size(); ++qi) {
        auto& qprop = qprops[qi];
        if (pd.getSurfaceSupportKHR(qi, surface) && (qprop.queueFlags & vk::QueueFlagBits::eGraphics) == vk::QueueFlagBits::eGraphics) {
            return qi;
        }
    }
    return ~0u;
}

RenderImageHandle VKRenderer::createRTResource(RTResourceCreateInfo resourceCreateInfo, const char* debugName) {
    auto memProps = physicalDevice.getMemoryProperties();
    RenderTextureResource rtr;
    rtr.image = vku::GenericImage{ *device, memProps, resourceCreateInfo.ici, resourceCreateInfo.viewType, resourceCreateInfo.aspectFlags, false, debugName };
    rtr.aspectFlags = resourceCreateInfo.aspectFlags;

    RenderImageHandle handle = lastHandle++;
    rtResources.insert({ handle, std::move(rtr) });
    return handle;
}

void VKRenderer::createSwapchain(vk::SwapchainKHR oldSwapchain) {
    vk::PresentModeKHR presentMode = (useVsync && !enableVR) ? vk::PresentModeKHR::eFifoRelaxed : vk::PresentModeKHR::eImmediate;
    QueueFamilyIndices qfi{ graphicsQueueFamilyIdx, presentQueueFamilyIdx };
    swapchain = std::make_unique<Swapchain>(physicalDevice, *device, surface, qfi, oldSwapchain, presentMode);
    swapchain->getSize(&width, &height);

    if (!enableVR) {
        renderWidth = width;
        renderHeight = height;
    }

    vku::executeImmediately(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), [this](vk::CommandBuffer cb) {
        for (auto& img : swapchain->images)
            vku::transitionLayout(cb, img, vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::AccessFlags{}, vk::AccessFlagBits::eMemoryRead);
        });
}

void VKRenderer::createFramebuffers() {
    for (int i = 0; i != swapchain->imageViews.size(); i++) {
        vk::ImageView attachments[1] = { swapchain->imageViews[i] };
        vk::FramebufferCreateInfo fci;
        fci.attachmentCount = 1;
        fci.pAttachments = attachments;
        fci.width = this->width;
        fci.height = this->height;
        fci.renderPass = irp->getRenderPass();
        fci.layers = 1;
        this->framebuffers.push_back(this->device->createFramebufferUnique(fci));
    }
}

VKRenderer::VKRenderer(const RendererInitInfo& initInfo, bool* success)
    : window(initInfo.window)
    , frameIdx(0)
    , lastHandle(0)
    , polyImage(std::numeric_limits<uint32_t>::max())
    , shadowmapImage(std::numeric_limits<uint32_t>::max())
    , shadowmapRes(1024)
    , enableVR(initInfo.enableVR)
    , vrPredictAmount(0.033f)
    , clearMaterialIndices(false)
    , irp(nullptr)
    , lowLatencyMode("r_lowLatency", "0", "Waits for GPU completion before starting the next frame. Has a significant impact on latency when VSync is enabled."){
    msaaSamples = vk::SampleCountFlagBits::e4;
    numMSAASamples = 4;

    vku::InstanceMaker instanceMaker;
    instanceMaker.apiVersion(VK_API_VERSION_1_2);
    unsigned int extCount;
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr);

    std::vector<const char*> names(extCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, names.data());

    std::vector<std::string> instanceExtensions;

    for (auto extName : names)
        instanceExtensions.push_back(extName);

    for (auto& extName : initInfo.additionalInstanceExtensions)
        instanceExtensions.push_back(extName);

    if (initInfo.enableVR && initInfo.activeVrApi == VrApi::OpenVR) {
        OpenVRInterface* vrInterface = static_cast<OpenVRInterface*>(initInfo.vrInterface);
        auto vrInstExts = vrInterface->getVulkanInstanceExtensions();

        for (auto& extName : vrInstExts) {
            if (std::find_if(instanceExtensions.begin(), instanceExtensions.end(), [&extName](std::string val) { return val == extName; }) != instanceExtensions.end()) {
                continue;
            }
            instanceExtensions.push_back(extName);
        }
    }

    for (auto& v : vk::enumerateInstanceExtensionProperties()) {
        logMsg(WELogCategoryRender, "supported extension: %s", v.extensionName);
    }

    for (auto& e : instanceExtensions) {
        logMsg(WELogCategoryRender, "activating extension: %s", e.c_str());
        instanceMaker.extension(e.c_str());
    }

#ifndef NDEBUG
    if (!enableVR || vrValidationLayers) {
        instanceMaker.layer("VK_LAYER_KHRONOS_validation");
        instanceMaker.extension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }
#endif
    instanceMaker.extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    instanceMaker.applicationName("Experimental Game")
        .engineName("Experimental Engine")
        .applicationVersion(1)
        .engineVersion(1);

    this->instance = instanceMaker.createUnique();
#ifndef NDEBUG
    if (!enableVR || vrValidationLayers)
        dbgCallback = vku::DebugCallback(*this->instance);
#endif
    auto physDevs = this->instance->enumeratePhysicalDevices();
    // TODO: Go through physical devices and select one properly
    this->physicalDevice = physDevs[0];

    auto memoryProps = this->physicalDevice.getMemoryProperties();

    auto physDevProps = physicalDevice.getProperties();
    logMsg(worlds::WELogCategoryRender, "Physical device:\n");
    logMsg(worlds::WELogCategoryRender, "\t-Name: %s", physDevProps.deviceName);
    logMsg(worlds::WELogCategoryRender, "\t-ID: %u", physDevProps.deviceID);
    logMsg(worlds::WELogCategoryRender, "\t-Vendor ID: %u", physDevProps.vendorID);
    logMsg(worlds::WELogCategoryRender, "\t-Device Type: %s", vk::to_string(physDevProps.deviceType).c_str());
    logMsg(worlds::WELogCategoryRender, "\t-Driver Version: %u", physDevProps.driverVersion);
    logMsg(worlds::WELogCategoryRender, "\t-Memory heap count: %u", memoryProps.memoryHeapCount);
    logMsg(worlds::WELogCategoryRender, "\t-Memory type count: %u", memoryProps.memoryTypeCount);

    vk::DeviceSize totalVram = 0;
    for (uint32_t i = 0; i < memoryProps.memoryHeapCount; i++) {
        auto& heap = memoryProps.memoryHeaps[i];
        totalVram += heap.size;
        logMsg(worlds::WELogCategoryRender, "Heap %i: %hu MB", i, heap.size / 1024 / 1024);
    }

    for (uint32_t i = 0; i < memoryProps.memoryTypeCount; i++) {
        auto& memType = memoryProps.memoryTypes[i];
        logMsg(worlds::WELogCategoryRender, "Memory type for heap %i: %s", memType.heapIndex, vk::to_string(memType.propertyFlags).c_str());
    }

    logMsg(worlds::WELogCategoryRender, "Approx. %hu MB total accessible graphics memory (NOT VRAM!)", totalVram / 1024 / 1024);

    auto qprops = this->physicalDevice.getQueueFamilyProperties();
    const auto badQueue = ~(uint32_t)0;
    graphicsQueueFamilyIdx = badQueue;
    computeQueueFamilyIdx = badQueue;
    vk::QueueFlags search = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;

    for (auto& qprop : qprops) {
        logMsg(worlds::WELogCategoryRender, "Queue family with properties %s (supports present: %i)", vk::to_string(qprop.queueFlags).c_str());
    }

    // Look for a queue family with both graphics and
    // compute first.
    for (uint32_t qi = 0; qi != qprops.size(); ++qi) {
        auto& qprop = qprops[qi];
        if ((qprop.queueFlags & search) == search) {
            this->graphicsQueueFamilyIdx = qi;
            this->computeQueueFamilyIdx = qi;
            break;
        }
    }

    // Search for async compute queue family
    asyncComputeQueueFamilyIdx = badQueue;
    for (int i = 0; i < qprops.size(); i++) {
        auto& qprop = qprops[i];
        if ((qprop.queueFlags & (vk::QueueFlagBits::eCompute)) == vk::QueueFlagBits::eCompute && i != computeQueueFamilyIdx) {
            asyncComputeQueueFamilyIdx = i;
            break;
        }
    }

    if (asyncComputeQueueFamilyIdx == badQueue)
        logWarn(worlds::WELogCategoryRender, "Couldn't find async compute queue");

    if (this->graphicsQueueFamilyIdx == badQueue || this->computeQueueFamilyIdx == badQueue) {
        *success = false;
        return;
    }

    vku::DeviceMaker dm{};
    dm.defaultLayers();
    dm.queue(this->graphicsQueueFamilyIdx);

    for (auto& ext : initInfo.additionalDeviceExtensions) {
        dm.extension(ext.c_str());
    }

    // Stupid workaround: putting this vector inside the if
    // causes it to go out of scope, making all the const char*
    // extension strings become invalid and screwing
    // everything up
    std::vector<std::string> vrDevExts;
    if (initInfo.enableVR && initInfo.activeVrApi == VrApi::OpenVR) {
        OpenVRInterface* vrInterface = static_cast<OpenVRInterface*>(initInfo.vrInterface);
        vrDevExts = vrInterface->getVulkanDeviceExtensions(physicalDevice);
        for (auto& extName : vrDevExts) {
            dm.extension(extName.c_str());
        }
    }

    vk::PhysicalDeviceFeatures supportedFeatures = physicalDevice.getFeatures();
    if (!supportedFeatures.shaderStorageImageMultisample) {
        *success = false;
        logWarn(worlds::WELogCategoryRender, "Missing shaderStorageImageMultisample");
        return;
    }

    if (!supportedFeatures.fragmentStoresAndAtomics) {
        logWarn(worlds::WELogCategoryRender, "Missing fragmentStoresAndAtomics");
    }

    if (!supportedFeatures.fillModeNonSolid) {
        logWarn(worlds::WELogCategoryRender, "Missing fillModeNonSolid");
    }

    if (!supportedFeatures.wideLines) {
        logWarn(worlds::WELogCategoryRender, "Missing wideLines");
    }

    vk::PhysicalDeviceFeatures features;
    features.shaderStorageImageMultisample = true;
    features.fragmentStoresAndAtomics = true;
    features.fillModeNonSolid = true;
    features.wideLines = true;
    features.samplerAnisotropy = true;
    dm.setFeatures(features);

    vk::PhysicalDeviceVulkan12Features vk12Features;
    vk12Features.timelineSemaphore = true;
    vk12Features.descriptorBindingPartiallyBound = true;
    vk12Features.runtimeDescriptorArray = true;
    dm.setPNext(&vk12Features);

    if (this->computeQueueFamilyIdx != this->graphicsQueueFamilyIdx) dm.queue(this->computeQueueFamilyIdx);
    this->device = dm.createUnique(this->physicalDevice);

    VmaAllocatorCreateInfo allocatorCreateInfo;
    memset(&allocatorCreateInfo, 0, sizeof(allocatorCreateInfo));
    allocatorCreateInfo.device = *device;
    allocatorCreateInfo.frameInUseCount = 0;
    allocatorCreateInfo.instance = *instance;
    allocatorCreateInfo.physicalDevice = physicalDevice;
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    allocatorCreateInfo.flags = 0;
    vmaCreateAllocator(&allocatorCreateInfo, &allocator);

    vk::PipelineCacheCreateInfo pipelineCacheInfo{};
    this->pipelineCache = this->device->createPipelineCacheUnique(pipelineCacheInfo);

    std::vector<vk::DescriptorPoolSize> poolSizes;
    poolSizes.emplace_back(vk::DescriptorType::eUniformBuffer, 1024);
    poolSizes.emplace_back(vk::DescriptorType::eCombinedImageSampler, 1024);
    poolSizes.emplace_back(vk::DescriptorType::eStorageBuffer, 1024);

    // Create an arbitrary number of descriptors in a pool.
    // Allow the descriptors to be freed, possibly not optimal behaviour.
    vk::DescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    descriptorPoolInfo.maxSets = 256;
    descriptorPoolInfo.poolSizeCount = (uint32_t)poolSizes.size();
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    this->descriptorPool = this->device->createDescriptorPoolUnique(descriptorPoolInfo);

    VkSurfaceKHR surface;
    SDL_Vulkan_CreateSurface(window, *this->instance, &surface);

    this->surface = surface;
    this->presentQueueFamilyIdx = findPresentQueue(this->physicalDevice, this->surface);

    vk::SemaphoreCreateInfo sci;
    this->imageAcquire = this->device->createSemaphoreUnique(sci);
    this->commandComplete = this->device->createSemaphoreUnique(sci);

    vk::CommandPoolCreateInfo cpci;
    cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    cpci.queueFamilyIndex = this->graphicsQueueFamilyIdx;
    this->commandPool = this->device->createCommandPoolUnique(cpci);

    createSwapchain(vk::SwapchainKHR{});

    if (initInfo.activeVrApi == VrApi::OpenVR) {
        OpenVRInterface* vrInterface = static_cast<OpenVRInterface*>(initInfo.vrInterface);
        vrInterface->getRenderResolution(&renderWidth, &renderHeight);
    }

    auto vkCtx = std::make_shared<VulkanCtx>(VulkanCtx{
        physicalDevice,
        *device,
        *pipelineCache,
        *descriptorPool,
        *commandPool,
        *instance,
        allocator,
        graphicsQueueFamilyIdx,
        GraphicsSettings {
            numMSAASamples,
            (int)shadowmapRes,
            enableVR
        },
        width, height,
        renderWidth, renderHeight
        });

    texSlots = std::make_unique<TextureSlots>(vkCtx);
    matSlots = std::make_unique<MaterialSlots>(vkCtx, *texSlots);
    cubemapSlots = std::make_unique<CubemapSlots>(vkCtx);

    vk::ImageCreateInfo brdfLutIci{ vk::ImageCreateFlags{}, vk::ImageType::e2D, vk::Format::eR16G16Sfloat, vk::Extent3D{512,512,1}, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::SharingMode::eExclusive, graphicsQueueFamilyIdx };
    brdfLut = vku::GenericImage{ *device, memoryProps, brdfLutIci, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor, false, "BRDF LUT" };

    cubemapConvoluter = std::make_unique<CubemapConvoluter>(vkCtx);

    vku::executeImmediately(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), [&](auto cb) {
        brdfLut.setLayout(cb, vk::ImageLayout::eColorAttachmentOptimal);
        });

    BRDFLUTRenderer brdfLutRenderer{ *vkCtx };
    brdfLutRenderer.render(*vkCtx, brdfLut);

    createSCDependents();

    vk::CommandBufferAllocateInfo cbai;
    cbai.commandPool = *this->commandPool;
    cbai.commandBufferCount = 4;
    cbai.level = vk::CommandBufferLevel::ePrimary;
    this->cmdBufs = this->device->allocateCommandBuffersUnique(cbai);

    for (int i = 0; i < this->cmdBufs.size(); i++) {
        vk::FenceCreateInfo fci;
        fci.flags = vk::FenceCreateFlagBits::eSignaled;
        vk::SemaphoreCreateInfo sci;
        vk::SemaphoreTypeCreateInfo stci;
        stci.initialValue = 0;
        stci.semaphoreType = vk::SemaphoreType::eTimeline;
        sci.pNext = &stci;
        this->cmdBufferSemaphores.push_back(this->device->createSemaphore(sci));
        this->cmdBufSemaphoreVals.push_back(0);

        vk::CommandBuffer cb = *this->cmdBufs[i];
        vk::CommandBufferBeginInfo cbbi;
        cb.begin(cbbi);
        cb.end();
    }

    timestampPeriod = physDevProps.limits.timestampPeriod;

    vk::QueryPoolCreateInfo qpci{};
    qpci.queryType = vk::QueryType::eTimestamp;
    qpci.queryCount = 2;
    queryPool = device->createQueryPoolUnique(qpci);


    *success = true;
#ifdef TRACY_ENABLE
    for (auto& cmdBuf : cmdBufs) {
        tracyContexts.push_back(tracy::CreateVkContext(physicalDevice, *device, device->getQueue(graphicsQueueFamilyIdx, 0), *cmdBufs[0]));
    }
#endif

    if (enableVR) {
        if (initInfo.activeVrApi == VrApi::OpenXR) {
            XrGraphicsBindingVulkanKHR graphicsBinding;
            graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
            graphicsBinding.instance = *instance;
            graphicsBinding.queueFamilyIndex = graphicsQueueFamilyIdx;
            graphicsBinding.queueIndex = 0;
            graphicsBinding.device = *device;
            graphicsBinding.physicalDevice = physicalDevice;
            graphicsBinding.next = nullptr;

            ((XRInterface*)initInfo.vrInterface)->createSession(graphicsBinding);
        } else if (initInfo.activeVrApi == VrApi::OpenVR) {
            vr::VRCompositor()->SetExplicitTimingMode(vr::EVRCompositorTimingMode::VRCompositorTimingMode_Explicit_RuntimePerformsPostPresentHandoff);
        }

        vrInterface = initInfo.vrInterface;
        vrApi = initInfo.activeVrApi;
    }

    uint32_t s = cubemapSlots->loadOrGet(g_assetDB.addOrGetExisting("DefaultCubemap.json"));
    cubemapConvoluter->convolute((*cubemapSlots)[s]);
}

// Quite a lot of resources are dependent on either the number of images
// there are in the swap chain or the swapchain itself, so they need to be
// recreated whenever the swap chain changes.
void VKRenderer::createSCDependents() {
    auto memoryProps = physicalDevice.getMemoryProperties();

    if (rtResources.count(polyImage) != 0) {
        rtResources.erase(polyImage);
    }

    if (rtResources.count(depthStencilImage) != 0) {
        rtResources.erase(depthStencilImage);
    }

    if (rtResources.count(imguiImage) != 0) {
        rtResources.erase(imguiImage);
    }

    if (rtResources.count(finalPrePresent) != 0) {
        rtResources.erase(finalPrePresent);
    }

    if (rtResources.count(finalPrePresentR) != 0) {
        rtResources.erase(finalPrePresentR);
    }

    vk::ImageCreateInfo ici;
    ici.imageType = vk::ImageType::e2D;
    ici.extent = vk::Extent3D{ renderWidth, renderHeight, 1 };
    ici.arrayLayers = enableVR ? 2 : 1;
    ici.mipLevels = 1;
    ici.format = vk::Format::eR16G16B16A16Sfloat;
    ici.initialLayout = vk::ImageLayout::eUndefined;
    ici.samples = msaaSamples;
    ici.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

    RTResourceCreateInfo polyCreateInfo{ ici, enableVR ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor };
    polyImage = createRTResource(polyCreateInfo, "Poly Image");

    ici.format = vk::Format::eD32Sfloat;
    ici.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    RTResourceCreateInfo depthCreateInfo{ ici, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eDepth };
    depthStencilImage = createRTResource(depthCreateInfo, "Depth Stencil Image");

    ici.format = vk::Format::eR8G8B8A8Unorm;
    ici.samples = vk::SampleCountFlagBits::e1;
    ici.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

    RTResourceCreateInfo imguiImageCreateInfo{ ici, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor };
    imguiImage = createRTResource(imguiImageCreateInfo, "ImGui Image");

    vk::ImageCreateInfo shadowmapIci;
    shadowmapIci.imageType = vk::ImageType::e2D;
    shadowmapIci.extent = vk::Extent3D{ shadowmapRes, shadowmapRes, 1 };
    shadowmapIci.arrayLayers = 1;
    shadowmapIci.mipLevels = 1;
    shadowmapIci.format = vk::Format::eD32Sfloat;
    shadowmapIci.initialLayout = vk::ImageLayout::eUndefined;
    shadowmapIci.samples = vk::SampleCountFlagBits::e1;
    shadowmapIci.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;

    RTResourceCreateInfo shadowmapCreateInfo{ shadowmapIci, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eDepth };
    shadowmapImage = createRTResource(shadowmapCreateInfo, "Shadowmap Image");

    //delete irp;
    graphSolver.clear();
    {
        auto srp = new ShadowmapRenderPass(shadowmapImage);
        graphSolver.addNode(srp);
    }

    {
        auto prp = new PolyRenderPass(depthStencilImage, polyImage, shadowmapImage, !enableVR);
        currentPRP = prp;
        graphSolver.addNode(prp);
    }

    ici.arrayLayers = 1;
    ici.samples = vk::SampleCountFlagBits::e1;
    ici.format = vk::Format::eR8G8B8A8Unorm;
    ici.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;

    RTResourceCreateInfo finalPrePresentCI{ ici, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor };
    finalPrePresent = createRTResource(finalPrePresentCI, "Final Pre-Present Image");

    if (enableVR) {
        finalPrePresentR = createRTResource(finalPrePresentCI, "Final Pre-Present Image (Right Eye)");
    }

    PassSetupCtx psc{ physicalDevice, *device, *pipelineCache, *descriptorPool, *commandPool, *instance, allocator, graphicsQueueFamilyIdx, GraphicsSettings{numMSAASamples, (int32_t)shadowmapRes, enableVR}, &texSlots, &cubemapSlots, &matSlots, rtResources, (int)swapchain->images.size(), enableVR, &brdfLut };

    auto tonemapRP = new TonemapRenderPass(polyImage, finalPrePresent);
    graphSolver.addNode(tonemapRP);

    if (irp == nullptr) {
        irp = new ImGuiRenderPass(*swapchain);
        irp->setup(psc);
    }

    createFramebuffers();

    vku::executeImmediately(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), [this](vk::CommandBuffer cmdBuf) {
        rtResources.at(polyImage).image.setLayout(cmdBuf, vk::ImageLayout::eGeneral);
        rtResources.at(finalPrePresent).image.setLayout(cmdBuf, vk::ImageLayout::eTransferSrcOptimal);
        if (enableVR) {
            rtResources.at(finalPrePresentR).image.setLayout(cmdBuf, vk::ImageLayout::eTransferSrcOptimal);
        }
        });

    auto solved = graphSolver.solve();

    for (auto& node : solved) {
        node->setup(psc);
    }

    if (enableVR) {
        tonemapRP->setRightFinalImage(psc, finalPrePresentR);
    }

    irp->handleResize(psc, finalPrePresent);
}

void VKRenderer::recreateSwapchain() {
    // Wait for current frame to finish
    device->waitIdle();

    // Check width/height - if it's 0, just ignore it
    auto surfaceCaps = this->physicalDevice.getSurfaceCapabilitiesKHR(this->surface);
    logMsg(WELogCategoryRender, "Recreating swapchain: New surface size is %ix%i", 
        surfaceCaps.currentExtent.width, surfaceCaps.currentExtent.height);

    if (surfaceCaps.currentExtent.width > 0 && surfaceCaps.currentExtent.height > 0) {
        this->width = surfaceCaps.currentExtent.width;
        this->height = surfaceCaps.currentExtent.height;
    }

    if (!enableVR) {
        renderWidth = width;
        renderHeight = height;
    }

    if (surfaceCaps.currentExtent.width == 0 || surfaceCaps.currentExtent.height == 0) {
        isMinimised = true;
        return;
    } else {
        isMinimised = false;
    }

    std::unique_ptr<Swapchain> oldSwapchain = std::move(swapchain);

    createSwapchain(*oldSwapchain->getSwapchain());

    framebuffers.clear();
    oldSwapchain.reset();
    imageAcquire.reset();
    vk::SemaphoreCreateInfo sci;
    imageAcquire = device->createSemaphoreUnique(sci);

    createSCDependents();

    swapchainRecreated = true;
}

void VKRenderer::presentNothing(uint32_t imageIndex) {
    vk::Semaphore waitSemaphore = *imageAcquire;

    vk::PresentInfoKHR presentInfo;
    vk::SwapchainKHR cSwapchain = *swapchain->getSwapchain();
    presentInfo.pSwapchains = &cSwapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    vk::CommandBufferBeginInfo cbbi;
    auto& cmdBuf = cmdBufs[imageIndex];
    cmdBuf->begin(cbbi);
    cmdBuf->end();
    vk::SubmitInfo submitInfo;
    vk::CommandBuffer cCmdBuf = *cmdBuf;
    submitInfo.pCommandBuffers = &cCmdBuf;
    submitInfo.commandBufferCount = 1;
    device->getQueue(presentQueueFamilyIdx, 0).submit(submitInfo, nullptr);

    device->getQueue(presentQueueFamilyIdx, 0).presentKHR(presentInfo);
}

void imageBarrier(vk::CommandBuffer& cb, vk::Image image, vk::ImageLayout layout, vk::AccessFlags srcMask, vk::AccessFlags dstMask, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor, uint32_t numLayers = 1) {
    vk::ImageMemoryBarrier imageMemoryBarriers = {};
    imageMemoryBarriers.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarriers.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarriers.oldLayout = layout;
    imageMemoryBarriers.newLayout = layout;
    imageMemoryBarriers.image = image;
    imageMemoryBarriers.subresourceRange = { aspectMask, 0, 1, 0, numLayers };

    // Put barrier on top
    vk::DependencyFlags dependencyFlags{};

    imageMemoryBarriers.srcAccessMask = srcMask;
    imageMemoryBarriers.dstAccessMask = dstMask;
    auto memoryBarriers = nullptr;
    auto bufferMemoryBarriers = nullptr;
    cb.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers);
}

void VKRenderer::imageBarrier(vk::CommandBuffer& cb, ImageBarrier& ib) {
    vk::ImageMemoryBarrier imageMemoryBarriers = {};
    imageMemoryBarriers.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarriers.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarriers.oldLayout = ib.oldLayout;
    imageMemoryBarriers.newLayout = ib.newLayout;
    imageMemoryBarriers.image = rtResources.at(ib.handle).image.image();
    imageMemoryBarriers.subresourceRange = { ib.aspectMask, 0, 1, 0, rtResources.at(ib.handle).image.info().arrayLayers };

    // Put barrier on top
    vk::DependencyFlags dependencyFlags{};

    imageMemoryBarriers.srcAccessMask = ib.srcMask;
    imageMemoryBarriers.dstAccessMask = ib.dstMask;
    auto memoryBarriers = nullptr;
    auto bufferMemoryBarriers = nullptr;
    cb.pipelineBarrier(ib.srcStage, ib.dstStage, dependencyFlags, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers);
}

vku::ShaderModule VKRenderer::loadShaderAsset(AssetID id) {
    PHYSFS_File* file = g_assetDB.openAssetFileRead(id);
    size_t size = PHYSFS_fileLength(file);
    void* buffer = std::malloc(size);

    size_t readBytes = PHYSFS_readBytes(file, buffer, size);
    assert(readBytes == size);
    PHYSFS_close(file);

    vku::ShaderModule sm{ *device, static_cast<uint32_t*>(buffer), readBytes };
    std::free(buffer);
    return sm;
}

uint32_t nextImageIdx = 0;
bool firstFrame = true;

void VKRenderer::acquireSwapchainImage(uint32_t* imageIdx) {
    vk::Result nextImageRes = swapchain->acquireImage(*device, *imageAcquire, imageIdx);

    if ((nextImageRes == vk::Result::eSuboptimalKHR || nextImageRes == vk::Result::eErrorOutOfDateKHR) && width != 0 && height != 0) {
        recreateSwapchain();
        
        // acquire image from new swapchain
        swapchain->acquireImage(*device, *imageAcquire, imageIdx);
    }
}

bool lowLatencyLast = false;

void VKRenderer::frame(Camera& cam, entt::registry& reg) {
    ZoneScoped;
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    currentPRP->setPickCoords(mx, my);

    uint32_t imageIndex = nextImageIdx;

    if (!lowLatencyMode.getInt() || !lowLatencyLast) {
        vk::SemaphoreWaitInfo swi;
        swi.pSemaphores = &cmdBufferSemaphores[imageIndex];
        swi.semaphoreCount = 1;
        swi.pValues = &cmdBufSemaphoreVals[imageIndex];
        device->waitSemaphores(swi, UINT64_MAX);
    }

    destroyTempTexBuffers(imageIndex);

    if (!lowLatencyMode.getInt() || (!lowLatencyLast && lowLatencyMode.getInt())) {
        if (!(lowLatencyLast && !lowLatencyMode.getInt()))
            acquireSwapchainImage(&imageIndex);
        lowLatencyLast = false;
    }

    if (swapchainRecreated) {
        if (lowLatencyMode.getInt())
            acquireSwapchainImage(&nextImageIdx);
        swapchainRecreated = false;
    }

    std::vector<RenderPass*> solvedNodes = graphSolver.solve();
    std::unordered_map<RenderImageHandle, vk::ImageAspectFlagBits> rtAspects;

    for (auto& pair : rtResources) {
        rtAspects.insert({ pair.first, pair.second.aspectFlags });
    }

    std::vector<std::vector<ImageBarrier>> barriers = graphSolver.createImageBarriers(solvedNodes, rtAspects);

    auto& cmdBuf = cmdBufs[imageIndex];

    RenderCtx rCtx{ cmdBuf, reg, imageIndex, cam, rtResources, renderWidth, renderHeight, loadedMeshes };
    rCtx.enableVR = enableVR;
    rCtx.materialSlots = &matSlots;
    rCtx.textureSlots = &texSlots;
    rCtx.cubemapSlots = &cubemapSlots;
    rCtx.viewPos = cam.position;

#ifdef TRACY_ENABLE
    rCtx.tracyContexts = &tracyContexts;
#endif

    if (enableVR) {
        if (vrApi == VrApi::OpenVR) {
            OpenVRInterface* ovrInterface = static_cast<OpenVRInterface*>(vrInterface);
            rCtx.vrProjMats[0] = ovrInterface->getProjMat(vr::EVREye::Eye_Left, 0.01f, 100.0f);
            rCtx.vrProjMats[1] = ovrInterface->getProjMat(vr::EVREye::Eye_Right, 0.01f, 100.0f);
            rCtx.vrViewMats[0] = ovrInterface->getViewMat(vr::EVREye::Eye_Left);
            rCtx.vrViewMats[1] = ovrInterface->getViewMat(vr::EVREye::Eye_Right);
        }
    }

    vk::CommandBufferBeginInfo cbbi;
    cbbi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    cmdBuf->begin(cbbi);
    cmdBuf->resetQueryPool(*queryPool, 0, 2);
    cmdBuf->writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *queryPool, 0);

    texSlots->setUploadCommandBuffer(*cmdBuf, imageIndex);

    if (clearMaterialIndices) {
        reg.view<WorldObject>().each([](entt::entity, WorldObject& wo) {
            wo.materialIdx = ~0u;
            });
        clearMaterialIndices = false;
    }

    PassSetupCtx psc{ physicalDevice, *device, *pipelineCache, *descriptorPool, *commandPool, *instance, allocator, graphicsQueueFamilyIdx, GraphicsSettings{numMSAASamples, (int32_t)shadowmapRes, enableVR}, &texSlots, &cubemapSlots, &matSlots, rtResources, (int)swapchain->images.size(), enableVR, &brdfLut };

    // Upload any necessary materials + meshes
    reg.view<WorldObject>().each([this, &rCtx](auto ent, WorldObject& wo) {
        if (wo.materialIdx == ~0u) {
            rCtx.reuploadMats = true;
            wo.materialIdx = matSlots->loadOrGet(wo.material);
        }

        if (loadedMeshes.find(wo.mesh) == loadedMeshes.end()) {
            preloadMesh(wo.mesh);
        }
        });

    reg.view<ProceduralObject>().each([this, &rCtx](auto ent, ProceduralObject& po) {
        if (po.materialIdx == ~0u) {
            rCtx.reuploadMats = true;
            po.materialIdx = matSlots->loadOrGet(po.material);
        }
        });


    for (auto& node : solvedNodes) {
        node->prePass(psc, rCtx);
    }

    for (int i = 0; i < solvedNodes.size(); i++) {
        auto& node = solvedNodes[i];
        // Put in barriers for this node
        for (auto& barrier : barriers[i])
            imageBarrier(*cmdBuf, barrier);

        node->execute(rCtx);
    }

    rCtx.width = width;
    rCtx.height = height;

    vku::transitionLayout(*cmdBuf, swapchain->images[imageIndex],
        vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eTransfer,
        vk::AccessFlagBits::eMemoryRead, vk::AccessFlagBits::eTransferWrite);

    vku::transitionLayout(*cmdBuf, rtResources.at(finalPrePresent).image.image(),
        vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
        vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead);

    if (enableVR) {
        vku::transitionLayout(*cmdBuf, rtResources.at(finalPrePresentR).image.image(),
            vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);
    }

    

    if (!enableVR) {
        vk::ImageBlit imageBlit;
        imageBlit.srcOffsets[1] = imageBlit.dstOffsets[1] = { (int)width, (int)height, 1 };
        imageBlit.dstSubresource = imageBlit.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
        cmdBuf->blitImage(
            rtResources.at(finalPrePresent).image.image(), vk::ImageLayout::eTransferSrcOptimal,
            swapchain->images[imageIndex], vk::ImageLayout::eTransferDstOptimal,
            imageBlit, vk::Filter::eNearest);
    } else {
        // Calculate the best crop for the current window size against the VR render target
        float scaleFac = glm::min((float)windowSize.x / renderWidth, (float)windowSize.y / renderHeight);

        vk::ImageBlit imageBlit;
        imageBlit.srcOffsets[1] = vk::Offset3D{ (int32_t)renderWidth, (int32_t)renderHeight, 1};
        imageBlit.dstOffsets[1] = vk::Offset3D{ (int32_t)(renderWidth * scaleFac), (int32_t)(renderHeight * scaleFac), 1 };
        imageBlit.dstSubresource = imageBlit.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };

        cmdBuf->blitImage(
            rtResources.at(finalPrePresentR).image.image(), vk::ImageLayout::eTransferSrcOptimal,
            swapchain->images[imageIndex], vk::ImageLayout::eTransferDstOptimal,
            imageBlit, vk::Filter::eNearest);
    }

    vku::transitionLayout(*cmdBuf, swapchain->images[imageIndex],
        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead);
    irp->execute(rCtx, *framebuffers[imageIndex]);

    ::imageBarrier(*cmdBuf, swapchain->images[imageIndex], vk::ImageLayout::ePresentSrcKHR, 
        vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead, vk::AccessFlagBits::eMemoryRead,
        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe);

    cmdBuf->writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *queryPool, 1);
#ifdef TRACY_ENABLE
    TracyVkCollect(tracyContexts[imageIndex], *cmdBuf);
#endif
    cmdBuf->end();

    if (enableVR && vrApi == VrApi::OpenVR) {
        OpenVRInterface* ovrInterface = static_cast<OpenVRInterface*>(vrInterface);

        vr::TrackedDevicePose_t pose;
        vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::ETrackingUniverseOrigin::TrackingUniverseStanding, vrPredictAmount, &pose, 1);

        glm::mat4 viewMats[2];
        viewMats[0] = ovrInterface->getViewMat(vr::EVREye::Eye_Left);
        viewMats[1] = ovrInterface->getViewMat(vr::EVREye::Eye_Right);

        glm::vec3 viewPos[2];

        for (int i = 0; i < 2; i++) {
            viewMats[i] = glm::inverse(ovrInterface->toMat4(pose.mDeviceToAbsoluteTracking) * viewMats[i]) * cam.getViewMatrix();
            viewPos[i] = glm::inverse(viewMats[i])[3];
        }

        currentPRP->lateUpdateVP(viewMats, viewPos, *device);

        vr::VRCompositor()->SubmitExplicitTimingData();
    }

    vk::SubmitInfo submit;
    submit.waitSemaphoreCount = 1;

    vk::Semaphore cImageAcquire = *imageAcquire;
    submit.pWaitSemaphores = &cImageAcquire;

    vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    submit.pWaitDstStageMask = &waitStages;

    submit.commandBufferCount = 1;
    vk::CommandBuffer cCmdBuf = *cmdBuf;
    submit.pCommandBuffers = &cCmdBuf;

    vk::Semaphore waitSemaphore = *commandComplete;
    submit.signalSemaphoreCount = 2;
    vk::Semaphore signalSemaphores[] = { waitSemaphore, cmdBufferSemaphores[imageIndex] };
    submit.pSignalSemaphores = signalSemaphores;

    vk::TimelineSemaphoreSubmitInfo tssi{};
    uint64_t semaphoreVals[] = { 0, cmdBufSemaphoreVals[imageIndex] + 1 };
    tssi.pSignalSemaphoreValues = semaphoreVals;
    tssi.signalSemaphoreValueCount = 2;

    submit.pNext = &tssi;
    auto queue = device->getQueue(graphicsQueueFamilyIdx, 0);
    queue.submit(1, &submit, nullptr);
    cmdBufSemaphoreVals[imageIndex]++;
    TracyMessageL("Queue submitted");

    if (enableVR) {
        // Submit to SteamVR
        vr::VRTextureBounds_t bounds;
        bounds.uMin = 0.0f;
        bounds.uMax = 1.0f;
        bounds.vMin = 0.0f;
        bounds.vMax = 1.0f;

        vr::VRVulkanTextureData_t vulkanData;
        VkImage vkImg = rtResources.at(finalPrePresent).image.image();
        vulkanData.m_nImage = (uint64_t)vkImg;
        vulkanData.m_pDevice = (VkDevice_T*)*device;
        vulkanData.m_pPhysicalDevice = (VkPhysicalDevice_T*)physicalDevice;
        vulkanData.m_pInstance = (VkInstance_T*)*instance;
        vulkanData.m_pQueue = (VkQueue_T*)device->getQueue(graphicsQueueFamilyIdx, 0);
        vulkanData.m_nQueueFamilyIndex = graphicsQueueFamilyIdx;

        vulkanData.m_nWidth = renderWidth;
        vulkanData.m_nHeight = renderHeight;
        vulkanData.m_nFormat = VK_FORMAT_R8G8B8A8_UNORM;
        vulkanData.m_nSampleCount = 1;

        // Image submission with validation layers turned on causes a crash
        // If we really want the validation layers, don't submit anything
        if (!vrValidationLayers) {
            vr::Texture_t texture = { &vulkanData, vr::TextureType_Vulkan, vr::ColorSpace_Auto };
            vr::VRCompositor()->Submit(vr::Eye_Left, &texture, &bounds);

            vulkanData.m_nImage = (uint64_t)(VkImage)rtResources.at(finalPrePresentR).image.image();
            vr::VRCompositor()->Submit(vr::Eye_Right, &texture, &bounds);
        }
    }

    vk::PresentInfoKHR presentInfo;
    vk::SwapchainKHR cSwapchain = *swapchain->getSwapchain();
    presentInfo.pSwapchains = &cSwapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    vk::Result presentResult = queue.presentKHR(presentInfo);

    if (presentResult != vk::Result::eSuccess)
        __debugbreak();

    TracyMessageL("Presented");

    if (vrApi == VrApi::OpenVR)
        vr::VRCompositor()->WaitGetPoses(nullptr, 0, nullptr, 0);

    std::array<std::uint64_t, 2> timeStamps = { {0} };
    auto queryRes = device->getQueryPoolResults<std::uint64_t>(
        *queryPool, 0, (uint32_t)timeStamps.size(),
        timeStamps, sizeof(std::uint64_t),
        vk::QueryResultFlagBits::e64
        );

    if (queryRes == vk::Result::eSuccess)
        lastRenderTimeTicks = timeStamps[1] - timeStamps[0];

    if (lowLatencyMode.getInt()) {
        vk::SemaphoreWaitInfo swi;
        swi.pSemaphores = &cmdBufferSemaphores[imageIndex];
        swi.semaphoreCount = 1;
        swi.pValues = &cmdBufSemaphoreVals[imageIndex];
        device->waitSemaphores(swi, UINT64_MAX);

        acquireSwapchainImage(&nextImageIdx);
        lowLatencyLast = true;
    }

    frameIdx++;
    FrameMark;
}

void VKRenderer::preloadMesh(AssetID id) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto ext = g_assetDB.getAssetExtension(id);

    if (ext == ".obj") { // obj
        // Use C++ physfs ifstream for tinyobjloader
        PhysFS::ifstream meshFileStream(g_assetDB.openAssetFileRead(id));
        loadObj(vertices, indices, meshFileStream);
    } else if (ext == ".mdl") { // source model
        std::filesystem::path mdlPath = g_assetDB.getAssetPath(id);
        std::string vtxPath = mdlPath.parent_path().string() + "/" + mdlPath.stem().string();
        vtxPath += ".dx90.vtx";
        std::string vvdPath = mdlPath.parent_path().string() + "/" + mdlPath.stem().string();
        vvdPath += ".vvd";

        logMsg("vtxPath: %s, vvdPath: %s", vtxPath.c_str(), vtxPath.c_str());

        AssetID vtxId = g_assetDB.addOrGetExisting(vtxPath);
        AssetID vvdId = g_assetDB.addOrGetExisting(vvdPath);
        loadSourceModel(id, vtxId, vvdId, vertices, indices);
    }

    auto memProps = physicalDevice.getMemoryProperties();
    LoadedMeshData lmd;
    lmd.indexType = vk::IndexType::eUint32;
    lmd.indexCount = (uint32_t)indices.size();
    lmd.ib = vku::IndexBuffer{ *device, allocator, indices.size() * sizeof(uint32_t), "Mesh Index Buffer"};
    lmd.ib.upload(*device, memProps, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), indices);
    lmd.vb = vku::VertexBuffer{ *device, allocator, vertices.size() * sizeof(Vertex), "Mesh Vertex Buffer"};
    lmd.vb.upload(*device, memProps, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), vertices);

    logMsg(WELogCategoryRender, "Loaded mesh %u, %u verts", id, (uint32_t)vertices.size());

    loadedMeshes.insert({ id, std::move(lmd) });

}

void VKRenderer::uploadProcObj(ProceduralObject& procObj) {
    if (procObj.vertices.size() == 0 || procObj.indices.size() == 0) {
        procObj.visible = false;
        return;
    } else {
        procObj.visible = true;
    }

    device->waitIdle();
    auto memProps = physicalDevice.getMemoryProperties();
    procObj.indexType = vk::IndexType::eUint32;
    procObj.indexCount = (uint32_t)procObj.indices.size();
    procObj.ib = vku::IndexBuffer{ *device, allocator, procObj.indices.size() * sizeof(uint32_t), procObj.dbgName.c_str() };
    procObj.ib.upload(*device, memProps, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), procObj.indices);
    procObj.vb = vku::VertexBuffer{ *device, allocator, procObj.vertices.size() * sizeof(Vertex), procObj.dbgName.c_str() };
    procObj.vb.upload(*device, memProps, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), procObj.vertices);
}

bool VKRenderer::getPickedEnt(entt::entity* entOut) {
    return currentPRP->getPickedEnt((uint32_t*)entOut);
}

void VKRenderer::requestEntityPick() {
    return currentPRP->requestEntityPick();
}

void VKRenderer::unloadUnusedMaterials(entt::registry& reg) {
    bool textureReferenced[NUM_TEX_SLOTS];
    bool materialReferenced[NUM_MAT_SLOTS];

    memset(textureReferenced, 0, sizeof(textureReferenced));
    memset(materialReferenced, 0, sizeof(materialReferenced));

    reg.view<WorldObject>().each([&materialReferenced, &textureReferenced, this](entt::entity, WorldObject& wo) {
        materialReferenced[wo.materialIdx] = true;

        uint32_t albedoIdx = (uint32_t)((*matSlots)[wo.materialIdx].pack0.z);
        textureReferenced[albedoIdx] = true;
        });

    for (uint32_t i = 0; i < NUM_MAT_SLOTS; i++) {
        if (!materialReferenced[i] && matSlots->isSlotPresent(i)) matSlots->unload(i);
    }

    for (uint32_t i = 0; i < NUM_TEX_SLOTS; i++) {
        if (!textureReferenced[i] && texSlots->isSlotPresent(i)) texSlots->unload(i);
    }

    std::unordered_set<AssetID> referencedMeshes;

    reg.view<WorldObject>().each([&referencedMeshes](entt::entity, WorldObject& wo) {
        referencedMeshes.insert(wo.mesh);
        });

    std::vector<AssetID> toUnload;

    for (auto& p : loadedMeshes) {
        if (!referencedMeshes.contains(p.first))
            toUnload.push_back(p.first);
    }

    for (auto& id : toUnload) {
        loadedMeshes.erase(id);
    }
}

void VKRenderer::reloadMatsAndTextures() {
    device->waitIdle();
    for (uint32_t i = 0; i < NUM_MAT_SLOTS; i++) {
        if (matSlots->isSlotPresent(i))
            matSlots->unload(i);
    }

    for (uint32_t i = 0; i < NUM_TEX_SLOTS; i++) {
        if (texSlots->isSlotPresent(i))
            texSlots->unload(i);
    }

    clearMaterialIndices = true;

    loadedMeshes.clear();
}

VKRenderer::~VKRenderer() {
    if (this->device) {
        this->device->waitIdle();
        // Some stuff has to be manually destroyed

        for (auto& semaphore : this->cmdBufferSemaphores) {
            this->device->destroySemaphore(semaphore);
        }

        graphSolver.clear();
        delete irp;

        texSlots.reset();
        matSlots.reset();

        rtResources.clear();
        loadedMeshes.clear();

#ifndef NDEBUG
        char* statsString;
        vmaBuildStatsString(allocator, &statsString, true);
        std::cout << statsString << "\n";
        vmaFreeStatsString(allocator, statsString);
#endif
        vmaDestroyAllocator(allocator);
        
        dbgCallback.reset();

        this->swapchain.reset();

        this->instance->destroySurfaceKHR(this->surface);
        logMsg(WELogCategoryRender, "Renderer destroyed.");
    }
}