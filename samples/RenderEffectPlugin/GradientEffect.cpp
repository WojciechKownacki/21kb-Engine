#include "kb/render/effects/RenderEffectApi.hpp"
#include <array>
#include <new>

using namespace kb::render::effects;
namespace {
struct Effect {
    const HostApi* api=nullptr;
    Handle program=0U,input=0U,output=0U,image=0U,params=0U,readback=0U;
    std::uint32_t width=0U,height=0U;
    bool verified=false;
    void Drop(Handle& h) {if(h) {api->release(api->context,h);h=0U;}}
    ~Effect() {Drop(readback);Drop(params);Drop(image);Drop(output);Drop(input);Drop(program);}
    bool Frame(const kb::render::effects::Frame& frame) {
        const std::array<std::uint32_t,4> data{7U,11U,13U,17U};
        if(frame.width!=width||frame.height!=height) {
            Drop(readback);Drop(image);Drop(output);Drop(input);width=frame.width;height=frame.height;
            // A small immutable input demonstrates buffer ownership separately
            // from compute-written output. CPU memory is copied by the host.
            input=api->createBuffer(api->context,BufferKind::InputUint,4U,data.data(),sizeof(data));
            output=api->createBuffer(api->context,BufferKind::StorageUint,4U,nullptr,0U);
            image=api->createTexture(api->context,width,height);
            if(!input||!output||!image) return false;
        }
        if(readback) {
            std::array<std::uint32_t,4> result{};
            const auto ready=api->pollReadback(api->context,readback,result.data(),sizeof(result));
            if(ready<0) return false;
            if(ready==1) {
                verified=result==std::array<std::uint32_t,4>{49U,53U,55U,59U};
                if(!verified) return false;
                Drop(readback);
            }
        }
        if(!api->updateBuffer(api->context,input,data.data(),sizeof(data))) return false;
        const Binding bindings[]{
            {.resource=input,.slot=0U,.kind=BindingKind::Buffer,.access=Access::Read},
            {.resource=output,.slot=1U,.kind=BindingKind::Buffer,.access=Access::Write},
            {.resource=image,.slot=2U,.kind=BindingKind::Image,.access=Access::Write}};
        UniformValue values{.uniform=params,.value={static_cast<float>(width),static_cast<float>(height),0,0}};
        const Dispatch dispatch{.program=program,.bindings=bindings,.bindingCount=3U,.uniforms=&values,.uniformCount=1U,
            .groups={(width+7U)/8U,(height+7U)/8U,1U}};
        if(!api->dispatch(api->context,&dispatch)||!api->present(api->context,image)) return false;
        if(!readback&&!verified) readback=api->readBuffer(api->context,output);
        return verified||readback!=0U;
    }
};
}
#if defined(_WIN32)
#define EFFECT_EXPORT __declspec(dllexport)
#else
#define EFFECT_EXPORT __attribute__((visibility("default")))
#endif
extern "C" EFFECT_EXPORT bool kb_create_render_effect_v1(const HostApi* api,Plugin* output) {
    if(!api||!output||api->size<sizeof(HostApi)||api->version!=ApiVersion) return false;
    auto* effect=new(std::nothrow) Effect;
    if(!effect) return false;
    effect->api=api;
    *output=Plugin{.instance=effect,
        .onFrame=[](void* e,const kb::render::effects::Frame* f){return static_cast<Effect*>(e)->Frame(*f);},
        .onDestroy=[](void* e){delete static_cast<Effect*>(e);},
        .status=[](void* e)->std::uint64_t{return static_cast<Effect*>(e)->verified?1U:0U;}};
    effect->params=api->createUniform(api->context,"u_fx_gradient_size",false);
    if(!effect->params) return false;
    effect->program=api->createProgram(api->context,"gradient");
    return effect->program!=0U;
}
