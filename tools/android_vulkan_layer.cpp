// System-loaded AXRB layer. No application library replacement or ELF rewriting.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include "vulkan_descriptor_template.h"

namespace {
constexpr const char* layerName="VK_LAYER_AXRB_runtime";
void* key(const void* h){return h?*reinterpret_cast<void* const*>(h):nullptr;}
struct Instance {VkInstance handle;PFN_vkGetInstanceProcAddr gipa;};
struct Device {VkDevice handle;PFN_vkGetDeviceProcAddr gdpa;};
std::mutex mutex;
std::map<void*,Instance> instances;
std::map<void*,Device> devices;
using Entries=std::vector<VkDescriptorUpdateTemplateEntry>;
std::map<std::pair<VkDevice,VkDescriptorUpdateTemplate>,std::shared_ptr<const Entries>> templates;
Instance instance(const void* h){std::lock_guard lock(mutex);return instances.at(key(h));}
Device device(const void* h){std::lock_guard lock(mutex);return devices.at(key(h));}
template<class T>T function(Instance s,const char* n){return reinterpret_cast<T>(s.gipa(s.handle,n));}
template<class T>T function(Device s,const char* n){return reinterpret_cast<T>(s.gdpa(s.handle,n));}
struct Promotion {const char* name;uint32_t api,revision;};
constexpr Promotion promotions[]={
    {VK_KHR_MULTIVIEW_EXTENSION_NAME,VK_API_VERSION_1_1,VK_KHR_MULTIVIEW_SPEC_VERSION},
    {VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,VK_API_VERSION_1_2,VK_KHR_CREATE_RENDERPASS_2_SPEC_VERSION},
    {VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,VK_API_VERSION_1_2,VK_KHR_DEPTH_STENCIL_RESOLVE_SPEC_VERSION}};
uint32_t apiVersion(Instance s,VkPhysicalDevice p){VkPhysicalDeviceProperties v{};function<PFN_vkGetPhysicalDeviceProperties>(s,"vkGetPhysicalDeviceProperties")(p,&v);return v.apiVersion;}
bool has(const std::vector<VkExtensionProperties>& es,const char* n){return std::any_of(es.begin(),es.end(),[&](auto& e){return !std::strcmp(e.extensionName,n);});}
VkResult extensions(Instance s,VkPhysicalDevice p,std::vector<VkExtensionProperties>& es){
    auto enumerate=function<PFN_vkEnumerateDeviceExtensionProperties>(s,"vkEnumerateDeviceExtensionProperties");
    for(int i=0;i<4;++i){uint32_t n=0;auto r=enumerate(p,nullptr,&n,nullptr);if(r!=VK_SUCCESS)return r;es.resize(n);r=enumerate(p,nullptr,&n,es.data());es.resize(n);if(r!=VK_INCOMPLETE)return r;}return VK_INCOMPLETE;
}
const char* alias(const char* name){
    if(!std::strcmp(name,"vkCreateRenderPass2KHR"))return "vkCreateRenderPass2";
    if(!std::strcmp(name,"vkCmdBeginRenderPass2KHR"))return "vkCmdBeginRenderPass2";
    if(!std::strcmp(name,"vkCmdNextSubpass2KHR"))return "vkCmdNextSubpass2";
    if(!std::strcmp(name,"vkCmdEndRenderPass2KHR"))return "vkCmdEndRenderPass2";
    return name;
}
PFN_vkVoidFunction intercept(const char*);
bool brokenDebugNames(){
    char value[PROP_VALUE_MAX]{};
    __system_property_get("debug.axrb.gfxstream_debug_names",value);
    return !std::strcmp(value,"1");
}
bool wrappedObject(VkObjectType type){
    switch(type){
    case VK_OBJECT_TYPE_INSTANCE: case VK_OBJECT_TYPE_PHYSICAL_DEVICE:
    case VK_OBJECT_TYPE_DEVICE: case VK_OBJECT_TYPE_QUEUE:
    case VK_OBJECT_TYPE_COMMAND_BUFFER: case VK_OBJECT_TYPE_COMMAND_POOL:
    case VK_OBJECT_TYPE_BUFFER: case VK_OBJECT_TYPE_FENCE:
    case VK_OBJECT_TYPE_SEMAPHORE: return true;
    default: return false;
    }
}
}
extern "C" {
// Mesa bf8862b49f18269ff41d88deab826bc5cea3141a: GFXStream's opaque
// host handles are not vk_object_base pointers. Match the upstream guard
// until the affected system image includes the driver fix.
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice h,const VkDebugUtilsObjectNameInfoEXT* info){
    if(brokenDebugNames()&&!wrappedObject(info->objectType))return VK_SUCCESS;
    auto next=function<PFN_vkSetDebugUtilsObjectNameEXT>(device(h),"vkSetDebugUtilsObjectNameEXT");
    return next?next(h,info):VK_ERROR_EXTENSION_NOT_PRESENT;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* count,VkLayerProperties* out){
    if(!out){*count=1;return VK_SUCCESS;}if(!*count)return VK_INCOMPLETE;
    *out={};std::strcpy(out->layerName,layerName);std::strcpy(out->description,"AXRB Android runtime compatibility");
    out->specVersion=VK_API_VERSION_1_3;out->implementationVersion=1;*count=1;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice,uint32_t* count,VkLayerProperties* out){return vkEnumerateInstanceLayerProperties(count,out);}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* name,uint32_t* count,VkExtensionProperties*){
    if(!name||std::strcmp(name,layerName))return VK_ERROR_LAYER_NOT_PRESENT;*count=0;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice h,const char* name,uint32_t* count,VkExtensionProperties* out){
    if(name){if(std::strcmp(name,layerName))return VK_ERROR_LAYER_NOT_PRESENT;*count=0;return VK_SUCCESS;}
    auto s=instance(h);std::vector<VkExtensionProperties> es;auto r=extensions(s,h,es);if(r!=VK_SUCCESS)return r;
    auto api=apiVersion(s,h);for(auto& p:promotions)if(api>=p.api&&!has(es,p.name)){VkExtensionProperties e{};std::strcpy(e.extensionName,p.name);e.specVersion=p.revision;es.push_back(e);}
    if(!out){*count=es.size();return VK_SUCCESS;}uint32_t n=std::min<uint32_t>(*count,es.size());std::copy_n(es.data(),n,out);*count=n;return n<es.size()?VK_INCOMPLETE:VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* alloc,VkInstance* out){
    auto* chain=reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
    while(chain&&(chain->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO||chain->function!=VK_LAYER_LINK_INFO))chain=reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain->pNext));
    if(!chain)return VK_ERROR_INITIALIZATION_FAILED;
    auto gipa=chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
    chain->u.pLayerInfo=chain->u.pLayerInfo->pNext;
    auto result=create(info,alloc,out);
    if(result==VK_SUCCESS){std::lock_guard lock(mutex);instances[key(*out)]={*out,gipa};__android_log_print(ANDROID_LOG_INFO,"AXRB.SystemVulkan","Runtime layer active");}
    return result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance h,const VkAllocationCallbacks* alloc){
    auto s=instance(h);{std::lock_guard lock(mutex);instances.erase(key(h));}function<PFN_vkDestroyInstance>(s,"vkDestroyInstance")(h,alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* alloc,VkDevice* out){
    auto s=instance(physical);
    auto* chain=reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
    while(chain&&(chain->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO||chain->function!=VK_LAYER_LINK_INFO))chain=reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain->pNext));
    if(!chain)return VK_ERROR_INITIALIZATION_FAILED;
    auto gdpa=chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    auto create=reinterpret_cast<PFN_vkCreateDevice>(chain->u.pLayerInfo->pfnNextGetInstanceProcAddr(s.handle,"vkCreateDevice"));
    chain->u.pLayerInfo=chain->u.pLayerInfo->pNext;
    std::vector<VkExtensionProperties> es;auto r=extensions(s,physical,es);if(r!=VK_SUCCESS)return r;
    auto api=apiVersion(s,physical);std::vector<const char*> names;
    for(uint32_t i=0;i<info->enabledExtensionCount;++i){auto n=info->ppEnabledExtensionNames[i];bool promoted=false;for(auto& p:promotions)if(api>=p.api&&!std::strcmp(n,p.name)&&!has(es,n))promoted=true;if(!promoted)names.push_back(n);}
    auto modified=*info;modified.enabledExtensionCount=names.size();modified.ppEnabledExtensionNames=names.data();
    auto result=create(physical,&modified,alloc,out);
    if(result==VK_SUCCESS){std::lock_guard lock(mutex);devices[key(*out)]={*out,gdpa};}
    return result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice h,const VkAllocationCallbacks* alloc){
    auto s=device(h);{std::lock_guard lock(mutex);devices.erase(key(h));for(auto it=templates.begin();it!=templates.end();)if(it->first.first==h)it=templates.erase(it);else ++it;}
    function<PFN_vkDestroyDevice>(s,"vkDestroyDevice")(h,alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice h,const VkDescriptorUpdateTemplateCreateInfo* info,const VkAllocationCallbacks* alloc,VkDescriptorUpdateTemplate* out){
    auto s=device(h);std::shared_ptr<const Entries> entries;
    if(info->templateType==VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET && std::all_of(info->pDescriptorUpdateEntries,info->pDescriptorUpdateEntries+info->descriptorUpdateEntryCount,[](auto& e){return axrb::descriptor_template_supported(e.descriptorType);}))entries=std::make_shared<const Entries>(info->pDescriptorUpdateEntries,info->pDescriptorUpdateEntries+info->descriptorUpdateEntryCount);
    auto create=function<PFN_vkCreateDescriptorUpdateTemplate>(s,"vkCreateDescriptorUpdateTemplate");
    if(!create)create=function<PFN_vkCreateDescriptorUpdateTemplate>(s,"vkCreateDescriptorUpdateTemplateKHR");
    if(!create)return VK_ERROR_EXTENSION_NOT_PRESENT;
    auto result=create(h,info,alloc,out);
    if(result==VK_SUCCESS&&entries){std::lock_guard lock(mutex);templates[{h,*out}]=std::move(entries);}
    return result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice h,VkDescriptorUpdateTemplate value,const VkAllocationCallbacks* alloc){
    auto s=device(h);{std::lock_guard lock(mutex);templates.erase({h,value});}
    auto destroy=function<PFN_vkDestroyDescriptorUpdateTemplate>(s,"vkDestroyDescriptorUpdateTemplate");
    if(!destroy)destroy=function<PFN_vkDestroyDescriptorUpdateTemplate>(s,"vkDestroyDescriptorUpdateTemplateKHR");
    destroy(h,value,alloc);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice h,VkDescriptorSet set,VkDescriptorUpdateTemplate value,const void* data){
    auto s=device(h);std::shared_ptr<const Entries> entries;
    {std::lock_guard lock(mutex);auto it=templates.find({h,value});if(it!=templates.end())entries=it->second;}
    if(entries){axrb::ExpandedDescriptorTemplate expanded(*entries,set,data);function<PFN_vkUpdateDescriptorSets>(s,"vkUpdateDescriptorSets")(h,expanded.writes.size(),expanded.writes.data(),0,nullptr);return;}
    auto update=function<PFN_vkUpdateDescriptorSetWithTemplate>(s,"vkUpdateDescriptorSetWithTemplate");
    if(!update)update=function<PFN_vkUpdateDescriptorSetWithTemplate>(s,"vkUpdateDescriptorSetWithTemplateKHR");
    update(h,set,value,data);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplateKHR(VkDevice h,const VkDescriptorUpdateTemplateCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorUpdateTemplate* o){return vkCreateDescriptorUpdateTemplate(h,i,a,o);}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplateKHR(VkDevice h,VkDescriptorUpdateTemplate t,const VkAllocationCallbacks* a){vkDestroyDescriptorUpdateTemplate(h,t,a);}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateKHR(VkDevice h,VkDescriptorSet s,VkDescriptorUpdateTemplate t,const void* d){vkUpdateDescriptorSetWithTemplate(h,s,t,d);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance h,const char* name){
    if(auto f=intercept(name))return f;
    // Extension entry points can be available when the corresponding core
    // name is gated by the application's requested Vulkan API version.
    if(!h)return nullptr;auto s=instance(h);auto f=s.gipa(h,name);
    if(!f)f=s.gipa(h,alias(name));
    return f;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice h,const char* name){
    if(auto f=intercept(name))return f;
    if(!h)return nullptr;auto s=device(h);auto f=s.gdpa(h,name);
    if(!f)f=s.gdpa(h,alias(name));
    return f;
}
}
namespace {
PFN_vkVoidFunction intercept(const char* name){
#define ENTRY(n) if(!std::strcmp(name,#n))return reinterpret_cast<PFN_vkVoidFunction>(n)
    ENTRY(vkGetInstanceProcAddr);ENTRY(vkGetDeviceProcAddr);
    ENTRY(vkCreateInstance);ENTRY(vkDestroyInstance);ENTRY(vkCreateDevice);ENTRY(vkDestroyDevice);
    ENTRY(vkEnumerateInstanceLayerProperties);ENTRY(vkEnumerateDeviceLayerProperties);ENTRY(vkEnumerateInstanceExtensionProperties);
    ENTRY(vkEnumerateDeviceExtensionProperties);
    ENTRY(vkSetDebugUtilsObjectNameEXT);
    ENTRY(vkCreateDescriptorUpdateTemplate);ENTRY(vkDestroyDescriptorUpdateTemplate);ENTRY(vkUpdateDescriptorSetWithTemplate);
    ENTRY(vkCreateDescriptorUpdateTemplateKHR);ENTRY(vkDestroyDescriptorUpdateTemplateKHR);ENTRY(vkUpdateDescriptorSetWithTemplateKHR);
#undef ENTRY
    return nullptr;
}
}
