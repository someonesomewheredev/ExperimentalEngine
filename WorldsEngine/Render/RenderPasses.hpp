#pragma once
#include "RenderGraph.hpp"
#include "vku/vku.hpp"
#include "RenderInternal.hpp"
#include <glm/glm.hpp>
#include <slib/StaticAllocList.hpp>
#include "robin_hood.h"

namespace worlds {
    struct MultiVP;
    struct LightUB;
    struct ModelMatrices;
    struct RenderContext;
    class RenderTexture;
    class Swapchain;
    class VKRenderer;
    class VRCullMeshRenderer {
    private:
        vku::Pipeline pipeline;
        vku::PipelineLayout pipelineLayout;
        vku::DescriptorSetLayout dsl;
        VkDescriptorSet ds;
        vku::GenericBuffer vertexBuf;
        uint32_t totalVertCount;
        uint32_t leftVertCount;
        VulkanHandles* handles;
    public:
        VRCullMeshRenderer(VulkanHandles* handles);
        void setup(RenderContext& ctx, VkRenderPass rp, VkDescriptorPool descriptorPool);
        void draw(VkCommandBuffer& cmdBuf);
        ~VRCullMeshRenderer();
    };

    struct SubmeshDrawInfo {
        uint32_t materialIdx;
        uint32_t matrixIdx;
        VkBuffer vb;
        VkBuffer ib;
        uint32_t indexCount;
        uint32_t indexOffset;
        uint32_t cubemapIdx;
        glm::vec3 cubemapExt;
        glm::vec3 cubemapPos;
        glm::vec4 texScaleOffset;
        entt::entity ent;
        VkPipeline pipeline;
        uint32_t drawMiscFlags;
        bool opaque;
        bool dontPrepass;

        bool skinned;
        uint32_t boneMatrixOffset;
        VkBuffer boneVB;
    };

    class DebugLinesPass {
    private:
        vku::Pipeline linePipeline;
        vku::PipelineLayout linePipelineLayout;
        vku::DescriptorSetLayout lineDsl;
        VkDescriptorSet lineDs;
        vku::GenericBuffer lineVB;
        uint32_t currentLineVBSize;
        int numLineVerts;
        VulkanHandles* handles;
    public:
        DebugLinesPass(VulkanHandles* handles);
        void setup(RenderContext& ctx, VkRenderPass renderPass, VkDescriptorPool descriptorPool);
        void prePass(RenderContext&);
        void execute(RenderContext&);
        ~DebugLinesPass();
    };

    class SkyboxPass {
    private:
        vku::Pipeline skyboxPipeline;
        vku::PipelineLayout skyboxPipelineLayout;
        vku::DescriptorSetLayout skyboxDsl;
        VkDescriptorSet skyboxDs;
        vku::Sampler sampler;
        VkImageView lastSkyImageView = nullptr;
        VulkanHandles* handles;
        uint32_t lastSky = 0;
        void updateDescriptors(RenderContext& ctx, uint32_t loadedSkyId);
    public:
        SkyboxPass(VulkanHandles* handles);
        void setup(RenderContext& ctx, VkRenderPass renderPass, VkDescriptorPool descriptorPool);
        void prePass(RenderContext&);
        void execute(RenderContext&);
    };

    class DepthPrepass {
    private:
        vku::Pipeline depthPrePipeline;
        vku::Pipeline alphaTestPipeline;
        vku::Pipeline skinnedPipeline;
        VulkanHandles* handles;
        VkPipelineLayout layout;
    public:
        DepthPrepass(VulkanHandles* handles);
        // Takes in the standard pipeline layout as an additional parameter
        void setup(RenderContext& ctx, VkRenderPass renderPass, VkPipelineLayout pipelineLayout);
        void prePass(RenderContext& ctx);
        void execute(RenderContext& ctx, slib::StaticAllocList<SubmeshDrawInfo>& drawInfo);
    };

    struct FontChar {
        uint32_t codepoint;

        uint16_t x;
        uint16_t y;

        uint16_t width;
        uint16_t height;

        int16_t originX;
        int16_t originY;

        uint16_t advance;
    };

    struct SDFFont {
        robin_hood::unordered_flat_map<uint32_t, FontChar> characters;
        float width;
        float height;
        vku::TextureImage2D atlas;
        uint32_t index;
    };

    class WorldSpaceUIPass {
    private:
        vku::Pipeline textPipeline;
        VkDescriptorSet descriptorSet;
        vku::DescriptorSetLayout descriptorSetLayout;
        vku::PipelineLayout pipelineLayout;
        VulkanHandles* handles;
        vku::Sampler sampler;
        vku::GenericBuffer vb;
        vku::GenericBuffer ib;


        uint32_t nextFontIdx = 0u;
        size_t bufferCapacity = 0;
        SDFFont& getFont(AssetID id);
        void updateBuffers(entt::registry&);
        void loadFont(AssetID font);
    public:
        WorldSpaceUIPass(VulkanHandles* handles);
        void setup(RenderContext& ctx, VkRenderPass renderPass, VkDescriptorPool descriptorPool);
        void prePass(RenderContext& ctx);
        void execute(RenderContext& ctx);
    };

    class LightCullPass {
    private:
        vku::Pipeline pipeline;
        vku::PipelineLayout pipelineLayout;

        vku::DescriptorSetLayout dsl;
        VkDescriptorSet descriptorSet;
        vku::Sampler sampler;

        VkShaderModule shader;
        VulkanHandles* handles;
        RenderTexture* depthStencilImage;
    public:
        LightCullPass(VulkanHandles* handles, RenderTexture* depthStencilImage);
        void setup(RenderContext& ctx, VkBuffer lightBuffer, VkBuffer lightTileInfoBuffer, VkBuffer lightTileBuffer, VkBuffer lightTileLightCountBuffer, VkDescriptorPool descriptorPool);
        void execute(RenderContext& ctx, int tileSize);
        ~LightCullPass();
    };

    struct LightingTile {
        uint32_t lightIdMasks[8];
        uint32_t aoBoxIdMasks[2];
        uint32_t aoSphereIdMasks[2];
    };

    const int MAX_LIGHT_TILES = 40000;

    struct LightTileInfoBuffer {
        uint32_t tileSize;
        uint32_t tilesPerEye;
        uint32_t numTilesX;
        uint32_t numTilesY;
    };

    class PolyRenderPass {
    private:
        vku::RenderPass renderPass;
        vku::RenderPass depthPass;
        vku::Pipeline pipeline;
        vku::Pipeline noBackfaceCullPipeline;
        vku::Pipeline alphaTestPipeline;
        vku::Pipeline skinnedPipeline;
        vku::PipelineLayout pipelineLayout;
        vku::DescriptorSetLayout dsl;

        vku::Pipeline wireframePipeline;
        vku::PipelineLayout wireframePipelineLayout;

        LightUB* lightMapped;
        LightTileInfoBuffer* lightTileInfoMapped;
        std::vector<ModelMatrices*> modelMatricesMapped;
        glm::mat4* skinningMatricesMapped;

        vku::GenericBuffer lightsUB;
        vku::GenericBuffer lightTileInfoBuffer;
        vku::GenericBuffer lightTilesBuffer;
        vku::GenericBuffer lightTileLightCountBuffer;

        std::vector<vku::GenericBuffer> modelMatrixUB;
        vku::GenericBuffer pickingBuffer;
        vku::GenericBuffer skinningMatrixUB;

        VkShaderModule fragmentShader;
        VkShaderModule vertexShader;

        VkShaderModule wireFragmentShader;
        VkShaderModule wireVertexShader;

        vku::Sampler albedoSampler;
        vku::Sampler shadowSampler;

        vku::Framebuffer renderFb;
        vku::Framebuffer depthFb;
        std::vector<VkDescriptorSet> descriptorSets;

        RenderTexture* depthStencilImage;
        RenderTexture* polyImage;

        bool enablePicking;
        int pickX, pickY;
        vku::Event pickEvent;
        bool pickThisFrame;
        bool awaitingResults;
        bool setEventNextFrame;
        bool dsUpdateNeeded = false;

        void updateDescriptorSets(RenderContext& ctx);
        VRCullMeshRenderer* cullMeshRenderer;
        DebugLinesPass* dbgLinesPass;
        SkyboxPass* skyboxPass;
        DepthPrepass* depthPrepass;
        WorldSpaceUIPass* uiPass;
        LightCullPass* lightCullPass;
        VulkanHandles* handles;

        void generateDrawInfo(RenderContext& ctx);
    public:
        PolyRenderPass(VulkanHandles* handles, RenderTexture* depthStencilImage, RenderTexture* polyImage, bool enablePicking = false);
        void setPickCoords(int x, int y) { pickX = x; pickY = y; }
        void setup(RenderContext& ctx, VkDescriptorPool descriptorPool);
        void prePass(RenderContext& ctx);
        void execute(RenderContext& ctx);
        void requestEntityPick();
        void reuploadDescriptors() { dsUpdateNeeded = true; }
        bool getPickedEnt(uint32_t* out);
        virtual ~PolyRenderPass();
    };

    struct CascadeMatrices;
    class ShadowCascadePass {
    private:
        vku::RenderPass renderPass;
        vku::Pipeline pipeline;
        vku::PipelineLayout pipelineLayout;
        vku::DescriptorSetLayout dsl;
        VkDescriptorSet ds;
        RenderTexture* shadowImage;
        vku::Framebuffer shadowFb;
        VkShaderModule shadowVertexShader;
        VkShaderModule shadowFragmentShader;
        vku::UniformBuffer matrixBuffer;
        uint32_t shadowmapRes;
        VulkanHandles* handles;

        void createRenderPass();
        void createDescriptorSet();
    public:
        ShadowCascadePass(VulkanHandles* handles, RenderTexture* shadowImage);
        void setup();
        void prePass(RenderContext& ctx);
        void execute(RenderContext& ctx);
        virtual ~ShadowCascadePass();
    };

    class AdditionalShadowsPass {
    private:
        vku::RenderPass renderPass;
        vku::Framebuffer fb;
        vku::Pipeline pipeline;
        vku::Pipeline alphaTestPipeline;
        vku::PipelineLayout pipelineLayout;
        vku::DescriptorSetLayout dsl;
        VkDescriptorSet descriptorSet;
        vku::Sampler sampler;
        VulkanHandles* handles;
        glm::mat4 shadowMatrices[4];
        bool renderIdx[4];
        bool dsUpdateNeeded = false;
        void updateDescriptorSet(RenderResources);
    public:
        AdditionalShadowsPass(VulkanHandles* handles);
        void reuploadDescriptors() { dsUpdateNeeded = true; }
        void setup(RenderResources resources);
        void prePass(RenderContext& ctx);
        void execute(RenderContext& ctx);
        ~AdditionalShadowsPass();
    };

    class TonemapRenderPass {
    private:
        VkShaderModule tonemapShader;
        vku::DescriptorSetLayout dsl;
        vku::Pipeline pipeline;
        vku::PipelineLayout pipelineLayout;
        VkDescriptorSet descriptorSet;
        VkDescriptorSet rDescriptorSet;
        vku::Sampler sampler;
        VkDescriptorPool dsPool;
        RenderTexture* finalPrePresent;
        RenderTexture* finalPrePresentR;
        RenderTexture* hdrImg;
        VulkanHandles* handles;
    public:
        TonemapRenderPass(VulkanHandles* handles, RenderTexture* hdrImg, RenderTexture* finalPrePresent);
        void setup(RenderContext& ctx, VkDescriptorPool descriptorPool);
        void execute(RenderContext& ctx);
        void setRightFinalImage(RenderTexture* right);
        virtual ~TonemapRenderPass();
    };

    class ImGuiRenderPass {
    private:
        vku::RenderPass renderPass;
        RenderTexture* target;
        Swapchain& currSwapchain;
        VulkanHandles* handles;
    public:
        VkRenderPass getRenderPass() { return renderPass; }
        ImGuiRenderPass(VulkanHandles* handles, Swapchain& swapchain);
        void setup();
        void execute(VkCommandBuffer&,
                uint32_t width, uint32_t height, VkFramebuffer currFb,
                ImDrawData* drawData);
        virtual ~ImGuiRenderPass();
    };
}
