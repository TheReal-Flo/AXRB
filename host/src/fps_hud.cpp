#include "openxr_session.h"
#if defined(_WIN32)
namespace axrb::host::detail {
namespace {
constexpr uint32_t width=256, height=64;
std::vector<uint8_t> hud_pixels(double fps) {
    // Small monochrome bitmap font: no font installation, GPU shaders or per-frame text work.
    static constexpr uint8_t digits[10][7]={
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}};
    static constexpr uint8_t f[7]={31,16,16,30,16,16,16}, p[7]={30,17,17,30,16,16,16},
        s[7]={15,16,16,14,1,1,30}, dot[7]={0,0,0,0,0,12,12}, blank[7]={};
    char text[32]; std::snprintf(text,sizeof(text),"FPS %.1f",(std::min)(fps,9999.9));
    std::vector<uint8_t> pixels(width*height*4,0);
    for(size_t i=3;i<pixels.size();i+=4)pixels[i]=255;
    int left=(int(width)-int(std::strlen(text))*18)/2;
    for(size_t n=0;text[n];++n){
        char c=text[n];const uint8_t* glyph=c>='0'&&c<='9'?digits[c-'0']:c=='F'?f:c=='P'?p:c=='S'?s:c=='.'?dot:blank;
        for(int y=0;y<7;++y)for(int x=0;x<5;++x)if(glyph[y]&(1<<(4-x)))
            for(int dy=0;dy<3;++dy)for(int dx=0;dx<3;++dx){
                int px=left+int(n)*18+x*3+dx,py=21+y*3+dy;
                if(px<0||px>=int(width))continue;
                size_t at=(py*width+px)*4;
                pixels[at]=pixels[at+1]=pixels[at+2]=255;
            }
    }
    return pixels; // black/white are identical in RGBA/BGRA and linear/sRGB.
}
}
bool OpenXrSession::update_fps_hud(XrCompositionLayerQuad& layer,uint32_t existingLayers) {
    if(!fpsHudInitialized_){
        fpsHudInitialized_=true;
        wchar_t name[128]{};
        auto length=GetEnvironmentVariableW(L"AXRB_FPS_HUD_EVENT",name,128);
        if(length&&length<128)fpsHudEvent_=OpenEventW(SYNCHRONIZE,FALSE,name);
    }
    if(!fpsHudEvent_||fpsHudFailed_)return false;
    const auto count=imageFrame_?imageFrame_->deliveredFrames.load(std::memory_order_relaxed):0;
    const bool changed=fpsCounter_.sample(count,axrb::host::FpsCounter::Clock::now());
    if(changed){++fpsHudGeneration_;fpsHudPixels_.clear();}
    if(WaitForSingleObject(fpsHudEvent_,0)!=WAIT_OBJECT_0)return false;
    if(fpsHudSwapchain_==XR_NULL_HANDLE){
        PFN_xrGetSystemProperties properties=nullptr;
        XrSystemProperties system{XR_TYPE_SYSTEM_PROPERTIES};
        if(!load_func("xrGetSystemProperties",&properties)||properties(instance_,systemId_,&system)!=XR_SUCCESS){fpsHudFailed_=true;return false;}
        fpsHudMaxLayers_=system.graphicsProperties.maxLayerCount;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=projectionFormat_;info.sampleCount=1;info.width=width;info.height=height;
        info.faceCount=info.arraySize=info.mipCount=1;
        if(createSwapchain_(session_,&info,&fpsHudSwapchain_)!=XR_SUCCESS){fpsHudFailed_=true;return false;}
        uint32_t count=0;
        if(enumerateSwapchainImages_(fpsHudSwapchain_,0,&count,nullptr)!=XR_SUCCESS||!count){fpsHudFailed_=true;return false;}
        fpsHudImages_.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});fpsHudUploaded_.assign(count,0);
        if(enumerateSwapchainImages_(fpsHudSwapchain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(fpsHudImages_.data()))!=XR_SUCCESS){fpsHudFailed_=true;return false;}
        std::fprintf(stderr,"AXRB FPS HUD: complete received game frames, 0.5 second sampling\n");
    }
    if(existingLayers>=fpsHudMaxLayers_)return false; // Never displace a game layer.
    uint32_t index=0;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(acquireSwapchainImage_(fpsHudSwapchain_,&acquire,&index)!=XR_SUCCESS)return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    if(waitSwapchainImage_(fpsHudSwapchain_,&wait)!=XR_SUCCESS){fpsHudFailed_=true;return false;}
    if(index>=fpsHudImages_.size()){fpsHudFailed_=true;return false;}
    if(fpsHudUploaded_[index]!=fpsHudGeneration_){
        if(fpsHudPixels_.empty())fpsHudPixels_=hud_pixels(fpsCounter_.fps());
        d3dContext_.get()->UpdateSubresource(fpsHudImages_[index].texture,0,nullptr,fpsHudPixels_.data(),width*4,width*height*4);
        fpsHudUploaded_[index]=fpsHudGeneration_;
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(releaseSwapchainImage_(fpsHudSwapchain_,&release)!=XR_SUCCESS){fpsHudFailed_=true;return false;}
    layer={XR_TYPE_COMPOSITION_LAYER_QUAD};layer.space=viewSpace_;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    layer.pose.orientation.w=1;layer.pose.position={0.34f,-0.24f,-1.5f};layer.size={0.32f,0.08f};
    layer.subImage.swapchain=fpsHudSwapchain_;layer.subImage.imageRect.extent={width,height};
    return true;
}
}
#endif
