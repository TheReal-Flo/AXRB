// Read-only, game-independent report of the memory budget exposed to clients.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <vector>
int main(){
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
    VkInstance instance;auto result=vkCreateInstance(&ci,nullptr,&instance);if(result)return 1;
    uint32_t count=0;vkEnumeratePhysicalDevices(instance,&count,nullptr);std::vector<VkPhysicalDevice> devices(count);vkEnumeratePhysicalDevices(instance,&count,devices.data());
    for(auto gpu:devices){
        VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(gpu,&props);
        uint32_t n=0;vkEnumerateDeviceExtensionProperties(gpu,nullptr,&n,nullptr);std::vector<VkExtensionProperties> extensions(n);vkEnumerateDeviceExtensionProperties(gpu,nullptr,&n,extensions.data());
        bool budget=false;for(auto& ext:extensions)if(!std::strcmp(ext.extensionName,VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))budget=true;
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budgets{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 memory{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};if(budget)memory.pNext=&budgets;
        vkGetPhysicalDeviceMemoryProperties2(gpu,&memory);
        std::printf("device=%s vendor=%x type=%u memory_budget=%d max_image=%u\n",props.deviceName,props.vendorID,props.deviceType,budget,props.limits.maxImageDimension2D);
        for(uint32_t i=0;i<memory.memoryProperties.memoryHeapCount;++i)std::printf("heap=%u flags=%x size_MiB=%llu budget_MiB=%llu usage_MiB=%llu\n",i,memory.memoryProperties.memoryHeaps[i].flags,(unsigned long long)(memory.memoryProperties.memoryHeaps[i].size>>20),(unsigned long long)(budgets.heapBudget[i]>>20),(unsigned long long)(budgets.heapUsage[i]>>20));
        for(uint32_t i=0;i<memory.memoryProperties.memoryTypeCount;++i)std::printf("type=%u heap=%u flags=%x\n",i,memory.memoryProperties.memoryTypes[i].heapIndex,memory.memoryProperties.memoryTypes[i].propertyFlags);
    }
    vkDestroyInstance(instance,nullptr);return 0;
}
