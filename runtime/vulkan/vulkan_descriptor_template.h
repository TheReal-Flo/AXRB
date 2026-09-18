#pragma once
#include <vulkan/vulkan.h>
#include <cstring>
#include <cstddef>
#include <deque>
#include <vector>

namespace axrb {
inline bool descriptor_template_supported(VkDescriptorType type) {
    return type >= VK_DESCRIPTOR_TYPE_SAMPLER && type <= VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
}
// Immutable, compiled once at template creation. Offsets refer to flat scratch arrays.
struct DescriptorTemplateLayout {
    enum class Kind { Image, Buffer, Texel };
    struct Entry {
        VkWriteDescriptorSet write{};
        size_t sourceOffset, sourceStride, destination;
        Kind kind;
        bool sampler, image;
    };
    std::vector<Entry> entries;
    size_t images = 0, buffers = 0, views = 0;
    DescriptorTemplateLayout(const VkDescriptorUpdateTemplateEntry* input, size_t count) {
        entries.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto& source = input[i];
            Entry e{};
            e.write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            e.write.dstBinding = source.dstBinding;
            e.write.dstArrayElement = source.dstArrayElement;
            e.write.descriptorCount = source.descriptorCount;
            e.write.descriptorType = source.descriptorType;
            e.sourceOffset = source.offset; e.sourceStride = source.stride;
            if (source.descriptorType >= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && source.descriptorType <= VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
                e.kind = Kind::Buffer; e.destination = buffers; buffers += source.descriptorCount;
            } else if (source.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || source.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
                e.kind = Kind::Texel; e.destination = views; views += source.descriptorCount;
            } else {
                e.kind = Kind::Image; e.destination = images; images += source.descriptorCount;
                e.sampler = source.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER || source.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                e.image = source.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER;
            }
            entries.push_back(e);
        }
    }
};
// All arrays retain their capacity between calls. No pointers escape the downstream call.
struct ExpandedDescriptorTemplate {
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkBufferView> views;
    std::vector<VkWriteDescriptorSet> writes;
    void expand(const DescriptorTemplateLayout& layout, VkDescriptorSet set, const void* data) {
        images.resize(layout.images); buffers.resize(layout.buffers); views.resize(layout.views);
        writes.resize(layout.entries.size());
        for (size_t i = 0; i < layout.entries.size(); ++i) {
            const auto& e = layout.entries[i];
            auto& w = writes[i]; w = e.write; w.dstSet = set;
            if (!w.descriptorCount) continue;
            if (e.kind == DescriptorTemplateLayout::Kind::Buffer) w.pBufferInfo = buffers.data() + e.destination;
            else if (e.kind == DescriptorTemplateLayout::Kind::Texel) w.pTexelBufferView = views.data() + e.destination;
            else w.pImageInfo = images.data() + e.destination;
            for (uint32_t j = 0; j < w.descriptorCount; ++j) {
                const auto* src = static_cast<const unsigned char*>(data) + e.sourceOffset + j * e.sourceStride;
                const size_t dst = e.destination + j;
                if (e.kind == DescriptorTemplateLayout::Kind::Buffer) std::memcpy(&buffers[dst], src, sizeof(VkDescriptorBufferInfo));
                else if (e.kind == DescriptorTemplateLayout::Kind::Texel) std::memcpy(&views[dst], src, sizeof(VkBufferView));
                else {
                    // Reused slots must not retain ignored handles from a previous template.
                    images[dst] = {};
                    if (e.sampler) std::memcpy(&images[dst].sampler, src + offsetof(VkDescriptorImageInfo, sampler), sizeof(VkSampler));
                    if (e.image) {
                        std::memcpy(&images[dst].imageView, src + offsetof(VkDescriptorImageInfo, imageView), sizeof(VkImageView));
                        std::memcpy(&images[dst].imageLayout, src + offsetof(VkDescriptorImageInfo, imageLayout), sizeof(VkImageLayout));
                    }
                }
            }
        }
    }
};
// Per-thread scratch, with separate stable slots for downstream re-entry.
// A single TLS vector would invalidate the outer call's pointers during recursion.
struct DescriptorScratchPool {
    std::deque<ExpandedDescriptorTemplate> slots;
    size_t depth = 0;
    struct Lease {
        DescriptorScratchPool& pool;
        ExpandedDescriptorTemplate& scratch;
        explicit Lease(DescriptorScratchPool& owner) : pool(owner), scratch(owner.acquire()) {}
        ~Lease() { --pool.depth; }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
    };
private:
    ExpandedDescriptorTemplate& acquire() {
        if (depth == slots.size()) slots.emplace_back();
        return slots[depth++];
    }
};
}
