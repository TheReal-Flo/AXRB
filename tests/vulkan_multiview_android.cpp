// Compare native x86_64 and translated ARM64 Vulkan struct forwarding.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <vector>
#ifdef AXRB_JNI
#include <jni.h>
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"AXRB.Multiview",__VA_ARGS__)
#else
#define LOG(...) std::printf(__VA_ARGS__)
#endif
#define CHECK(call) do{VkResult r=(call);if(r!=VK_SUCCESS){LOG("FAIL %s %d line %d\n",#call,r,__LINE__);return 1;}}while(0)
int probe(){
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="AXRB multiview ABI probe";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;VkInstance instance;CHECK(vkCreateInstance(&ci,nullptr,&instance));
    uint32_t n=1;VkPhysicalDevice physical;CHECK(vkEnumeratePhysicalDevices(instance,&n,&physical));
    VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
    VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f.pNext=&mv;vkGetPhysicalDeviceFeatures2(physical,&f);
    LOG("multiview=%u returned sType=%u/%u\n",mv.multiview,f.sType,mv.sType);if(!mv.multiview)return 2;
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families.data());uint32_t family=0;while(!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
    mv.multiviewGeometryShader=mv.multiviewTessellationShader=VK_FALSE;
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.pNext=&mv;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;VkDevice device;CHECK(vkCreateDevice(physical,&di,nullptr,&device));
    VkAttachmentDescription attachments[3]{};for(auto& a:attachments){a.format=VK_FORMAT_R8G8B8A8_UNORM;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;}
    VkAttachmentReference ref[2]={{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription subpasses[2]{};for(int i=0;i<2;++i){subpasses[i].pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpasses[i].colorAttachmentCount=1;subpasses[i].pColorAttachments=&ref[i];}
    uint32_t masks[2]={3,3};VkRenderPassMultiviewCreateInfo multi{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};multi.subpassCount=2;multi.pViewMasks=masks;
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ri.pNext=&multi;ri.attachmentCount=3;ri.pAttachments=attachments;ri.subpassCount=2;ri.pSubpasses=subpasses;
    LOG("CreateRenderPass attachments=[37,37,37], viewMasks=[3,3], multiview enabled\n");
    VkRenderPass pass;CHECK(vkCreateRenderPass(device,&ri,nullptr,&pass));vkDestroyRenderPass(device,pass,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);LOG("PASS multiview renderpass creation\n");return 0;
}
#ifdef AXRB_JNI
extern "C" JNIEXPORT jstring JNICALL Java_com_axrb_mathprobe_MainActivity_run(JNIEnv* env,jclass,jint){int r=probe();return env->NewStringUTF(r?"FAIL Vulkan multiview probe":"PASS Vulkan multiview probe");}
#else
int main(){return probe();}
#endif
