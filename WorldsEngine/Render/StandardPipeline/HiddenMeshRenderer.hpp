#pragma once
#include <stdint.h>
#include <Util/UniquePtr.hpp>

namespace R2::VK
{
    class DescriptorSetLayout;
    class DescriptorSet;
    class Pipeline;
    class PipelineLayout;
    class Buffer;
    class Texture;
    class Core;
    class CommandBuffer;
}

namespace worlds
{
    class VKRenderer;
    struct EngineInterfaces;

    class HiddenMeshRenderer
    {
        const EngineInterfaces& interfaces;
        UniquePtr<R2::VK::Buffer> vertexBuffer;
        UniquePtr<R2::VK::Buffer> indexBuffer;
        UniquePtr<R2::VK::Pipeline> pipeline;
        UniquePtr<R2::VK::PipelineLayout> pipelineLayout;
        uint32_t leftIndexCount;
        uint32_t rightIndexCount;
        uint32_t leftVertCount;
    public:
        HiddenMeshRenderer(const EngineInterfaces& interfaces, int sampleCount);
        void Execute(R2::VK::CommandBuffer& cb);
    };
}