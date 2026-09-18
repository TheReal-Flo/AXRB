// Mock only the downstream driver; exercise the actual layer interceptors on Android.
#include "../../runtime/vulkan/android_vulkan_layer.cpp"
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);std::abort(); } } while(0)
namespace {
std::atomic<unsigned> resolved{0},updates{0},fallbacks{0},destroyed{0};
thread_local bool nest=false;
VkDescriptorUpdateTemplate nestedTemplate=reinterpret_cast<VkDescriptorUpdateTemplate>(2);
VkDescriptorImageInfo payload{reinterpret_cast<VkSampler>(17),reinterpret_cast<VkImageView>(23),VK_IMAGE_LAYOUT_GENERAL};
VKAPI_ATTR void VKAPI_CALL update(VkDevice device,uint32_t count,const VkWriteDescriptorSet* writes,uint32_t,const VkCopyDescriptorSet*) {
    CHECK(count==1);CHECK(writes[0].pImageInfo);CHECK(writes[0].pImageInfo[0].imageView==payload.imageView);
    CHECK(writes[0].pImageInfo[0].sampler==VK_NULL_HANDLE);
    const auto* outer=writes[0].pImageInfo;const auto set=writes[0].dstSet;
    if(nest){nest=false;vkUpdateDescriptorSetWithTemplate(device,reinterpret_cast<VkDescriptorSet>(99),nestedTemplate,&payload);}
    CHECK(writes[0].pImageInfo==outer);CHECK(writes[0].dstSet==set);CHECK(outer[0].imageView==payload.imageView);
    ++updates;
}
VKAPI_ATTR void VKAPI_CALL fallback(VkDevice,VkDescriptorSet,VkDescriptorUpdateTemplate,const void*){++fallbacks;}
VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice,const VkDescriptorUpdateTemplateCreateInfo*,const VkAllocationCallbacks*,VkDescriptorUpdateTemplate* out){return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL destroy(VkDevice,VkDescriptorUpdateTemplate,const VkAllocationCallbacks*){++destroyed;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice,const char*){++resolved;return nullptr;}
}
int main(){
    void* dispatch=reinterpret_cast<void*>(123);VkDevice h=reinterpret_cast<VkDevice>(&dispatch);
    devices[key(h)]={h,resolver,create,destroy,fallback,update};
    VkDescriptorUpdateTemplateEntry entry{3,1,1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,0,0};
    VkDescriptorUpdateTemplateCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
    info.descriptorUpdateEntryCount=1;info.pDescriptorUpdateEntries=&entry;
    info.templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    auto t=reinterpret_cast<VkDescriptorUpdateTemplate>(1);
    CHECK(vkCreateDescriptorUpdateTemplate(h,&info,nullptr,&t)==VK_SUCCESS);
    CHECK(vkCreateDescriptorUpdateTemplateKHR(h,&info,nullptr,&nestedTemplate)==VK_SUCCESS);
    nest=true;vkUpdateDescriptorSetWithTemplateKHR(h,reinterpret_cast<VkDescriptorSet>(5),t,&payload);
    std::vector<std::thread> threads;
    for(unsigned i=0;i<4;++i)threads.emplace_back([&,i]{for(unsigned j=0;j<10000;++j)vkUpdateDescriptorSetWithTemplate(h,reinterpret_cast<VkDescriptorSet>(uintptr_t(i+10)),t,&payload);});
    // Retire an unrelated template while other threads update the shared template.
    vkDestroyDescriptorUpdateTemplate(h,nestedTemplate,nullptr);
    for(auto& thread:threads)thread.join();
    CHECK(updates==40002);CHECK(resolved==0);
    std::shared_ptr<const Layout> retained;
    {std::shared_lock lock(mutex);retained=templates.at({h,t});}
    vkDestroyDescriptorUpdateTemplateKHR(h,t,nullptr);
    axrb::ExpandedDescriptorTemplate scratch;scratch.expand(*retained,reinterpret_cast<VkDescriptorSet>(5),&payload);
    CHECK(scratch.writes[0].pImageInfo[0].imageView==payload.imageView);
    // Same numeric handle on another device must not pick up this device's layout.
    void* dispatch2=reinterpret_cast<void*>(456);VkDevice h2=reinterpret_cast<VkDevice>(&dispatch2);
    devices[key(h2)]={h2,resolver,create,destroy,fallback,update};
    CHECK(vkCreateDescriptorUpdateTemplate(h,&info,nullptr,&t)==VK_SUCCESS);
    info.templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
    CHECK(vkCreateDescriptorUpdateTemplate(h2,&info,nullptr,&t)==VK_SUCCESS);
    vkUpdateDescriptorSetWithTemplate(h2,reinterpret_cast<VkDescriptorSet>(5),t,&payload);
    CHECK(fallbacks==1);
    entry.descriptorType=VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
    info.templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    CHECK(vkCreateDescriptorUpdateTemplate(h2,&info,nullptr,&t)==VK_SUCCESS);
    vkUpdateDescriptorSetWithTemplateKHR(h2,reinterpret_cast<VkDescriptorSet>(5),t,&payload);
    CHECK(fallbacks==2);CHECK(resolved==0);CHECK(destroyed==2);
    std::puts("Layer dispatch, re-entry, concurrent reads/retirement, retained lifetime and fallback passed");
}
