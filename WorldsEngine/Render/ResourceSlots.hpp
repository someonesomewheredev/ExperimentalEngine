#pragma once
#include <robin_hood.h>
#include "../Core/Fatal.hpp"
#include "Loaders/TextureLoader.hpp"
#include "Loaders/CubemapLoader.hpp"
#include "PackedMaterial.hpp"
#include <array>
#include <nlohmann/json_fwd.hpp>
#include <slib/Bitset.hpp>

namespace std {
    class mutex;
}

namespace worlds {
    const uint32_t NUM_TEX_SLOTS = 128;
    const uint32_t NUM_MAT_SLOTS = 128;
    const uint32_t NUM_CUBEMAP_SLOTS = 32;
    struct VulkanHandles;
    class CubemapConvoluter;

    template <typename slotType, uint32_t slotCount, typename key>
    class ResourceSlots {
    protected:
        std::array<slotType, slotCount> slots;
        slib::Bitset<slotCount> present;
        robin_hood::unordered_flat_map<key, uint32_t> lookup;
        robin_hood::unordered_flat_map<uint32_t, key> reverseLookup;
        virtual uint32_t load(key k) = 0;

        uint32_t getFreeSlot() {
            for (uint32_t i = 0; i < slotCount; i++) {
                if (!present[i]) {
                    return i;
                }
            }

            return ~0u;
        }
    public:
        uint32_t loadedCount;
        ResourceSlots() : slots(), present(), loadedCount(0) {
            for (uint32_t i = 0; i < slotCount; i++) {
                present[i] = false;
            }
        }

        virtual ~ResourceSlots() {}

        constexpr uint32_t size() const { return slotCount; }
        slotType* getSlots() { return slots.data(); }
        bool isSlotPresent(int idx) const { return present[idx]; }
        slotType& operator[](int idx) { return slots[idx]; }
        slotType& operator[](uint32_t idx) { return slots[idx]; }

        virtual uint32_t loadOrGet(key k) {
            auto iter = lookup.find(k);

            if (iter != lookup.end()) return iter->second;

            return load(k);
        }

        uint32_t get(key k) const { return lookup.at(k); }
        key getKeyForSlot(uint32_t idx) const { return reverseLookup.at(idx); }
        bool isLoaded(key k) const { return lookup.find(k) != lookup.end(); }
        virtual void unload(uint32_t idx) = 0;
    };

    class TextureSlots : public ResourceSlots<vku::TextureImage2D, NUM_TEX_SLOTS, AssetID> {
    protected:
        uint32_t load(AssetID asset) override;
    private:
        std::shared_ptr<VulkanHandles> vkCtx;
        VkCommandBuffer cb;
        uint32_t frameIdx;
        std::mutex* slotMutex;
    public:
        bool frameStarted = false;

        TextureSlots(std::shared_ptr<VulkanHandles> vkCtx);
        void setUploadCommandBuffer(VkCommandBuffer cb, uint32_t frameIdx);

        void unload(uint32_t idx) override;
        ~TextureSlots();
    };

    struct PackedMaterial;
    struct MatExtraData {
        bool noCull = false;
        bool wireframe = false;
        AssetID overrideShader = INVALID_ASSET;
    };

    class MaterialSlots : public ResourceSlots<PackedMaterial, NUM_MAT_SLOTS, AssetID> {
    protected:
        void parseMaterial(AssetID asset, PackedMaterial& mat, MatExtraData& extraDat);

        uint32_t load(AssetID asset) override;
    private:
        uint32_t getTexture(nlohmann::json& j, std::string key);
        std::shared_ptr<VulkanHandles> vkCtx;
        std::array<MatExtraData, NUM_MAT_SLOTS> matExtraData;
        TextureSlots& texSlots;
        std::mutex* slotMutex;
    public:
        MatExtraData& getExtraDat(uint32_t slot);
        MaterialSlots(std::shared_ptr<VulkanHandles> vkCtx, TextureSlots& texSlots);
        void unload(uint32_t idx) override;
        ~MaterialSlots();
    };

    class CubemapSlots : public ResourceSlots<vku::TextureImageCube, NUM_CUBEMAP_SLOTS, AssetID> {
    protected:
        uint32_t load(AssetID asset) override;
    private:
        std::shared_ptr<VulkanHandles> vkCtx;
        VkCommandBuffer cb;
        uint32_t imageIndex;
        uint32_t missingSlot;
        std::shared_ptr<CubemapConvoluter> cc;
    public:
        uint32_t loadOrGet(AssetID asset) override;

        void setUploadCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex) {
            this->cb = cb;
            this->imageIndex = imageIndex;
        }

        CubemapConvoluter* convoluter() {
            return cc.get();
        }

        CubemapSlots(std::shared_ptr<VulkanHandles> vkCtx, std::shared_ptr<CubemapConvoluter> cc);

        void unload(uint32_t idx) override;
    };
}
