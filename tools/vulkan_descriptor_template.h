#pragma once
#include <vulkan/vulkan.h>
#include <cstring>
#include <cstddef>
#include <vector>

namespace axrb {
inline bool descriptor_template_supported(VkDescriptorType type) {
    return type >= VK_DESCRIPTOR_TYPE_SAMPLER && type <= VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
}
// Expand on the guest side, before gfxstream packs template data for transport.
// Copy only meaningful image fields: ignored sampler/view fields need not hold valid handles.
struct ExpandedDescriptorTemplate {
    struct Payload {
        std::vector<VkDescriptorImageInfo> images;
        std::vector<VkDescriptorBufferInfo> buffers;
        std::vector<VkBufferView> views;
    };
    std::vector<Payload> payloads;
    std::vector<VkWriteDescriptorSet> writes;
    ExpandedDescriptorTemplate(const std::vector<VkDescriptorUpdateTemplateEntry>& entries,
                               VkDescriptorSet set, const void* data) : payloads(entries.size()), writes(entries.size()) {
        for(size_t i=0;i<entries.size();++i){
            const auto& e=entries[i]; auto& p=payloads[i]; auto& w=writes[i];
            w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=e.dstBinding;
            w.dstArrayElement=e.dstArrayElement;w.descriptorCount=e.descriptorCount;w.descriptorType=e.descriptorType;
            const bool buffer=e.descriptorType>=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && e.descriptorType<=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            const bool texel=e.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || e.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            if(buffer){p.buffers.resize(e.descriptorCount);w.pBufferInfo=p.buffers.data();}
            else if(texel){p.views.resize(e.descriptorCount);w.pTexelBufferView=p.views.data();}
            else{p.images.resize(e.descriptorCount);w.pImageInfo=p.images.data();}
            for(uint32_t j=0;j<e.descriptorCount;++j){
                const auto* src=static_cast<const unsigned char*>(data)+e.offset+j*e.stride;
                if(buffer)std::memcpy(&p.buffers[j],src,sizeof(VkDescriptorBufferInfo));
                else if(texel)std::memcpy(&p.views[j],src,sizeof(VkBufferView));
                else {
                    if(e.descriptorType==VK_DESCRIPTOR_TYPE_SAMPLER || e.descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                        std::memcpy(&p.images[j].sampler,src+offsetof(VkDescriptorImageInfo,sampler),sizeof(VkSampler));
                    if(e.descriptorType!=VK_DESCRIPTOR_TYPE_SAMPLER){
                        std::memcpy(&p.images[j].imageView,src+offsetof(VkDescriptorImageInfo,imageView),sizeof(VkImageView));
                        std::memcpy(&p.images[j].imageLayout,src+offsetof(VkDescriptorImageInfo,imageLayout),sizeof(VkImageLayout));
                    }
                }
            }
        }
    }
};
}
