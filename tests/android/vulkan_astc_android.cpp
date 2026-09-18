// Standalone native x86_64 Android/Gfxstream ASTC upload regression probe.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(call) do { VkResult r=(call); if(r!=VK_SUCCESS){std::fprintf(stderr,"%s: %d at %d\n",#call,r,__LINE__);return 1;} }while(0)
int main(int argc, char** argv) {
    setbuf(stdout,nullptr);
    std::vector<unsigned char> encoded;
    if(argc>1){
        FILE* f=std::fopen(argv[1],"rb");if(!f)return 3;
        unsigned char header[16];if(std::fread(header,1,16,f)!=16||std::memcmp(header,"\x13\xab\xa1\x5c\x06\x06\x01",7)){std::fclose(f);return 3;}
        std::fseek(f,0,SEEK_END);long size=std::ftell(f)-16;std::fseek(f,16,SEEK_SET);
        if(size<=0||size%16){std::fclose(f);return 3;}encoded.resize(size);
        if(std::fread(encoded.data(),1,size,f)!=static_cast<size_t>(size)){std::fclose(f);return 3;}std::fclose(f);
        std::printf("Using %zu encoded ASTC blocks\n",encoded.size()/16);
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName="AXRB ASTC isolation";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
    VkInstance instance;CHECK(vkCreateInstance(&ci,nullptr,&instance));
    uint32_t n=1;VkPhysicalDevice physical;CHECK(vkEnumeratePhysicalDevices(instance,&n,&physical));
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);
    std::printf("Device: %s\n",props.deviceName);if(props.vendorID!=0x10de)return 2;
    VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(physical,&mp);
    auto memoryType=[&](uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<mp.memoryTypeCount;++i)if((bits&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&flags)==flags)return i;std::abort();};
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);
    std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families.data());
    uint32_t family=0;while(!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkPhysicalDeviceFeatures features{};features.textureCompressionASTC_LDR=VK_TRUE;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.pEnabledFeatures=&features;
    VkDevice device;CHECK(vkCreateDevice(physical,&di,nullptr,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;CHECK(vkCreateCommandPool(device,&pi,nullptr,&pool));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    VkCommandBuffer cmd;CHECK(vkAllocateCommandBuffers(device,&ai,&cmd));
    for(bool volume:{false,true})for(bool mips:{false,true}) {
        uint32_t width=volume?96:2048,height=volume?66:2048,depth=volume?35:1,levels=mips?7:1;
        std::printf("ASTC 6x6 %s %ux%ux%u mipLevels=%u\n",volume?"3D":"2D",width,height,depth,levels);
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=volume?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D;
        ii.format=volume?VK_FORMAT_ASTC_6x6_UNORM_BLOCK:VK_FORMAT_ASTC_6x6_SRGB_BLOCK;ii.extent={width,height,depth};ii.mipLevels=levels;ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;ii.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImage image;CHECK(vkCreateImage(device,&ii,nullptr,&image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,image,&req);
        std::printf("Image memory requirement: %llu bytes\n", static_cast<unsigned long long>(req.size));
        VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mi.allocationSize=req.size;mi.memoryTypeIndex=memoryType(req.memoryTypeBits,0);
        VkDeviceMemory imageMem;CHECK(vkAllocateMemory(device,&mi,nullptr,&imageMem));CHECK(vkBindImageMemory(device,image,imageMem,0));
        std::vector<VkBufferImageCopy> copies;VkDeviceSize bytes=0;
        for(uint32_t mip=0;mip<levels;++mip){VkBufferImageCopy c{};c.bufferOffset=bytes;c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,0,1};c.imageExtent={std::max(1u,width>>mip),std::max(1u,height>>mip),std::max(1u,depth>>mip)};bytes+=((c.imageExtent.width+5)/6)*((c.imageExtent.height+5)/6)*c.imageExtent.depth*16;copies.push_back(c);}
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VkBuffer buffer;CHECK(vkCreateBuffer(device,&bi,nullptr,&buffer));vkGetBufferMemoryRequirements(device,buffer,&req);mi.allocationSize=req.size;mi.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory bufferMem;CHECK(vkAllocateMemory(device,&mi,nullptr,&bufferMem));CHECK(vkBindBufferMemory(device,buffer,bufferMem,0));
        void* mapped;CHECK(vkMapMemory(device,bufferMem,0,bytes,0,&mapped));
        const unsigned char block[16]={0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0,0,0,0,0xff,0xff};
        for(size_t i=0;i<bytes;i+=16)std::memcpy(static_cast<char*>(mapped)+i,encoded.empty()?block:encoded.data()+i%encoded.size(),16);vkUnmapMemory(device,bufferMem);
        CHECK(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,1};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        vkCmdCopyBufferToImage(cmd,buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,copies.size(),copies.data());
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        CHECK(vkEndCommandBuffer(cmd));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));std::puts("PASS upload/decompression queue completed");
        vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,bufferMem,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,imageMem,nullptr);
    }
    vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);return 0;
}
