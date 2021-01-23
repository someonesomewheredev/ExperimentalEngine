#include <vulkan/vulkan.hpp>
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
#include "WMDLLoader.hpp"
#include "RobloxMeshLoader.hpp"

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

RenderTexture* VKRenderer::createRTResource(RTResourceCreateInfo resourceCreateInfo, const char* debugName) {
    return new RenderTexture{ getVKCtx(), resourceCreateInfo, debugName };
}

void VKRenderer::createSwapchain(vk::SwapchainKHR oldSwapchain) {
    vk::PresentModeKHR presentMode = (useVsync && !enableVR) ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate;
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
    for (size_t i = 0; i != swapchain->imageViews.size(); i++) {
        vk::ImageView attachments[1] = { swapchain->imageViews[i] };
        vk::FramebufferCreateInfo fci;
        fci.attachmentCount = 1;
        fci.pAttachments = attachments;
        fci.width = width;
        fci.height = height;
        fci.renderPass = irp->getRenderPass();
        fci.layers = 1;
        framebuffers.push_back(device->createFramebufferUnique(fci));
    }
}

void VKRenderer::createInstance(const RendererInitInfo& initInfo) {
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
        logMsg(WELogCategoryRender, "supported extension: %s", v.extensionName.data());
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

    instanceMaker.applicationName(initInfo.applicationName ? "Worlds Engine" : initInfo.applicationName)
        .engineName("Worlds")
        .applicationVersion(1)
        .engineVersion(1);

    instance = instanceMaker.createUnique();
}

void logPhysDevInfo(const vk::PhysicalDevice& physicalDevice) {
    auto memoryProps = physicalDevice.getMemoryProperties();

    auto physDevProps = physicalDevice.getProperties();
    logMsg(worlds::WELogCategoryRender, "Physical device:\n");
    logMsg(worlds::WELogCategoryRender, "\t-Name: %s", physDevProps.deviceName.data());
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
}

bool checkPhysicalDeviceFeatures(const vk::PhysicalDevice& physDev) {
    vk::PhysicalDeviceFeatures supportedFeatures = physDev.getFeatures();
    if (!supportedFeatures.shaderStorageImageMultisample) {
        logWarn(worlds::WELogCategoryRender, "Missing shaderStorageImageMultisample");
        return false;
    }

    if (!supportedFeatures.fragmentStoresAndAtomics) {
        logWarn(worlds::WELogCategoryRender, "Missing fragmentStoresAndAtomics");
    }

    if (!supportedFeatures.fillModeNonSolid) {
        logWarn(worlds::WELogCategoryRender, "Missing fillModeNonSolid");
        return false;
    }

    if (!supportedFeatures.wideLines) {
        logWarn(worlds::WELogCategoryRender, "Missing wideLines");
        return false;
    }

    return true;
}

bool isDeviceBetter(vk::PhysicalDevice a, vk::PhysicalDevice b) {
    auto aProps = a.getProperties();
    auto bProps = b.getProperties();

    if (bProps.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && aProps.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
        return true;
    }

    return aProps.deviceID < bProps.deviceID;
}

vk::PhysicalDevice pickPhysicalDevice(std::vector<vk::PhysicalDevice>& physicalDevices) {
    std::sort(physicalDevices.begin(), physicalDevices.end(), isDeviceBetter);

    return physicalDevices[0];
}

VKRenderer::VKRenderer(const RendererInitInfo& initInfo, bool* success)
    : finalPrePresent(nullptr)
    , finalPrePresentR(nullptr)
    , shadowmapImage(nullptr)
    , imguiImage(nullptr)
    , window(initInfo.window)
    , frameIdx(0)
    , shadowmapRes(4096)
    , enableVR(initInfo.enableVR)
    , pickingPRP(nullptr)
    , vrPRP(nullptr)
    , irp(nullptr)
    , vrPredictAmount(0.033f)
    , clearMaterialIndices(false)
    , useVsync(true)
    , lowLatencyMode("r_lowLatency", "0", "Waits for GPU completion before starting the next frame. Has a significant impact on latency when VSync is enabled.")
    , enablePicking(initInfo.enablePicking)
    , nextHandle(0u) {
    msaaSamples = vk::SampleCountFlagBits::e2;
    numMSAASamples = 2;

    createInstance(initInfo);

#ifndef NDEBUG
    if (!enableVR || vrValidationLayers)
        dbgCallback = vku::DebugCallback(*instance);
#endif
    auto physDevs = instance->enumeratePhysicalDevices();
    physicalDevice = pickPhysicalDevice(physDevs);

    logPhysDevInfo(physicalDevice);

    auto qprops = physicalDevice.getQueueFamilyProperties();
    const auto badQueue = ~(uint32_t)0;
    graphicsQueueFamilyIdx = badQueue;
    vk::QueueFlags search = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;

    // Look for a queue family with both graphics and
    // compute first.
    for (uint32_t qi = 0; qi != qprops.size(); ++qi) {
        auto& qprop = qprops[qi];
        if ((qprop.queueFlags & search) == search) {
            graphicsQueueFamilyIdx = qi;
            break;
        }
    }

    // Search for async compute queue family
    asyncComputeQueueFamilyIdx = badQueue;
    for (size_t i = 0; i < qprops.size(); i++) {
        auto& qprop = qprops[i];
        if ((qprop.queueFlags & (vk::QueueFlagBits::eCompute)) == vk::QueueFlagBits::eCompute && i != graphicsQueueFamilyIdx) {
            asyncComputeQueueFamilyIdx = i;
            break;
        }
    }

    if (asyncComputeQueueFamilyIdx == badQueue)
        logWarn(worlds::WELogCategoryRender, "Couldn't find async compute queue");

    if (graphicsQueueFamilyIdx == badQueue) {
        *success = false;
        return;
    }

    vku::DeviceMaker dm{};
    dm.defaultLayers();
    dm.queue(graphicsQueueFamilyIdx);

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

    if (!checkPhysicalDeviceFeatures(physicalDevice)) {
        *success = false;
        return;
    }

    vk::PhysicalDeviceFeatures features;
    features.shaderStorageImageMultisample = true;
    features.fragmentStoresAndAtomics = true;
    features.fillModeNonSolid = true;
    features.wideLines = true;
    features.samplerAnisotropy = true;
    features.shaderStorageImageWriteWithoutFormat = true;
    dm.setFeatures(features);

    vk::PhysicalDeviceVulkan12Features vk12Features;
    vk12Features.timelineSemaphore = true;
    vk12Features.descriptorBindingPartiallyBound = true;
    vk12Features.runtimeDescriptorArray = true;
    dm.setPNext(&vk12Features);

    device = dm.createUnique(physicalDevice);

    VmaAllocatorCreateInfo allocatorCreateInfo;
    memset(&allocatorCreateInfo, 0, sizeof(allocatorCreateInfo));
    allocatorCreateInfo.device = *device;
    allocatorCreateInfo.frameInUseCount = 0;
    allocatorCreateInfo.instance = *instance;
    allocatorCreateInfo.physicalDevice = physicalDevice;
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorCreateInfo.flags = 0;
    vmaCreateAllocator(&allocatorCreateInfo, &allocator);

    vk::PipelineCacheCreateInfo pipelineCacheInfo{};
    PipelineCacheSerializer::loadPipelineCache(physicalDevice.getProperties(), pipelineCacheInfo);
    pipelineCache = device->createPipelineCacheUnique(pipelineCacheInfo);
    std::free((void*)pipelineCacheInfo.pInitialData);

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
    descriptorPool = device->createDescriptorPoolUnique(descriptorPoolInfo);

    // Create surface and find presentation queue
    VkSurfaceKHR surface;
    SDL_Vulkan_CreateSurface(window, *instance, &surface);

    this->surface = surface;
    presentQueueFamilyIdx = findPresentQueue(physicalDevice, surface);

    int qfi = 0;
    for (auto& qprop : qprops) {
        logMsg(worlds::WELogCategoryRender, "Queue family with properties %s (supports present: %i)", 
            vk::to_string(qprop.queueFlags).c_str(), physicalDevice.getSurfaceSupportKHR(qfi, surface));
        qfi++;
    }

    // Command pool
    vk::CommandPoolCreateInfo cpci;
    cpci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    cpci.queueFamilyIndex = graphicsQueueFamilyIdx;
    commandPool = device->createCommandPoolUnique(cpci);

    createSwapchain(vk::SwapchainKHR{});

    if (initInfo.activeVrApi == VrApi::OpenVR) {
        OpenVRInterface* vrInterface = static_cast<OpenVRInterface*>(initInfo.vrInterface);
        vrInterface->getRenderResolution(&renderWidth, &renderHeight);
    }

    auto vkCtx = std::make_shared<VulkanHandles>(VulkanHandles{
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

    vk::ImageCreateInfo brdfLutIci{
        vk::ImageCreateFlags{},
        vk::ImageType::e2D,
        vk::Format::eR16G16Sfloat,
        vk::Extent3D{256,256,1}, 1, 1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::SharingMode::eExclusive, graphicsQueueFamilyIdx
    };

    brdfLut = vku::GenericImage{ *device, allocator, brdfLutIci, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor, false, "BRDF LUT" };

    cubemapConvoluter = std::make_unique<CubemapConvoluter>(vkCtx);

    vku::executeImmediately(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), [&](auto cb) {
        brdfLut.setLayout(cb, vk::ImageLayout::eColorAttachmentOptimal);
        });

    BRDFLUTRenderer brdfLutRenderer{ *vkCtx };
    brdfLutRenderer.render(*vkCtx, brdfLut);

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

    vku::executeImmediately(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), [&](auto cb) {
        shadowmapImage->image.setLayout(cb, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eDepth);
        });

    createSCDependents();

    vk::CommandBufferAllocateInfo cbai;
    cbai.commandPool = *commandPool;
    cbai.commandBufferCount = maxFramesInFlight;
    cbai.level = vk::CommandBufferLevel::ePrimary;
    cmdBufs = device->allocateCommandBuffersUnique(cbai);

    for (size_t i = 0; i < cmdBufs.size(); i++) {
        vk::FenceCreateInfo fci;
        fci.flags = vk::FenceCreateFlagBits::eSignaled;
        cmdBufFences.push_back(device->createFence(fci));

        vk::SemaphoreCreateInfo sci;
        cmdBufferSemaphores.push_back(device->createSemaphore(sci));
        imgAvailable.push_back(device->createSemaphore(sci));

        vk::CommandBuffer cb = *cmdBufs[i];
        vk::CommandBufferBeginInfo cbbi;
        cb.begin(cbbi);
        cb.end();
    }
    imgFences.resize(cmdBufs.size());

    timestampPeriod = physicalDevice.getProperties().limits.timestampPeriod;

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
        if (initInfo.activeVrApi == VrApi::OpenVR) {
            vr::VRCompositor()->SetExplicitTimingMode(vr::EVRCompositorTimingMode::VRCompositorTimingMode_Explicit_RuntimePerformsPostPresentHandoff);
        }

        vrInterface = initInfo.vrInterface;
        vrApi = initInfo.activeVrApi;
    }

    uint32_t s = cubemapSlots->loadOrGet(g_assetDB.addOrGetExisting("Cubemap2.json"));
    cubemapConvoluter->convolute((*cubemapSlots)[s]);

    g_console->registerCommand([&](void*, const char* arg) {
        numMSAASamples = std::atoi(arg);
        // The sample count flags are actually identical to the number of samples
        msaaSamples = (vk::SampleCountFlagBits)numMSAASamples;
        recreateSwapchain();
        }, "r_setMSAASamples", "Sets the number of MSAA samples.", nullptr);

    g_console->registerCommand([&](void*, const char*) {
        recreateSwapchain();
        }, "r_recreateSwapchain", "", nullptr);

    g_console->registerCommand([&](void*, const char*) {
        char* statsString;
        vmaBuildStatsString(allocator, &statsString, true);
        logMsg("%s", statsString);
        auto file = PHYSFS_openWrite("memory.json");
        PHYSFS_writeBytes(file, statsString, strlen(statsString));
        PHYSFS_close(file);
        vmaFreeStatsString(allocator, statsString);
        }, "r_printAllocInfo", "", nullptr);

    PassSetupCtx psc{
        &materialUB,
        getVKCtx(),
        &texSlots,
        &cubemapSlots,
        &matSlots,
        (int)swapchain->images.size(),
        enableVR,
        &brdfLut
    };

    shadowmapPass = new ShadowmapRenderPass(shadowmapImage);
    shadowmapPass->setup(psc);

    materialUB = vku::UniformBuffer(*device, allocator, sizeof(MaterialsUB), VMA_MEMORY_USAGE_GPU_ONLY, "Materials");

    MaterialsUB materials;
    materialUB.upload(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), &materials, sizeof(materials));
}

// Quite a lot of resources are dependent on either the number of images
// there are in the swap chain or the swapchain itself, so they need to be
// recreated whenever the swap chain changes.
void VKRenderer::createSCDependents() {
    delete imguiImage;
    delete finalPrePresent;
    delete finalPrePresentR;

    PassSetupCtx psc{
        &materialUB,
        getVKCtx(),
        &texSlots,
        &cubemapSlots,
        &matSlots,
        (int)swapchain->images.size(),
        enableVR,
        &brdfLut
    };

    if (irp == nullptr) {
        irp = new ImGuiRenderPass(*swapchain);
        irp->setup(psc);
    }

    createFramebuffers();

    vk::ImageCreateInfo ici;
    ici.imageType = vk::ImageType::e2D;
    ici.extent = vk::Extent3D{ renderWidth, renderHeight, 1 };
    ici.arrayLayers = 1;
    ici.mipLevels = 1;
    ici.initialLayout = vk::ImageLayout::eUndefined;
    ici.format = vk::Format::eR8G8B8A8Unorm;
    ici.samples = vk::SampleCountFlagBits::e1;
    ici.usage = vk::ImageUsageFlagBits::eColorAttachment |
        vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage;

    RTResourceCreateInfo imguiImageCreateInfo{ ici, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor };
    imguiImage = createRTResource(imguiImageCreateInfo, "ImGui Image");

    ici.usage = vk::ImageUsageFlagBits::eColorAttachment |
        vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eTransferSrc;

    RTResourceCreateInfo finalPrePresentCI{ ici, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor };

    finalPrePresent = createRTResource(finalPrePresentCI, "Final Pre-Present");

    if (enableVR)
        finalPrePresentR = createRTResource(finalPrePresentCI, "Final Pre-Present R");

    vku::executeImmediately(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), [&](vk::CommandBuffer cmdBuf) {
        finalPrePresent->image.setLayout(cmdBuf, vk::ImageLayout::eTransferSrcOptimal);
        });

    RTTPassHandle screenPass = ~0u;
    for (auto& p : rttPasses) {
        if (p.second.outputToScreen) {
            screenPass = p.first;
        }
    }

    if (screenPass != ~0u) {
        if (rttPasses.at(screenPass).isVr) {
            vrPRP = nullptr;
        }
        destroyRTTPass(screenPass);
    }

    imgFences.clear();
    imgFences.resize(swapchain->images.size());
}

void VKRenderer::recreateSwapchain() {
    // Wait for current frame to finish
    device->waitIdle();

    // Check width/height - if it's 0, just ignore it
    auto surfaceCaps = physicalDevice.getSurfaceCapabilitiesKHR(surface);

    if (surfaceCaps.currentExtent.width == 0 || surfaceCaps.currentExtent.height == 0) {
        logMsg(WELogCategoryRender, "Ignoring resize with 0 width or height");
        isMinimised = true;

        while (isMinimised) {
            auto surfaceCaps = physicalDevice.getSurfaceCapabilitiesKHR(surface);
            isMinimised = surfaceCaps.currentExtent.width == 0 || surfaceCaps.currentExtent.height == 0;
            SDL_PumpEvents();
            SDL_Delay(50);
        }

        recreateSwapchain();
        return;
    }

    isMinimised = false;

    logMsg(WELogCategoryRender, "Recreating swapchain: New surface size is %ix%i",
        surfaceCaps.currentExtent.width, surfaceCaps.currentExtent.height);

    if (surfaceCaps.currentExtent.width > 0 && surfaceCaps.currentExtent.height > 0) {
        width = surfaceCaps.currentExtent.width;
        height = surfaceCaps.currentExtent.height;
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

    createSCDependents();

    swapchainRecreated = true;
}

void VKRenderer::presentNothing(uint32_t imageIndex) {
    vk::Semaphore imgSemaphore = imgAvailable[frameIdx];
    vk::Semaphore cmdBufSemaphore = cmdBufferSemaphores[frameIdx];

    vk::PresentInfoKHR presentInfo;
    vk::SwapchainKHR cSwapchain = *swapchain->getSwapchain();
    presentInfo.pSwapchains = &cSwapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pWaitSemaphores = &cmdBufSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    vk::CommandBufferBeginInfo cbbi;
    auto& cmdBuf = cmdBufs[frameIdx];
    cmdBuf->begin(cbbi);
    cmdBuf->end();

    vk::SubmitInfo submitInfo;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imgSemaphore;

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &cmdBufferSemaphores[frameIdx];
    vk::CommandBuffer cCmdBuf = *cmdBuf;
    submitInfo.pCommandBuffers = &cCmdBuf;
    submitInfo.commandBufferCount = 1;
    device->getQueue(presentQueueFamilyIdx, 0).submit(submitInfo, nullptr);

    auto presentResult = device->getQueue(presentQueueFamilyIdx, 0).presentKHR(presentInfo);
    if (presentResult != vk::Result::eSuccess && presentResult != vk::Result::eSuboptimalKHR)
        fatalErr("Present failed!");
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

vku::ShaderModule VKRenderer::loadShaderAsset(AssetID id) {
    ZoneScoped;
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

void VKRenderer::acquireSwapchainImage(uint32_t* imageIdx) {
    ZoneScoped;
    vk::Result nextImageRes = swapchain->acquireImage(*device, imgAvailable[frameIdx], imageIdx);

    if ((nextImageRes == vk::Result::eSuboptimalKHR || nextImageRes == vk::Result::eErrorOutOfDateKHR) && width != 0 && height != 0) {
        recreateSwapchain();

        // acquire image from new swapchain
        swapchain->acquireImage(*device, imgAvailable[frameIdx], imageIdx);
    }
}

void VKRenderer::submitToOpenVR() {
    // Submit to SteamVR
    vr::VRTextureBounds_t bounds;
    bounds.uMin = 0.0f;
    bounds.uMax = 1.0f;
    bounds.vMin = 0.0f;
    bounds.vMax = 1.0f;

    vr::VRVulkanTextureData_t vulkanData;
    VkImage vkImg = finalPrePresent->image.image();
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

        vulkanData.m_nImage = (uint64_t)(VkImage)finalPrePresentR->image.image();
        vr::VRCompositor()->Submit(vr::Eye_Right, &texture, &bounds);
    }
}

void VKRenderer::uploadSceneAssets(entt::registry& reg) {
    ZoneScoped;
    bool reuploadMats = false;

    // Upload any necessary materials + meshes
    reg.view<WorldObject>().each([&](auto, WorldObject& wo) {
        for (int i = 0; i < NUM_SUBMESH_MATS; i++) {
            if (!wo.presentMaterials[i]) continue;

            if (wo.materialIdx[i] == ~0u) {
                reuploadMats = true;
                wo.materialIdx[i] = matSlots->loadOrGet(wo.materials[i]);
            }
        }

        if (loadedMeshes.find(wo.mesh) == loadedMeshes.end()) {
            preloadMesh(wo.mesh);
        }
        });

    reg.view<ProceduralObject>().each([&](auto, ProceduralObject& po) {
        if (po.materialIdx == ~0u) {
            reuploadMats = true;
            po.materialIdx = matSlots->loadOrGet(po.material);
        }
        });

    reg.view<WorldCubemap>().each([&](auto, WorldCubemap& wc) {
        if (wc.loadIdx == ~0u) {
            wc.loadIdx = cubemapSlots->loadOrGet(wc.cubemapId);
            cubemapConvoluter->convolute(cubemapSlots->getSlots()[wc.loadIdx]);
            reuploadMats = true;
        }
        });

    if (reuploadMats)
        reuploadMaterials();
}

worlds::ConVar doGTAO{ "r_doGTAO", "1" };

void VKRenderer::writeCmdBuf(vk::UniqueCommandBuffer& cmdBuf, uint32_t imageIndex, Camera& cam, entt::registry& reg) {
    ZoneScoped;

    RenderCtx rCtx{ cmdBuf, reg, imageIndex, &cam, renderWidth, renderHeight, loadedMeshes };
    rCtx.enableVR = enableVR;
    rCtx.materialSlots = &matSlots;
    rCtx.textureSlots = &texSlots;
    rCtx.cubemapSlots = &cubemapSlots;
    rCtx.viewPos = cam.position;
    rCtx.dbgStats = &dbgStats;

#ifdef TRACY_ENABLE
    rCtx.tracyContexts = &tracyContexts;
#endif

    if (enableVR) {
        OpenVRInterface* ovrInterface = static_cast<OpenVRInterface*>(vrInterface);
        rCtx.vrProjMats[0] = ovrInterface->getProjMat(vr::EVREye::Eye_Left, 0.01f, 100.0f);
        rCtx.vrProjMats[1] = ovrInterface->getProjMat(vr::EVREye::Eye_Right, 0.01f, 100.0f);

        vr::TrackedDevicePose_t pose;
        vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::ETrackingUniverseOrigin::TrackingUniverseStanding, vrPredictAmount, &pose, 1);

        glm::mat4 viewMats[2];
        rCtx.vrViewMats[0] = ovrInterface->getViewMat(vr::EVREye::Eye_Left);
        rCtx.vrViewMats[1] = ovrInterface->getViewMat(vr::EVREye::Eye_Right);

        for (int i = 0; i < 2; i++) {
            rCtx.vrViewMats[i] = glm::inverse(ovrInterface->toMat4(pose.mDeviceToAbsoluteTracking) * viewMats[i]) * cam.getViewMatrix();
        }
    }

    vk::CommandBufferBeginInfo cbbi;
    cbbi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    cmdBuf->begin(cbbi);
    texSlots->frameStarted = true;
    cmdBuf->resetQueryPool(*queryPool, 0, 2);
    cmdBuf->writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *queryPool, 0);

    texSlots->setUploadCommandBuffer(*cmdBuf, frameIdx);

    if (clearMaterialIndices) {
        reg.view<WorldObject>().each([](entt::entity, WorldObject& wo) {
            memset(wo.materialIdx, ~0u, sizeof(wo.materialIdx));
            });
        clearMaterialIndices = false;
    }

    PassSetupCtx psc{ &materialUB, getVKCtx(), &texSlots, &cubemapSlots, &matSlots, (int)swapchain->images.size(), enableVR, &brdfLut };

    uploadSceneAssets(reg);

    finalPrePresent->image.setLayout(*cmdBuf,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eColorAttachmentWrite);

    shadowmapPass->execute(rCtx);

    int numActivePasses = 0;
    for (auto& p : rttPasses) {
        if (!p.second.active) continue;
        numActivePasses++;

        if (!p.second.outputToScreen) {
            p.second.sdrFinalTarget->image.setLayout(*cmdBuf,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eColorAttachmentWrite);
        }

        auto& rpi = p.second;
        rCtx.width = rpi.width;
        rCtx.height = rpi.height;
        if (rpi.cam)
            rCtx.cam = rpi.cam;
        else
            rCtx.cam = &cam;
        rCtx.viewPos = rCtx.cam->position;
        rCtx.enableVR = p.second.isVr;

        rpi.prp->prePass(psc, rCtx);
        rpi.prp->execute(rCtx);

        if (doGTAO.getInt())
            rpi.gtrp->execute(rCtx);

        rpi.hdrTarget->image.barrier(*cmdBuf,
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eComputeShader,
            vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead);

        rpi.trp->execute(rCtx);

        if (!rpi.outputToScreen) {
            rpi.sdrFinalTarget->image.setLayout(*cmdBuf,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead);
        }
    }
    dbgStats.numRTTPasses = numActivePasses;
    rCtx.width = width;
    rCtx.height = height;

    vku::transitionLayout(*cmdBuf, swapchain->images[imageIndex],
        vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eTransfer,
        vk::AccessFlagBits::eMemoryRead, vk::AccessFlagBits::eTransferWrite);

    finalPrePresent->image.setLayout(*cmdBuf,
        vk::ImageLayout::eTransferSrcOptimal,
        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
        vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead);

    if (enableVR) {
        finalPrePresentR->image.setLayout(*cmdBuf,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);
    }

    cmdBuf->clearColorImage(swapchain->images[imageIndex], vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue{ std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f } }, vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

    if (!enableVR) {
        vk::ImageBlit imageBlit;
        imageBlit.srcOffsets[1] = imageBlit.dstOffsets[1] = vk::Offset3D{ (int)width, (int)height, 1 };
        imageBlit.dstSubresource = imageBlit.srcSubresource = vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
        cmdBuf->blitImage(
            finalPrePresent->image.image(), vk::ImageLayout::eTransferSrcOptimal,
            swapchain->images[imageIndex], vk::ImageLayout::eTransferDstOptimal,
            imageBlit, vk::Filter::eNearest);
    } else {
        // Calculate the best crop for the current window size against the VR render target
        float scaleFac = glm::min((float)windowSize.x / renderWidth, (float)windowSize.y / renderHeight);

        vk::ImageBlit imageBlit;
        imageBlit.srcOffsets[1] = vk::Offset3D{ (int32_t)renderWidth, (int32_t)renderHeight, 1 };
        imageBlit.dstOffsets[1] = vk::Offset3D{ (int32_t)(renderWidth * scaleFac), (int32_t)(renderHeight * scaleFac), 1 };
        imageBlit.dstSubresource = imageBlit.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };

        cmdBuf->blitImage(
            finalPrePresent->image.image(), vk::ImageLayout::eTransferSrcOptimal,
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
    texSlots->frameStarted = false;
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

        if (vrPRP)
            vrPRP->lateUpdateVP(viewMats, viewPos, *device);

        vr::VRCompositor()->SubmitExplicitTimingData();
    }
}

void VKRenderer::reuploadMaterials() {
    materialUB.upload(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), matSlots->getSlots(), sizeof(PackedMaterial) * 256);

    for (auto& pair : this->rttPasses) {
        pair.second.prp->reuploadDescriptors();
    }
}

void VKRenderer::frame(Camera& cam, entt::registry& reg) {
    ZoneScoped;
    device->waitForFences(1, &cmdBufFences[frameIdx], VK_TRUE, UINT64_MAX);
    device->resetFences(1, &cmdBufFences[frameIdx]);

    dbgStats.numCulledObjs = 0;
    dbgStats.numDrawCalls = 0;
    dbgStats.numPipelineSwitches = 0;
    destroyTempTexBuffers(frameIdx);

    uint32_t imageIndex;
    acquireSwapchainImage(&imageIndex);

    if (imgFences[imageIndex]) {
        device->waitForFences(imgFences[imageIndex], true, UINT64_MAX);
    }

    imgFences[imageIndex] = cmdBufFences[frameIdx];

    auto& cmdBuf = cmdBufs[frameIdx];
    writeCmdBuf(cmdBuf, imageIndex, cam, reg);

    vk::SubmitInfo submit;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imgAvailable[frameIdx];

    vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    submit.pWaitDstStageMask = &waitStages;

    submit.commandBufferCount = 1;
    vk::CommandBuffer cCmdBuf = *cmdBuf;
    submit.pCommandBuffers = &cCmdBuf;
    submit.pSignalSemaphores = &cmdBufferSemaphores[frameIdx];
    submit.signalSemaphoreCount = 1;

    auto queue = device->getQueue(graphicsQueueFamilyIdx, 0);
    auto submitResult = queue.submit(1, &submit, cmdBufFences[frameIdx]);

    if (submitResult != vk::Result::eSuccess) {
        std::string errStr = vk::to_string(submitResult);
        fatalErr(("Failed to submit queue (error: " + errStr + ")").c_str());
    }

    TracyMessageL("Queue submitted");

    if (enableVR) {
        submitToOpenVR();
    }

    vk::PresentInfoKHR presentInfo;
    vk::SwapchainKHR cSwapchain = *swapchain->getSwapchain();
    presentInfo.pSwapchains = &cSwapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pWaitSemaphores = &cmdBufferSemaphores[frameIdx];
    presentInfo.waitSemaphoreCount = 1;

    try {
        vk::Result presentResult = queue.presentKHR(presentInfo);

        if (presentResult == vk::Result::eSuboptimalKHR) {
            recreateSwapchain();
        } else if (presentResult != vk::Result::eSuccess) {
            fatalErr("Failed to present");
        }
    } catch (vk::OutOfDateKHRError) {
        recreateSwapchain();
    }

    TracyMessageL("Presented");

    if (vrApi == VrApi::OpenVR)
        vr::VRCompositor()->WaitGetPoses(nullptr, 0, nullptr, 0);

    std::array<std::uint64_t, 2> timeStamps = { {0} };

    auto queryRes = device->getQueryPoolResults(
        *queryPool, 0, (uint32_t)timeStamps.size(),
        timeStamps.size() * sizeof(uint64_t), timeStamps.data(),
        sizeof(uint64_t), vk::QueryResultFlagBits::e64
    );

    if (queryRes == vk::Result::eSuccess)
        lastRenderTimeTicks = timeStamps[1] - timeStamps[0];

    frameIdx++;
    frameIdx %= maxFramesInFlight;
    FrameMark;
}

void VKRenderer::preloadMesh(AssetID id) {
    ZoneScoped;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto ext = g_assetDB.getAssetExtension(id);

    LoadedMeshData lmd;

    if (ext == ".obj") { // obj
        // Use C++ physfs ifstream for tinyobjloader
        PhysFS::ifstream meshFileStream(g_assetDB.openAssetFileRead(id));
        loadObj(vertices, indices, meshFileStream, lmd);
        lmd.numSubmeshes = 1;
        lmd.submeshes[0].indexCount = indices.size();
        lmd.submeshes[0].indexOffset = 0;
    } else if (ext == ".mdl") { // source model
        std::filesystem::path mdlPath = g_assetDB.getAssetPath(id);
        std::string vtxPath = mdlPath.parent_path().string() + "/" + mdlPath.stem().string();
        vtxPath += ".dx90.vtx";
        std::string vvdPath = mdlPath.parent_path().string() + "/" + mdlPath.stem().string();
        vvdPath += ".vvd";

        AssetID vtxId = g_assetDB.addOrGetExisting(vtxPath);
        AssetID vvdId = g_assetDB.addOrGetExisting(vvdPath);
        loadSourceModel(id, vtxId, vvdId, vertices, indices, lmd);
    } else if (ext == ".wmdl") {
        loadWorldsModel(id, vertices, indices, lmd);
    } else if (ext == ".rblx") {
        loadRobloxMesh(id, vertices, indices, lmd);
    }

    auto memProps = physicalDevice.getMemoryProperties();
    lmd.indexType = vk::IndexType::eUint32;
    lmd.indexCount = (uint32_t)indices.size();
    lmd.ib = vku::IndexBuffer{ *device, allocator, indices.size() * sizeof(uint32_t), "Mesh Index Buffer" };
    lmd.ib.upload(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), indices);
    lmd.vb = vku::VertexBuffer{ *device, allocator, vertices.size() * sizeof(Vertex), "Mesh Vertex Buffer" };
    lmd.vb.upload(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), vertices);

    lmd.aabbMax = glm::vec3(0.0f);
    lmd.aabbMin = glm::vec3(std::numeric_limits<float>::max());
    lmd.sphereRadius = 0.0f;
    for (auto& vtx : vertices) {
        lmd.sphereRadius = std::max(glm::length(vtx.position), lmd.sphereRadius);
        lmd.aabbMax = glm::max(lmd.aabbMax, vtx.position);
        lmd.aabbMin = glm::min(lmd.aabbMin, vtx.position);
    }

    logMsg(WELogCategoryRender, "Loaded mesh %u, %u verts. Sphere radius %f", id, (uint32_t)vertices.size(), lmd.sphereRadius);

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
    procObj.ib.upload(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), procObj.indices);
    procObj.vb = vku::VertexBuffer{ *device, allocator, procObj.vertices.size() * sizeof(Vertex), procObj.dbgName.c_str() };
    procObj.vb.upload(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), procObj.vertices);
}

bool VKRenderer::getPickedEnt(entt::entity* entOut) {
    if (pickingPRP)
        return pickingPRP->getPickedEnt((uint32_t*)entOut);
    else
        return false;
}

void VKRenderer::requestEntityPick(int x, int y) {
    if (pickingPRP) {
        pickingPRP->setPickCoords(x, y);
        pickingPRP->requestEntityPick();
    }
}

void VKRenderer::unloadUnusedMaterials(entt::registry& reg) {
    bool textureReferenced[NUM_TEX_SLOTS];
    bool materialReferenced[NUM_MAT_SLOTS];

    memset(textureReferenced, 0, sizeof(textureReferenced));
    memset(materialReferenced, 0, sizeof(materialReferenced));

    reg.view<WorldObject>().each([&materialReferenced, &textureReferenced, this](entt::entity, WorldObject& wo) {
        for (int i = 0; i < NUM_SUBMESH_MATS; i++) {
            if (!wo.presentMaterials[i]) continue;
            materialReferenced[wo.materialIdx[i]] = true;

            uint32_t albedoIdx = (uint32_t)((*matSlots)[wo.materialIdx[i]].albedoTexIdx);
            textureReferenced[albedoIdx] = true;

            int normalTex = (*matSlots)[wo.materialIdx[i]].normalTexIdx;

            if (normalTex > -1) {
                textureReferenced[normalTex] = true;
            }

            int heightmapTex = (*matSlots)[wo.materialIdx[i]].heightmapTexIdx;

            if (heightmapTex > -1) {
                textureReferenced[heightmapTex] = true;
            }

            int metalMapTex = (*matSlots)[wo.materialIdx[i]].metalTexIdx;

            if (metalMapTex > -1) {
                textureReferenced[metalMapTex] = true;
            }

            int roughTexIdx = (*matSlots)[wo.materialIdx[i]].roughTexIdx;

            if (roughTexIdx > -1) {
                textureReferenced[roughTexIdx] = true;
            }

            int aoTexIdx = (*matSlots)[wo.materialIdx[i]].aoTexIdx;

            if (aoTexIdx > -1) {
                textureReferenced[aoTexIdx] = true;
            }
        }
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

VulkanHandles VKRenderer::getVKCtx() {
    return VulkanHandles{
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
    };
}

RTTPassHandle VKRenderer::createRTTPass(RTTPassCreateInfo& ci) {
    RTTPassInternal rpi;
    rpi.cam = ci.cam;

    vk::ImageCreateInfo ici;
    ici.imageType = vk::ImageType::e2D;
    ici.extent = vk::Extent3D{ ci.width, ci.height, 1 };
    ici.arrayLayers = ci.isVr ? 2 : 1;
    ici.mipLevels = 1;
    ici.format = vk::Format::eB10G11R11UfloatPack32;
    ici.initialLayout = vk::ImageLayout::eUndefined;
    ici.samples = msaaSamples;
    ici.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

    RTResourceCreateInfo polyCreateInfo{ ici, vk::ImageViewType::e2DArray, vk::ImageAspectFlagBits::eColor };
    rpi.hdrTarget = createRTResource(polyCreateInfo, "HDR Target");

    ici.format = vk::Format::eD32Sfloat;
    ici.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
    RTResourceCreateInfo depthCreateInfo{ ici, vk::ImageViewType::e2DArray, vk::ImageAspectFlagBits::eDepth };
    rpi.depthTarget = createRTResource(depthCreateInfo, "Depth Stencil Image");

    {
        auto prp = new PolyRenderPass(rpi.depthTarget, rpi.hdrTarget, shadowmapImage, enablePicking);
        if (ci.useForPicking)
            pickingPRP = prp;
        if (ci.isVr)
            vrPRP = prp;
        rpi.prp = prp;
    }

    ici.samples = vk::SampleCountFlagBits::e1;
    ici.format = vk::Format::eR8Unorm;
    ici.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
    RTResourceCreateInfo gtaoTarget{ ici, vk::ImageViewType::e2DArray, vk::ImageAspectFlagBits::eColor };
    rpi.gtaoOut = createRTResource(gtaoTarget, "GTAO Target");

    ici.arrayLayers = 1;
    ici.format = vk::Format::eR8G8B8A8Unorm;
    ici.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;

    if (!ci.outputToScreen) {
        RTResourceCreateInfo sdrTarget{ ici, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor };
        rpi.sdrFinalTarget = createRTResource(sdrTarget, "SDR Target");
    }


    PassSetupCtx psc{ &materialUB, getVKCtx(), &texSlots, &cubemapSlots, &matSlots, (int)swapchain->images.size(), ci.isVr, &brdfLut,
    ci.width, ci.height };
    auto tonemapRP = new TonemapRenderPass(rpi.hdrTarget, ci.outputToScreen ? finalPrePresent : rpi.sdrFinalTarget, rpi.gtaoOut);
    rpi.trp = tonemapRP;

    vku::executeImmediately(*device, *commandPool, device->getQueue(graphicsQueueFamilyIdx, 0), [&](vk::CommandBuffer cmdBuf) {
        rpi.hdrTarget->image.setLayout(cmdBuf, vk::ImageLayout::eGeneral);
        if (!ci.outputToScreen)
            rpi.sdrFinalTarget->image.setLayout(cmdBuf, vk::ImageLayout::eShaderReadOnlyOptimal);
        if (ci.isVr) {
            finalPrePresentR->image.setLayout(cmdBuf, vk::ImageLayout::eTransferSrcOptimal);
        }
        });

    rpi.gtrp = new GTAORenderPass{ this, rpi.depthTarget, rpi.gtaoOut };
    rpi.trp->setup(psc);
    rpi.prp->setup(psc);
    rpi.gtrp->setup(psc);

    if (ci.isVr) {
        tonemapRP->setRightFinalImage(psc, finalPrePresentR);
    }

    rpi.isVr = ci.isVr;
    rpi.enableShadows = ci.enableShadows;
    rpi.outputToScreen = ci.outputToScreen;
    rpi.width = ci.width;
    rpi.height = ci.height;
    rpi.active = true;

    RTTPassHandle handle = nextHandle;
    nextHandle++;
    rttPasses.insert({ handle, rpi });
    return handle;
}

void VKRenderer::destroyRTTPass(RTTPassHandle handle) {
    device->waitIdle();
    auto& rpi = rttPasses.at(handle);

    delete rpi.prp;
    delete rpi.trp;
    delete rpi.gtrp;

    delete rpi.hdrTarget;
    delete rpi.depthTarget;
    delete rpi.gtaoOut;

    if (!rpi.outputToScreen)
        delete rpi.sdrFinalTarget;

    rttPasses.erase(handle);
}

VKRenderer::~VKRenderer() {
    if (device) {
        device->waitIdle();
        auto physDevProps = physicalDevice.getProperties();
        PipelineCacheSerializer::savePipelineCache(physDevProps, *pipelineCache, *device);
#ifndef NDEBUG
        char* statsString;
#endif

        for (auto& semaphore : cmdBufferSemaphores) {
            device->destroySemaphore(semaphore);
        }

        std::vector<RTTPassHandle> toDelete;
        for (auto& p : rttPasses) {
            toDelete.push_back(p.first);
        }

        for (auto& h : toDelete) {
            destroyRTTPass(h);
        }

        rttPasses.clear();
        delete irp;

        texSlots.reset();
        matSlots.reset();
        cubemapSlots.reset();

        brdfLut.destroy();
        loadedMeshes.clear();

        delete imguiImage;
        delete shadowmapImage;
        delete finalPrePresent;

        if (enableVR)
            delete finalPrePresentR;

        materialUB.destroy();

#ifndef NDEBUG
        vmaBuildStatsString(allocator, &statsString, true);
        logMsg("%s", statsString);

        auto file = PHYSFS_openWrite("memory_shutdown.json");
        PHYSFS_writeBytes(file, statsString, strlen(statsString));
        PHYSFS_close(file);

        vmaFreeStatsString(allocator, statsString);
#endif
        vmaDestroyAllocator(allocator);

        dbgCallback.reset();

        swapchain.reset();

        instance->destroySurfaceKHR(surface);
        logMsg(WELogCategoryRender, "Renderer destroyed.");
    }
}
