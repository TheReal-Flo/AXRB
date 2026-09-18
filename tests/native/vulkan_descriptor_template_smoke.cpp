#include "../../runtime/vulkan/vulkan_descriptor_template.h"
#include <array>
#include <cstdio>
int main(){
    struct Data { uint64_t padding; VkDescriptorImageInfo sampled[2]; VkDescriptorImageInfo sampler; VkDescriptorBufferInfo buffer; VkBufferView view; } data{};
    data.sampled[0]={reinterpret_cast<VkSampler>(99),reinterpret_cast<VkImageView>(11),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    data.sampled[1]={reinterpret_cast<VkSampler>(99),reinterpret_cast<VkImageView>(12),VK_IMAGE_LAYOUT_GENERAL};
    data.sampler={reinterpret_cast<VkSampler>(13),reinterpret_cast<VkImageView>(99),VK_IMAGE_LAYOUT_UNDEFINED};
    data.buffer={reinterpret_cast<VkBuffer>(14),32,64};data.view=reinterpret_cast<VkBufferView>(15);
    std::vector<VkDescriptorUpdateTemplateEntry> entries{
        {3,1,2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,offsetof(Data,sampled),sizeof(VkDescriptorImageInfo)},
        {4,0,1,VK_DESCRIPTOR_TYPE_SAMPLER,offsetof(Data,sampler),0},
        {5,0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,offsetof(Data,buffer),0},
        {6,0,1,VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,offsetof(Data,view),0},
        {7,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,offsetof(Data,sampled),0}};
    axrb::DescriptorTemplateLayout layout(entries.data(),entries.size());
    axrb::ExpandedDescriptorTemplate out;
    out.expand(layout,reinterpret_cast<VkDescriptorSet>(16),&data);
    if(out.writes.size()!=5 || out.writes[0].dstBinding!=3 || out.writes[0].dstArrayElement!=1 ||
       out.writes[0].pImageInfo[1].imageView!=data.sampled[1].imageView || out.writes[0].pImageInfo[0].sampler ||
       out.writes[1].pImageInfo[0].imageView || out.writes[1].pImageInfo[0].sampler!=data.sampler.sampler ||
       out.writes[2].pBufferInfo[0].offset!=32 || out.writes[2].pBufferInfo[0].range!=64 ||
       out.writes[3].pTexelBufferView[0]!=data.view || out.writes[4].pImageInfo[0].sampler!=data.sampled[0].sampler ||
       axrb::descriptor_template_supported(VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK))return 1;
    // Zero stride repeats the same payload; packed/unaligned entries must be copied safely.
    std::array<unsigned char,sizeof(data)+1> unaligned{};std::memcpy(unaligned.data()+1,&data,sizeof(data));
    entries[0].stride=0;
    axrb::DescriptorTemplateLayout repeatedLayout(entries.data(),entries.size());
    axrb::ExpandedDescriptorTemplate repeated;
    repeated.expand(repeatedLayout,reinterpret_cast<VkDescriptorSet>(16),unaligned.data()+1);
    if(repeated.writes[0].pImageInfo[1].imageView!=data.sampled[0].imageView)return 1;
    const auto* imageStorage=out.images.data();const auto* writeStorage=out.writes.data();
    for(unsigned i=0;i<10000;++i)out.expand(layout,reinterpret_cast<VkDescriptorSet>(17),&data);
    if(out.images.data()!=imageStorage || out.writes.data()!=writeStorage || out.writes[0].dstSet!=reinterpret_cast<VkDescriptorSet>(17))return 2;
    // Reuse a combined slot as sampler-only and then image-only, clearing stale fields.
    for(auto type:{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_SAMPLER,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE}){
        VkDescriptorUpdateTemplateEntry e{0,0,1,type,offsetof(Data,sampled),0};
        axrb::DescriptorTemplateLayout one(&e,1);out.expand(one,reinterpret_cast<VkDescriptorSet>(16),&data);
        const auto& image=out.writes[0].pImageInfo[0];
        if(type==VK_DESCRIPTOR_TYPE_SAMPLER && image.imageView)return 3;
        if(type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && image.sampler)return 4;
        if(out.writes[0].pBufferInfo || out.writes[0].pTexelBufferView)return 5;
    }
    axrb::DescriptorTemplateLayout empty(nullptr,0);out.expand(empty,VK_NULL_HANDLE,nullptr);
    if(!out.writes.empty())return 6;
    std::puts("Descriptor template expansion and allocation reuse passed");
}
