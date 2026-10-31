#include "kb/render/effects/RenderEffectHost.hpp"
#include "kb/render/ShaderManifest.hpp"
#include "kb/render/frame/FinalCompositePass.hpp"
#include "engine/modules/EngineModuleLoader.hpp"
#include "engine/assets/AssetFileName.hpp"
#include <bgfx/bgfx.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <thread>
#include <vector>
#include <stdexcept>

namespace kb::render {
namespace {
using namespace effects;
struct ShaderFile { std::string program, profile; std::filesystem::path path; };
struct Manifest { std::filesystem::path root, library; std::vector<ShaderFile> shaders; };
bool Ready(std::uint32_t now, std::uint32_t ready) { return static_cast<std::int32_t>(now-ready)>=0; }
bool Identifier(std::string_view s) {
    return !s.empty()&&s.size()<=128U&&std::all_of(s.begin(),s.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';
    });
}
std::filesystem::path Contained(const std::filesystem::path& root, const std::filesystem::path& relative) {
    if(relative.empty()||relative.is_absolute()||relative.has_root_name()) throw std::runtime_error("Effect paths must be relative");
    for(const auto& part:relative) if(part=="..") throw std::runtime_error("Effect path escapes the bundle");
    const auto path=std::filesystem::canonical(root/relative);
    auto a=root.begin(),b=path.begin();
    for(;a!=root.end();++a,++b) if(b==path.end()||*a!=*b) throw std::runtime_error("Effect symlink escapes the bundle");
    if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("Effect resource is not a file");
    return path;
}
std::string PathUtf8(const std::filesystem::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::vector<std::uint8_t> Bytes(const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);
    if(size==0U||size>64U*1024U*1024U) throw std::runtime_error("Effect file size is invalid");
    std::ifstream input(path,std::ios::binary);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if(!input.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()))) throw std::runtime_error("Effect file read failed");
    return data;
}
Manifest ReadManifest(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>65536U) throw std::runtime_error("Effect manifest is too large");
    Manifest m; m.root=std::filesystem::canonical(path.parent_path().empty()?".":path.parent_path());
    std::ifstream input(path);std::string magic,kind;unsigned version=0U;
    if(!(input>>magic>>version)||magic!="21kb-render-effect"||version!=1U) throw std::runtime_error("Unsupported effect manifest version");
    std::map<std::pair<std::string,std::string>,bool> seen;
    while(input>>kind) {
        std::string filename;
        if(kind=="library") {
            if(!m.library.empty()||!(input>>std::quoted(filename))) throw std::runtime_error("Invalid effect library entry");
            m.library=Contained(m.root,kb::assets::Utf8Path(filename));
        } else if(kind=="shader") {
            ShaderFile s;
            if(!(input>>std::quoted(s.program)>>std::quoted(s.profile)>>std::quoted(filename))||
                !Identifier(s.program)||!Identifier(s.profile)||seen.contains({s.program,s.profile})||m.shaders.size()>=128U)
                throw std::runtime_error("Invalid effect shader entry");
            static constexpr const char* profiles[]{"dxbc","dxil","spirv","glsl","essl","metal","wgsl"};
            if(std::find(std::begin(profiles),std::end(profiles),s.profile)==std::end(profiles)) throw std::runtime_error("Unknown effect shader profile");
            seen[{s.program,s.profile}]=true;s.path=Contained(m.root,kb::assets::Utf8Path(filename));
            m.shaders.push_back(std::move(s));
        } else throw std::runtime_error("Unknown effect manifest entry");
    }
    if(!input.eof()||m.library.empty()||m.shaders.empty()) throw std::runtime_error("Incomplete effect manifest");
    return m;
}
}

bool ExportRenderEffect(const std::filesystem::path& manifest, const std::filesystem::path& output, std::string& error) {
    std::filesystem::path temporary;bool created=false;
    try {
        error.clear();const auto m=ReadManifest(manifest);
        if(output.empty()||std::filesystem::exists(output)) throw std::runtime_error("Effect export destination must not exist");
        temporary=output;temporary+=".staging";
        if(std::filesystem::exists(temporary)) throw std::runtime_error("Effect export staging destination already exists");
        if(!temporary.parent_path().empty()) std::filesystem::create_directories(temporary.parent_path());
        if(!std::filesystem::create_directory(temporary)) throw std::runtime_error("Effect export staging destination already exists");
        created=true;
        const auto libraryName=m.library.filename();
        std::filesystem::copy_file(m.library,temporary/libraryName);
        std::ofstream out(temporary/"effect.kbfx",std::ios::binary);
        out<<"21kb-render-effect 1\nlibrary "<<std::quoted(PathUtf8(libraryName))<<'\n';
        std::uint64_t total=0U;
        for(const auto& shader:m.shaders) {
            const auto data=Bytes(shader.path);total+=data.size();
            if(total>256U*1024U*1024U) throw std::runtime_error("Effect shader bundle is too large");
            const auto name=std::filesystem::path("shaders")/shader.profile/(shader.program+".bin");
            std::filesystem::create_directories((temporary/name).parent_path());
            std::ofstream file(temporary/name,std::ios::binary);
            if(!file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()))) throw std::runtime_error("Effect shader export failed");
            out<<"shader "<<std::quoted(shader.program)<<' '<<std::quoted(shader.profile)<<' '<<std::quoted(PathUtf8(name))<<'\n';
        }
        out.close();if(!out) throw std::runtime_error("Effect manifest export failed");
        static_cast<void>(ReadManifest(temporary/"effect.kbfx"));
        if(std::filesystem::exists(output)) throw std::runtime_error("Effect export destination appeared during export");
        std::filesystem::rename(temporary,output);return true;
    } catch(const std::exception& e) {
        error=e.what();
        if(created) {std::error_code ignored;std::filesystem::remove_all(temporary,ignored);}
        return false;
    }
}

struct RenderEffectHost::Impl {
    enum class Kind { Program, Buffer, Texture, Uniform, Readback, BorrowedTexture };
    struct Resource {
        Kind kind=Kind::Buffer;std::uint64_t owner=0U;
        bgfx::ProgramHandle program=BGFX_INVALID_HANDLE;
        bgfx::DynamicIndexBufferHandle indices=BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle vertices=BGFX_INVALID_HANDLE;
        bgfx::TextureHandle texture=BGFX_INVALID_HANDLE;
        bgfx::UniformHandle uniform=BGFX_INVALID_HANDLE;
        std::vector<std::uint8_t> data;
        std::uint32_t bytes=0U,ready=0U,lastReadFrame=0U;bool input=false,sampler=false,polled=false,hasBeenRead=false;
        ~Resource() { Destroy(); }
        void Destroy() {
            if(bgfx::isValid(program)) bgfx::destroy(program);
            if(bgfx::isValid(indices)) bgfx::destroy(indices);
            if(bgfx::isValid(vertices)) bgfx::destroy(vertices);
            if(bgfx::isValid(texture)&&kind!=Kind::BorrowedTexture) bgfx::destroy(texture);
            if(bgfx::isValid(uniform)) bgfx::destroy(uniform);
            program=BGFX_INVALID_HANDLE;indices=BGFX_INVALID_HANDLE;vertices=BGFX_INVALID_HANDLE;
            texture=BGFX_INVALID_HANDLE;uniform=BGFX_INVALID_HANDLE;
        }
    };
    struct Slot {
        Impl* host=nullptr;std::uint64_t owner=0U;
        HostApi api{};Plugin plugin{};Manifest manifest;
        kb::modules::EngineModuleLibrary library;
        Frame frame{};std::uint16_t view=0U,framebuffer=UINT16_MAX;bool executing=false;
    };
    std::map<Handle,std::unique_ptr<Resource>> resources;
    std::vector<std::unique_ptr<Resource>> retired;
    std::vector<std::unique_ptr<Slot>> slots;
    std::thread::id thread=std::this_thread::get_id();
    kb::modules::EngineModuleLoader loader;
    FinalCompositePass presentPass;
    RenderEffectStats totals{};
    std::string error;
    Handle next=1U;std::uint64_t nextOwner=1U;std::uint32_t completed=0U;bool running=false;
    bool OnThread() { if(thread!=std::this_thread::get_id()) {error="Render effects require the renderer thread";return false;}return true; }
    template<class R,class F> static R Guard(void* context,R failure,F&& f) noexcept {
        auto& s=*static_cast<Slot*>(context);
        try { if(!s.host->OnThread()) return failure;s.host->error.clear();return f(s); }
        catch(const std::exception& e) {s.host->error=e.what();return failure;}
        catch(...) {s.host->error="Render effect API failed";return failure;}
    }
    Resource& Get(Slot& s,Handle h) {
        const auto it=resources.find(h);
        if(it==resources.end()||it->second->owner!=s.owner) throw std::runtime_error("Invalid or stale effect resource ownership");
        return *it->second;
    }
    Handle Add(Slot& s,std::unique_ptr<Resource> r) {
        if(resources.size()>=4096U||next==0U) {r->Destroy();throw std::runtime_error("Effect resource budget exceeded");}
        r->owner=s.owner;const auto handle=next++;resources.emplace(handle,std::move(r));return handle;
    }
    void Collect() {
        std::erase_if(retired,[&](auto& r){if(!Ready(completed,r->ready)) return false;r->Destroy();return true;});
    }
    bool Release(Slot& s,Handle h) {
        auto& r=Get(s,h);
        if(r.kind==Kind::BorrowedTexture) throw std::runtime_error("Borrowed scene textures cannot be released");
        if(r.kind==Kind::Readback&&!Ready(completed,r.ready)) retired.push_back(std::move(resources.at(h)));
        else r.Destroy();
        resources.erase(h);return true;
    }
    HostApi Api(Slot& slot) {
        HostApi a;a.context=&slot;
        a.lastError=[](void* c){return static_cast<Slot*>(c)->host->error.c_str();};
        a.createProgram=[](void* c,const char* name)->Handle {return Guard<Handle>(c,0U,[&](Slot& s){
            if(name==nullptr||!Identifier(name)) throw std::runtime_error("Invalid effect program name");
            const auto directory=ShaderProfileDirectoryForRenderer(bgfx::getRendererType());
            const auto profile=directory!=nullptr?std::filesystem::path(directory).filename().string():std::string{};
            const auto found=std::find_if(s.manifest.shaders.begin(),s.manifest.shaders.end(),[&](const auto& f){return f.program==name&&!profile.empty()&&f.profile==profile;});
            if(found==s.manifest.shaders.end()) throw std::runtime_error("Effect has no shader for the active backend");
            const auto data=Bytes(found->path);
            if(data.size()<4U||data[0]!='C'||data[1]!='S'||data[2]!='H') throw std::runtime_error("Effect shader is not compute bytecode");
            const auto shader=bgfx::createShader(bgfx::copy(data.data(),static_cast<std::uint32_t>(data.size())));
            if(!bgfx::isValid(shader)) throw std::runtime_error("Effect compute shader creation failed");
            auto r=std::make_unique<Resource>();r->kind=Kind::Program;r->program=bgfx::createProgram(shader,true);
            if(!bgfx::isValid(r->program)) throw std::runtime_error("Effect compute program creation failed");
            return s.host->Add(s,std::move(r));});};
        a.createBuffer=[](void* c,BufferKind kind,std::uint32_t count,const void* data,std::uint32_t bytes)->Handle {return Guard<Handle>(c,0U,[&](Slot& s){
            const bool vec=kind==BufferKind::InputVec4||kind==BufferKind::StorageVec4;
            const bool input=kind==BufferKind::InputUint||kind==BufferKind::InputVec4;
            if(static_cast<unsigned>(kind)>3U||count==0U||count>1048576U||
                (bytes!=0U&&(data==nullptr||!input||bytes!=count*(vec?16U:4U)))) throw std::runtime_error("Invalid effect buffer descriptor");
            auto r=std::make_unique<Resource>();r->kind=Kind::Buffer;r->input=input;r->bytes=count*(vec?16U:4U);
            const auto flags=static_cast<std::uint16_t>(input?BGFX_BUFFER_COMPUTE_READ:BGFX_BUFFER_COMPUTE_READ_WRITE);
            if(vec) {
                bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::Position,4U,bgfx::AttribType::Float).end();
                r->vertices=bgfx::createDynamicVertexBuffer(count,layout,flags);
                if(!bgfx::isValid(r->vertices)) throw std::runtime_error("Effect vector buffer allocation failed");
                if(bytes) bgfx::update(r->vertices,0U,bgfx::copy(data,bytes));
            } else {
                r->indices=bgfx::createDynamicIndexBuffer(count,static_cast<std::uint16_t>(flags|BGFX_BUFFER_INDEX32));
                if(!bgfx::isValid(r->indices)) throw std::runtime_error("Effect integer buffer allocation failed");
                if(bytes) bgfx::update(r->indices,0U,bgfx::copy(data,bytes));
            }
            return s.host->Add(s,std::move(r));});};
        a.createTexture=[](void* c,std::uint32_t w,std::uint32_t h)->Handle {return Guard<Handle>(c,0U,[&](Slot& s){
            if(w==0U||h==0U||w>4096U||h>4096U||w>bgfx::getCaps()->limits.maxTextureSize||h>bgfx::getCaps()->limits.maxTextureSize)
                throw std::runtime_error("Invalid effect texture extent");
            if((bgfx::getCaps()->formats[bgfx::TextureFormat::RGBA8]&BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE)==0U)
                throw std::runtime_error("Effect RGBA8 image writes are unsupported");
            auto r=std::make_unique<Resource>();r->kind=Kind::Texture;
            r->texture=bgfx::createTexture2D(static_cast<std::uint16_t>(w),static_cast<std::uint16_t>(h),false,1U,bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_COMPUTE_WRITE|BGFX_TEXTURE_BLIT_DST|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
            if(!bgfx::isValid(r->texture)) throw std::runtime_error("Effect image allocation failed");
            return s.host->Add(s,std::move(r));});};
        a.createUniform=[](void* c,const char* name,bool sampler)->Handle {return Guard<Handle>(c,0U,[&](Slot& s){
            if(name==nullptr||!Identifier(name)||!std::string_view(name).starts_with(sampler?"s_fx_":"u_fx_")) throw std::runtime_error("Effect uniforms require u_fx_ or s_fx_ names");
            auto r=std::make_unique<Resource>();r->kind=Kind::Uniform;r->sampler=sampler;
            r->uniform=bgfx::createUniform(name,sampler?bgfx::UniformType::Sampler:bgfx::UniformType::Vec4);
            if(!bgfx::isValid(r->uniform)) throw std::runtime_error("Effect uniform creation failed");
            return s.host->Add(s,std::move(r));});};
        a.updateBuffer=[](void* c,Handle h,const void* data,std::uint32_t bytes)->bool {return Guard<bool>(c,false,[&](Slot& s){
            auto& r=s.host->Get(s,h);
            if(r.kind!=Kind::Buffer||!r.input||data==nullptr||bytes!=r.bytes) throw std::runtime_error("Effect input buffer update is invalid");
            if(r.hasBeenRead&&r.lastReadFrame==s.host->completed) throw std::runtime_error("Update effect input before its first dispatch in the frame");
            if(bgfx::isValid(r.indices)) bgfx::update(r.indices,0U,bgfx::copy(data,bytes));
            else bgfx::update(r.vertices,0U,bgfx::copy(data,bytes));return true;});};
        a.dispatch=[](void* c,const Dispatch* d)->bool {return Guard<bool>(c,false,[&](Slot& s){
            if(!s.executing||d==nullptr||d->bindingCount>16U||d->uniformCount>16U||
                (d->bindingCount&&d->bindings==nullptr)||(d->uniformCount&&d->uniforms==nullptr)) throw std::runtime_error("Invalid effect dispatch scope");
            auto& program=s.host->Get(s,d->program);if(program.kind!=Kind::Program) throw std::runtime_error("Effect dispatch requires a program");
            for(auto group:d->groups) if(group==0U||group>65535U) throw std::runtime_error("Invalid effect dispatch dimensions");
            std::uint32_t stages=0U;
            // Validate the complete descriptor before changing encoder state.
            for(std::uint32_t i=0U;i<d->bindingCount;++i) {
                const auto& b=d->bindings[i];auto& r=s.host->Get(s,b.resource);
                if(b.slot>=16U||b.slot>=bgfx::getCaps()->limits.maxComputeBindings||(stages&(1U<<b.slot))||static_cast<unsigned>(b.access)>2U)
                    throw std::runtime_error("Invalid or duplicate effect binding");
                stages|=1U<<b.slot;
                for(std::uint32_t j=0U;j<i;++j) if(d->bindings[j].resource==b.resource&&
                    (d->bindings[j].access!=Access::Read||b.access!=Access::Read)) throw std::runtime_error("Conflicting effect resource aliases");
                if(b.kind==BindingKind::Buffer) {
                    if(r.kind!=Kind::Buffer||(r.input&&b.access!=Access::Read)) throw std::runtime_error("Effect buffer access is invalid");
                } else if(b.kind==BindingKind::Image) {
                    if(r.kind!=Kind::Texture) throw std::runtime_error("Effect image binding requires an owned image");
                    const auto formats=bgfx::getCaps()->formats[bgfx::TextureFormat::RGBA8];
                    if((b.access!=Access::Write&&(formats&BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ)==0U)||
                        (b.access!=Access::Read&&(formats&BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE)==0U)) throw std::runtime_error("Effect image access is unsupported");
                } else if(b.kind==BindingKind::SampledTexture) {
                    if(b.slot>=bgfx::getCaps()->limits.maxTextureSamplers) throw std::runtime_error("Effect texture sampler limit exceeded");
                    auto& u=s.host->Get(s,b.sampler);
                    if((r.kind!=Kind::Texture&&r.kind!=Kind::BorrowedTexture)||b.access!=Access::Read||u.kind!=Kind::Uniform||!u.sampler)
                        throw std::runtime_error("Effect sampled texture binding is invalid");
                } else throw std::runtime_error("Unknown effect binding kind");
            }
            for(std::uint32_t i=0U;i<d->uniformCount;++i) {auto& u=s.host->Get(s,d->uniforms[i].uniform);if(u.kind!=Kind::Uniform||u.sampler) throw std::runtime_error("Effect value uniform is invalid");}
            for(std::uint32_t i=0U;i<d->bindingCount;++i) {
                const auto& b=d->bindings[i];auto& r=s.host->Get(s,b.resource);
                const auto access=b.access==Access::Read?bgfx::Access::Read:b.access==Access::Write?bgfx::Access::Write:bgfx::Access::ReadWrite;
                if(b.kind==BindingKind::Buffer) {
                    if(r.input) {r.hasBeenRead=true;r.lastReadFrame=s.host->completed;}
                    if(bgfx::isValid(r.indices)) bgfx::setBuffer(static_cast<std::uint8_t>(b.slot),r.indices,access);
                    else bgfx::setBuffer(static_cast<std::uint8_t>(b.slot),r.vertices,access);
                } else if(b.kind==BindingKind::Image) bgfx::setImage(static_cast<std::uint8_t>(b.slot),r.texture,0U,access,bgfx::TextureFormat::RGBA8);
                else bgfx::setTexture(static_cast<std::uint8_t>(b.slot),s.host->Get(s,b.sampler).uniform,r.texture);
            }
            for(std::uint32_t i=0U;i<d->uniformCount;++i) bgfx::setUniform(s.host->Get(s,d->uniforms[i].uniform).uniform,d->uniforms[i].value);
            bgfx::dispatch(s.view,program.program,d->groups[0],d->groups[1],d->groups[2]);++s.host->totals.dispatches;return true;});};
        a.present=[](void* c,Handle h)->bool {return Guard<bool>(c,false,[&](Slot& s){
            auto& r=s.host->Get(s,h);if(!s.executing||r.kind!=Kind::Texture) throw std::runtime_error("Effect presentation requires an owned image inside onFrame");
            if(!s.host->presentPass.IsInitialized()&&!s.host->presentPass.Initialize()) throw std::runtime_error("Effect presentation shader is unavailable");
            if(!s.host->presentPass.Submit(FinalCompositePassDesc{.viewId=static_cast<bgfx::ViewId>(s.view+1U),.postProcessColor=r.texture,
                .frameBuffer=bgfx::FrameBufferHandle{s.framebuffer},.extent={s.frame.width,s.frame.height}})) throw std::runtime_error("Effect presentation failed");
            ++s.host->totals.presentations;return true;});};
        a.readBuffer=[](void* c,Handle h)->Handle {return Guard<Handle>(c,0U,[&](Slot& s){
            auto& b=s.host->Get(s,h);if(!s.executing||b.kind!=Kind::Buffer) throw std::runtime_error("Effect readback requires a buffer inside onFrame");
            auto r=std::make_unique<Resource>();r->kind=Kind::Readback;r->bytes=b.bytes;r->data.resize(b.bytes);
            const auto handle=s.host->Add(s,std::move(r));auto& read=s.host->Get(s,handle);
            const bgfx::BufferHandle buffer=bgfx::isValid(b.indices)?bgfx::BufferHandle(b.indices):bgfx::BufferHandle(b.vertices);
            read.ready=bgfx::read(bgfx::BufferRegion{.handle=buffer,.size=b.bytes},read.data.data());return handle;});};
        a.pollReadback=[](void* c,Handle h,void* output,std::uint32_t bytes)->std::int32_t {return Guard<std::int32_t>(c,-1,[&](Slot& s)->std::int32_t {
            auto& r=s.host->Get(s,h);if(r.kind!=Kind::Readback||output==nullptr||bytes!=r.bytes) throw std::runtime_error("Invalid effect readback destination");
            if(!Ready(s.host->completed,r.ready)) return 0;
            std::copy(r.data.begin(),r.data.end(),static_cast<std::uint8_t*>(output));
            if(!r.polled) {r.polled=true;++s.host->totals.completedReadbacks;}return 1;});};
        a.release=[](void* c,Handle h)->bool {return Guard<bool>(c,false,[&](Slot& s){return s.host->Release(s,h);});};
        return a;
    }
};

RenderEffectHost::RenderEffectHost():impl_(std::make_unique<Impl>()) {}
RenderEffectHost::~RenderEffectHost() {
    Unload();
    // Only host-owned bytes remain. Drain their finite frame fences before
    // destroying the storage that the asynchronous API writes into.
    while(!impl_->retired.empty()) {impl_->completed=bgfx::frame();impl_->Collect();}
    impl_->presentPass.Shutdown();
}
bool RenderEffectHost::Load(const std::filesystem::path& path,std::string& error) {
    auto& h=*impl_;
    try {
        if(!h.OnThread()||h.running) throw std::runtime_error("Cannot load an effect during a callback");
        if((bgfx::getCaps()->supported&BGFX_CAPS_COMPUTE)==0U) throw std::runtime_error("Compute render effects are unsupported");
        if(h.slots.size()>=6U) throw std::runtime_error("Effect plugin view budget exceeded");
        auto s=std::make_unique<Impl::Slot>();s->host=&h;s->owner=h.nextOwner++;s->manifest=ReadManifest(path);
        auto loaded=h.loader.Load(kb::modules::EngineModuleLoadDesc{.key="render-effect-"+std::to_string(s->owner),.modulePath=s->manifest.library,
            .shadowCopy=true,.shadowCopyDirectory={},.shadowCopyDirectoryName="21kb_render_effects",.diagnosticLabel="render effect"});
        if(!loaded.Succeeded()) throw std::runtime_error(loaded.errors.empty()?"Effect native module load failed":loaded.errors.front());
        s->library=std::move(loaded.library);std::vector<std::string> errors;
        const auto create=reinterpret_cast<CreatePlugin>(s->library.FindSymbol(CreateSymbol,errors,"render effect"));
        if(create==nullptr) throw std::runtime_error("Effect entry point is missing");
        s->api=h.Api(*s);
        // Retain the slot before invoking native code so rollback owns every
        // resource even when creation fails partway through.
        h.slots.push_back(std::move(s));auto& slot=*h.slots.back();
        bool valid=false;
        try {valid=create(&slot.api,&slot.plugin);} catch(...) {h.error="Effect factory threw";}
        if(!valid||slot.plugin.version!=ApiVersion||slot.plugin.size<sizeof(Plugin)||slot.plugin.onFrame==nullptr||slot.plugin.onDestroy==nullptr) {
            const auto reason=h.error.empty()?"Effect factory or ABI validation failed":h.error;
            if(slot.plugin.onDestroy) { try {slot.plugin.onDestroy(slot.plugin.instance);} catch(...) {} }
            for(auto it=h.resources.begin();it!=h.resources.end();) {
                if(it->second->owner==slot.owner) {it->second->Destroy();it=h.resources.erase(it);} else ++it;
            }
            h.slots.pop_back();throw std::runtime_error(reason);
        }
        error.clear();h.error.clear();return true;
    } catch(const std::exception& e) {error=e.what();h.error=error;return false;}
}
void RenderEffectHost::Unload() noexcept {
    auto& h=*impl_;if(!h.OnThread()||h.running) {h.error="Cannot unload an effect during a callback";return;}
    for(auto it=h.slots.rbegin();it!=h.slots.rend();++it) {
        auto& s=**it;
        try {s.plugin.onDestroy(s.plugin.instance);} catch(...) {h.error="Effect destroy callback threw";}
        for(auto r=h.resources.begin();r!=h.resources.end();) {
            if(r->second->owner!=s.owner) {++r;continue;}
            const auto handle=r->first;++r;
            try {static_cast<void>(h.Release(s,handle));} catch(...) {h.error="Effect resource cleanup failed";}
        }
    }
    h.slots.clear();h.Collect();
}
bool RenderEffectHost::Run(std::uint16_t view,std::uint32_t w,std::uint32_t height,std::uint32_t completed,float delta,
    std::uint16_t framebuffer,std::uint16_t sceneColor) {
    auto& h=*impl_;
    if(!h.OnThread()||h.running||w==0U||height==0U||static_cast<std::uint32_t>(view)+h.slots.size()*2U>bgfx::getCaps()->limits.maxViews) {
        h.error="Invalid effect frame or view budget";return false;
    }
    h.completed=completed;h.Collect();h.running=true;
    struct ResetRunning { Impl& host; ~ResetRunning() {host.running=false;for(auto& slot:host.slots) slot->executing=false;} } reset{h};
    bool valid=true;std::string firstError;
    try {
    for(auto& owned:h.slots) {
        auto& s=*owned;s.view=view;view=static_cast<std::uint16_t>(view+2U);s.framebuffer=framebuffer;
        s.frame={.width=w,.height=height,.completedFrame=completed,.deltaSeconds=delta};
        Handle borrowed=0U;
        if(sceneColor!=UINT16_MAX) {
            auto r=std::make_unique<Impl::Resource>();r->kind=Impl::Kind::BorrowedTexture;r->texture={sceneColor};
            borrowed=h.Add(s,std::move(r));s.frame.sceneColor=borrowed;
        }
        s.executing=true;
        try {if(!s.plugin.onFrame(s.plugin.instance,&s.frame)) {valid=false;if(h.error.empty()) h.error="Effect frame callback failed";}}
        catch(...) {valid=false;h.error="Effect frame callback threw";}
        s.executing=false;if(borrowed) h.resources.erase(borrowed);
        if(!valid&&firstError.empty()) firstError=h.error.empty()?"Effect frame callback failed":h.error;
    }
    if(!valid) h.error=firstError;
    return valid;
    } catch(const std::exception& e) {h.error=e.what();return false;}
      catch(...) {h.error="Effect frame failed";return false;}
}
RenderEffectStats RenderEffectHost::Stats() const noexcept {
    auto result=impl_->totals;result.plugins=static_cast<std::uint32_t>(impl_->slots.size());
    result.readbacks=static_cast<std::uint32_t>(impl_->retired.size());
    for(const auto& [handle,r]:impl_->resources) {
        static_cast<void>(handle);
        switch(r->kind) {
        case Impl::Kind::Program:++result.programs;break;
        case Impl::Kind::Buffer:++result.buffers;break;
        case Impl::Kind::Texture:++result.textures;break;
        case Impl::Kind::Uniform:++result.uniforms;break;
        case Impl::Kind::Readback:++result.readbacks;break;
        case Impl::Kind::BorrowedTexture:break;
        }
    }
    return result;
}
std::uint64_t RenderEffectHost::PluginStatus(std::uint32_t i) const noexcept {
    if(i>=impl_->slots.size()||impl_->slots[i]->plugin.status==nullptr) return 0U;
    try {return impl_->slots[i]->plugin.status(impl_->slots[i]->plugin.instance);} catch(...) {return 0U;}
}
const std::string& RenderEffectHost::LastError() const noexcept {return impl_->error;}
} // namespace kb::render
