#include "CliCommands.hpp"
#include "CliCommon.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "kb/render/effects/RenderEffectHost.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include <charconv>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::cli {
int RunRenderEffectRunCommand(const ArgumentList& arguments, CommandIo io) {
    const auto manifest=arguments.Option("--manifest");
    constexpr std::string_view allowed[]{"--manifest","--frames"};
    std::uint32_t frames=8U;
    if(const auto value=arguments.Option("--frames")) {
        const auto [end,ec]=std::from_chars(value->data(),value->data()+value->size(),frames);
        if(ec!=std::errc{}||end!=value->data()+value->size()) frames=0U;
    }
    if(!manifest||frames==0U||frames>1024U||!arguments.Errors().empty()||!arguments.UnknownOptions(allowed).empty()||!arguments.Positionals().empty()) {
        io.err<<"error: render-effect-run requires --manifest <effect.kbfx> [--frames 1..1024]\n";return 1;
    }
#if defined(_WIN32)
    class Surface final:public kb::render::RenderSurface {
    public:
        HWND window=CreateWindowExW(0,L"STATIC",L"compute-effect-headless",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        ~Surface() override {if(window) DestroyWindow(window);}
        std::uint32_t Width() const noexcept override {return 64U;}
        std::uint32_t Height() const noexcept override {return 64U;}
        void* NativeWindowHandle() const noexcept override {return window;}
        void* NativeDisplayHandle() const noexcept override {return nullptr;}
    } surface;
    kb::render::Renderer renderer;
    kb::render::DisplayConfig config{};
    config.preferredBgfxRendererType=static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    config.enableEditorRendering=false;config.syncMode=kb::render::DisplaySyncMode::Uncapped;
    if(!surface.window||!renderer.Initialize(surface,&config)) {io.err<<"error: effect graphics host initialization failed\n";return 1;}
    auto& effects=renderer.RenderEffects();std::string error;
    if(!effects.Load(kb::assets::Utf8Path(*manifest),error)) {io.err<<"error: "<<error<<'\n';return 1;}
    for(std::uint32_t i=0U;i<frames;++i) {
        if(!renderer.BeginFrame()) {io.err<<"error: effect frame start failed\n";return 1;}
        renderer.SubmitClear(0x000000FFU);renderer.EndFrame();
        if(!effects.LastError().empty()) {io.err<<"error: "<<effects.LastError()<<'\n';return 1;}
    }
    const auto stats=effects.Stats();const auto status=effects.PluginStatus(0U);
    effects.Unload();const auto released=effects.Stats();
    io.out<<"{\"schema\":\"21kb.render-effect/v1\",\"frames\":"<<frames
        <<",\"dispatches\":"<<stats.dispatches<<",\"presentations\":"<<stats.presentations
        <<",\"completedReadbacks\":"<<stats.completedReadbacks<<",\"pluginStatus\":"<<status
        <<",\"liveGpuResourcesAfterUnload\":"<<(released.programs+released.buffers+released.textures+released.uniforms)<<"}\n";
    return 0;
#else
    io.err<<"error: render-effect-run needs a configured native graphics surface on this platform\n";return 1;
#endif
}
int RunRenderEffectExportCommand(const ArgumentList& arguments, CommandIo io) {
    const auto manifest=arguments.Option("--manifest"),output=arguments.Option("--output");
    constexpr std::string_view allowed[]{"--manifest","--output"};
    if(!manifest||!output||!arguments.Errors().empty()||!arguments.UnknownOptions(allowed).empty()||!arguments.Positionals().empty()) {
        io.err<<"error: render-effect-export requires --manifest <effect.kbfx> --output <new-directory>\n";return 1;
    }
    std::string error;
    if(!kb::render::ExportRenderEffect(kb::assets::Utf8Path(*manifest),kb::assets::Utf8Path(*output),error)) {
        io.err<<"error: "<<error<<'\n';return 1;
    }
    io.out<<"Exported render effect with native module and compiled shader variants\n";return 0;
}
} // namespace kb::cli
