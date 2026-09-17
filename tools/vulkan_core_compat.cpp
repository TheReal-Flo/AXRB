// Shared app-local Vulkan compatibility for the Android gfxstream driver.
// Gfxstream advertises Vulkan 1.3 but omits these promoted extension names.
// Expose aliases only when the physical device's core version supports them.
#include <vulkan/vulkan.h>
#include <android/log.h>
#include <dlfcn.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "vulkan_descriptor_template.h"
#include <sys/system_properties.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"AXRB.VulkanCompat",__VA_ARGS__)
namespace {
bool textureDiagnostics(){static bool enabled=[] {char value[PROP_VALUE_MAX]{};__system_property_get("debug.axrb.texturediag",value);return value[0]=='1';}();return enabled;}
void* realLibrary(){static void* h=dlopen("libvulkan.so",RTLD_NOW|RTLD_LOCAL);return h;}
template<class T>T real(const char* name){return reinterpret_cast<T>(dlsym(realLibrary(),name));}
using TemplateKey = std::pair<VkDevice, VkDescriptorUpdateTemplate>;
std::mutex templateMutex;
std::map<TemplateKey, std::shared_ptr<const std::vector<VkDescriptorUpdateTemplateEntry>>> templates;
struct Promotion {const char* name;uint32_t api;uint32_t revision;};
constexpr Promotion promotions[]={
    {VK_KHR_MULTIVIEW_EXTENSION_NAME,VK_API_VERSION_1_1,VK_KHR_MULTIVIEW_SPEC_VERSION},
    {VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,VK_API_VERSION_1_2,VK_KHR_CREATE_RENDERPASS_2_SPEC_VERSION},
    {VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,VK_API_VERSION_1_2,VK_KHR_DEPTH_STENCIL_RESOLVE_SPEC_VERSION},
};
bool has(const std::vector<VkExtensionProperties>& es,const char* name){for(auto& e:es)if(!std::strcmp(e.extensionName,name))return true;return false;}
VkResult deviceExtensions(VkPhysicalDevice physical,std::vector<VkExtensionProperties>& es){
    auto enumerate=real<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
    for(int tries=0;tries<4;++tries){uint32_t n=0;auto r=enumerate(physical,nullptr,&n,nullptr);if(r!=VK_SUCCESS)return r;es.resize(n);r=enumerate(physical,nullptr,&n,es.data());es.resize(n);if(r!=VK_INCOMPLETE)return r;}return VK_INCOMPLETE;
}
uint32_t version(VkPhysicalDevice physical){VkPhysicalDeviceProperties p{};real<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(physical,&p);return p.apiVersion;}
PFN_vkVoidFunction intercept(const char* name);
const char* coreAlias(const char* name){
    if(!std::strcmp(name,"vkCreateRenderPass2KHR"))return "vkCreateRenderPass2";
    if(!std::strcmp(name,"vkCmdBeginRenderPass2KHR"))return "vkCmdBeginRenderPass2";
    if(!std::strcmp(name,"vkCmdNextSubpass2KHR"))return "vkCmdNextSubpass2";
    if(!std::strcmp(name,"vkCmdEndRenderPass2KHR"))return "vkCmdEndRenderPass2";
    return name;
}
}
extern "C" {
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device,const VkAllocationCallbacks* alloc){
    {std::lock_guard lock(templateMutex);for(auto it=templates.begin();it!=templates.end();){if(it->first.first==device)it=templates.erase(it);else ++it;}}
    real<PFN_vkDestroyDevice>("vkDestroyDevice")(device,alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice device,const VkDescriptorUpdateTemplateCreateInfo* info,const VkAllocationCallbacks* alloc,VkDescriptorUpdateTemplate* out){
    std::shared_ptr<const std::vector<VkDescriptorUpdateTemplateEntry>> entries;
    if(info->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET &&
       std::all_of(info->pDescriptorUpdateEntries, info->pDescriptorUpdateEntries + info->descriptorUpdateEntryCount,
           [](const auto& e){ return axrb::descriptor_template_supported(e.descriptorType); })) {
        entries = std::make_shared<const std::vector<VkDescriptorUpdateTemplateEntry>>(
            info->pDescriptorUpdateEntries, info->pDescriptorUpdateEntries + info->descriptorUpdateEntryCount);
    }
    auto result=real<PFN_vkCreateDescriptorUpdateTemplate>("vkCreateDescriptorUpdateTemplate")(device,info,alloc,out);
    if(result==VK_SUCCESS && entries){std::lock_guard lock(templateMutex);templates[{device,*out}]=std::move(entries);}
    return result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice device,VkDescriptorUpdateTemplate value,const VkAllocationCallbacks* alloc){
    {std::lock_guard lock(templateMutex);templates.erase({device,value});}
    real<PFN_vkDestroyDescriptorUpdateTemplate>("vkDestroyDescriptorUpdateTemplate")(device,value,alloc);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice device,VkDescriptorSet set,VkDescriptorUpdateTemplate value,const void* data){
    std::shared_ptr<const std::vector<VkDescriptorUpdateTemplateEntry>> entries;
    {std::lock_guard lock(templateMutex);auto it=templates.find({device,value});if(it!=templates.end())entries=it->second;}
    if(!entries){real<PFN_vkUpdateDescriptorSetWithTemplate>("vkUpdateDescriptorSetWithTemplate")(device,set,value,data);return;}
    axrb::ExpandedDescriptorTemplate expanded(*entries,set,data);
    real<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device,static_cast<uint32_t>(expanded.writes.size()),expanded.writes.data(),0,nullptr);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplateKHR(VkDevice d,const VkDescriptorUpdateTemplateCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorUpdateTemplate* o){return vkCreateDescriptorUpdateTemplate(d,i,a,o);}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplateKHR(VkDevice d,VkDescriptorUpdateTemplate t,const VkAllocationCallbacks* a){vkDestroyDescriptorUpdateTemplate(d,t,a);}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateKHR(VkDevice d,VkDescriptorSet s,VkDescriptorUpdateTemplate t,const void* p){vkUpdateDescriptorSetWithTemplate(d,s,t,p);}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device,const VkImageCreateInfo* info,const VkAllocationCallbacks* alloc,VkImage* out){
    auto result=real<PFN_vkCreateImage>("vkCreateImage")(device,info,alloc,out);
    if(textureDiagnostics()) LOG("Texture create image=%p size=%ux%ux%u mips=%u layers=%u format=%d usage=%x result=%d",result==VK_SUCCESS?(void*)*out:nullptr,info->extent.width,info->extent.height,info->extent.depth,info->mipLevels,info->arrayLayers,info->format,info->usage,result);
    return result;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer cmd,VkBuffer source,VkImage dest,VkImageLayout layout,uint32_t count,const VkBufferImageCopy* regions){
    if(textureDiagnostics())for(uint32_t i=0;i<count;++i)LOG("Texture upload image=%p mip=%u size=%ux%ux%u layer=%u count=%u",(void*)dest,regions[i].imageSubresource.mipLevel,regions[i].imageExtent.width,regions[i].imageExtent.height,regions[i].imageExtent.depth,regions[i].imageSubresource.baseArrayLayer,regions[i].imageSubresource.layerCount);
    real<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(cmd,source,dest,layout,count,regions);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice device,const VkSamplerCreateInfo* info,const VkAllocationCallbacks* alloc,VkSampler* out){
    auto result=real<PFN_vkCreateSampler>("vkCreateSampler")(device,info,alloc,out);
    if(textureDiagnostics())LOG("Texture sampler bias=%g lod=%g..%g filter=%d/%d mipmode=%d result=%d",info->mipLodBias,info->minLod,info->maxLod,info->minFilter,info->magFilter,info->mipmapMode,result);
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physical,const char* layer,uint32_t* count,VkExtensionProperties* out){
    if(layer)return real<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties")(physical,layer,count,out);
    std::vector<VkExtensionProperties> es;auto r=deviceExtensions(physical,es);if(r!=VK_SUCCESS)return r;
    uint32_t api=version(physical);
    for(auto& p:promotions)if(api>=p.api&&!has(es,p.name)){VkExtensionProperties e{};std::strncpy(e.extensionName,p.name,sizeof(e.extensionName)-1);e.specVersion=p.revision;es.push_back(e);}
    if(!out){*count=es.size();return VK_SUCCESS;}uint32_t n=std::min<uint32_t>(*count,es.size());std::copy_n(es.data(),n,out);*count=n;return n<es.size()?VK_INCOMPLETE:VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* alloc,VkDevice* out){
    std::vector<VkExtensionProperties> es;auto r=deviceExtensions(physical,es);if(r!=VK_SUCCESS)return r;
    uint32_t api=version(physical);std::vector<const char*> names;
    for(uint32_t i=0;i<info->enabledExtensionCount;++i){const char* name=info->ppEnabledExtensionNames[i];bool promoted=false;for(auto& p:promotions)if(api>=p.api&&!std::strcmp(name,p.name)&&!has(es,name)){promoted=true;LOG("Using core support for %s",name);}if(!promoted)names.push_back(name);}
    for(auto* p=reinterpret_cast<const VkBaseInStructure*>(info->pNext);p;p=p->pNext)LOG("Device feature sType=%u",p->sType);
    auto changed=*info;changed.enabledExtensionCount=names.size();changed.ppEnabledExtensionNames=names.data();
    return real<PFN_vkCreateDevice>("vkCreateDevice")(physical,&changed,alloc,out);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char* name){
    if(auto f=intercept(name))return f;
    return real<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr")(instance,coreAlias(name));
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char* name){
    if(auto f=intercept(name))return f;
    return real<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr")(device,coreAlias(name));
}
}
namespace {
PFN_vkVoidFunction intercept(const char* name){
#define ENTRY(n) if(!std::strcmp(name,#n))return reinterpret_cast<PFN_vkVoidFunction>(n)
    ENTRY(vkGetInstanceProcAddr);ENTRY(vkGetDeviceProcAddr);ENTRY(vkEnumerateDeviceExtensionProperties);ENTRY(vkCreateDevice);
    ENTRY(vkCreateImage);ENTRY(vkCmdCopyBufferToImage);ENTRY(vkCreateSampler);
    ENTRY(vkDestroyDevice);
    ENTRY(vkCreateDescriptorUpdateTemplate);ENTRY(vkDestroyDescriptorUpdateTemplate);ENTRY(vkUpdateDescriptorSetWithTemplate);
    ENTRY(vkCreateDescriptorUpdateTemplateKHR);ENTRY(vkDestroyDescriptorUpdateTemplateKHR);ENTRY(vkUpdateDescriptorSetWithTemplateKHR);
#undef ENTRY
    return nullptr;
}
}
