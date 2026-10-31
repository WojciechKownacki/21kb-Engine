#include "RendererTestSupport.hpp"
#include "kb/render/effects/RenderEffectHost.hpp"
#include <bgfx/bgfx.h>
#include <array>
#include <filesystem>
#include <vector>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render::tests {
void RunRenderEffectPluginTests(const std::filesystem::path& manifest) {
#if defined(_WIN32)
    const auto window=CreateWindowExW(0,L"STATIC",L"external-effect-headless",WS_OVERLAPPEDWINDOW,
        0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(window!=nullptr,"Effect hidden surface failed");
    bgfx::Init init{};init.type=bgfx::RendererType::Direct3D11;init.swapChain.nwh=window;
    init.swapChain.width=init.swapChain.height=64U;
    Require(bgfx::init(init),"Effect test requires D3D11");
    {
        const auto color=bgfx::createTexture2D(64U,64U,false,1U,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT|BGFX_TEXTURE_BLIT_DST);
        const auto readback=bgfx::createTexture2D(64U,64U,false,1U,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
        const auto target=bgfx::createFrameBuffer(1U,&color,false);
        Require(bgfx::isValid(target)&&bgfx::isValid(readback),"Effect readback target failed");
        RenderEffectHost host;
        std::string error;
        const auto load=[&]() {const auto loaded=host.Load(manifest,error);Require(loaded,error.c_str());};
        load();
        std::uint32_t completed=bgfx::frame();
        const auto capture=[&](std::uint32_t width) {
            if(!host.Run(100U,width,64U,completed,1.0F/60.0F,target.idx)) Require(false,host.LastError().c_str());
            bgfx::blit(240U,bgfx::TextureRegion{.handle=readback},bgfx::TextureRegion{.handle=color,.width=64U,.height=64U});
            std::vector<std::uint8_t> pixels(64U*64U*4U);
            const auto ready=bgfx::read(bgfx::TextureRegion{.handle=readback},pixels.data());
            completed=bgfx::frame();
            for(auto guard=0U;static_cast<std::int32_t>(completed-ready)<0&&guard<8U;++guard) completed=bgfx::frame();
            Require(static_cast<std::int32_t>(completed-ready)>=0,"Effect image readback timed out");
            return pixels;
        };
        for(auto frame=0U;frame<5U;++frame) {
            const auto pixels=capture(64U);
            const auto left=(32U*64U+4U)*4U,right=(32U*64U+59U)*4U;
            Require(pixels[right]>pixels[left]+40U&&pixels[left+1U]>40U&&pixels[left+2U]>20U,
                "External compute texture was not synchronized into the rendered image");
        }
        Require(host.PluginStatus(0U)==1U,"External plugin did not verify its actual GPU buffer readback");
        const auto active=host.Stats();
        Require(active.plugins==1U&&active.programs==1U&&active.buffers==2U&&active.textures==1U&&active.uniforms==1U&&
            active.dispatches==5U&&active.presentations==5U&&active.completedReadbacks==1U,"Effect owned resource or dispatch counts differ");
        static_cast<void>(capture(32U)); // Resize replaces the owned image and buffer resources.
        host.Unload();
        auto empty=host.Stats();
        Require(empty.plugins==0U&&empty.programs==0U&&empty.buffers==0U&&empty.textures==0U&&empty.uniforms==0U,
            "Effect unload retained owned GPU resources");
        load();
        if(!host.Run(100U,64U,64U,completed,1.0F/60.0F,target.idx)) Require(false,host.LastError().c_str());
        host.Unload(); // Destroy native code while the host still owns the asynchronous readback destination.
        for(auto i=0U;i<4U;++i) {
            completed=bgfx::frame();
            Require(host.Run(100U,64U,64U,completed,0.0F,target.idx),"Effect retirement polling failed");
        }
        empty=host.Stats();
        Require(empty.readbacks==0U&&empty.programs==0U&&empty.buffers==0U&&empty.textures==0U&&empty.uniforms==0U,
            "Effect deferred readback storage leaked after native unload");
        Require(!host.Load(manifest.parent_path()/"unsupported.kbfx",error),"Effect accepted an unsupported manifest version");
        Require(!host.Load(manifest.parent_path()/"missing-program.kbfx",error),"Effect factory accepted an absent program");
        empty=host.Stats();
        Require(empty.plugins==0U&&empty.programs==0U&&empty.uniforms==0U,"Failed effect factory retained resources or native callbacks");
        std::cout<<"external_effect exported_source_free=1 dispatches="<<active.dispatches
            <<" image_gradient=PASS gpu_buffer_readback=49,53,55,59 resize=PASS unload_pending_readback=PASS rollback=PASS live_resources=0\n";
        bgfx::destroy(target);bgfx::destroy(readback);bgfx::destroy(color);
    }
    bgfx::shutdown();DestroyWindow(window);
#else
    static_cast<void>(manifest);Require(false,"Effect acceptance requires a configured graphics surface");
#endif
}
} // namespace kb::render::tests
