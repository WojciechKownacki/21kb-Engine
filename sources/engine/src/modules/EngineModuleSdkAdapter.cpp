#include "modules/EngineModuleSdkAdapter.hpp"
#include "engine/modules/EngineModuleCallback.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace kb::modules {
namespace {
std::atomic<std::uint64_t> nextHandle{ 1U };
std::mutex callbackMutex;
std::unordered_map<KbModuleRegistration, std::weak_ptr<ModuleCallbackState>> callbacks;

void Error(KbModuleError* output, std::string_view message) noexcept {
    if (output == nullptr) return;
    const auto count = std::min(message.size(), sizeof(output->message) - 1U);
    std::memcpy(output->message, message.data(), count);
    output->message[count] = '\0';
}
std::string Message(const KbModuleError& error) {
    const auto end = std::find(std::begin(error.message), std::end(error.message), '\0');
    return { error.message, end };
}
std::string CopyString(KbModuleString value) {
    if (value.size > 4096U || (value.size != 0U && value.data == nullptr))
        throw std::runtime_error("invalid SDK string view");
    return value.size == 0U ? std::string{} : std::string{ value.data, value.size };
}
void Check(KbModuleStatus status, const KbModuleError& error, const char* operation) {
    if (status != KB_MODULE_OK)
        throw std::runtime_error(std::string{ "SDK module " } + operation + " failed (" +
            std::to_string(status) + "): " + Message(error));
}
} // namespace

struct ModuleCallbackState {
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic<bool> active{ false };
    KbModuleRegistration id = 0U;
    KbModuleScene scene = 0U;
    KbModuleTick tick = nullptr;
    KbModuleRelease release = nullptr;
    void* user = nullptr;
    unsigned invoking = 0U;

    KbModuleStatus Invoke(float deltaSeconds, KbModuleError* error) noexcept {
        if (std::this_thread::get_id() != owner) return KB_MODULE_WRONG_THREAD;
        if (!active.load()) return KB_MODULE_STALE_HANDLE;
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) return KB_MODULE_INVALID_ARGUMENT;
        if (invoking != 0U) return KB_MODULE_BUSY;
        ++invoking;
        KbModuleStatus result = KB_MODULE_INTERNAL_ERROR;
        try { result = tick(user, scene, deltaSeconds, error); }
        catch (...) { Error(error, "SDK callback violated the no-exception contract"); }
        --invoking;
        return result;
    }
    void Deactivate() {
        active.store(false);
        const auto dispose = release;
        void* data = user;
        tick = nullptr;
        release = nullptr;
        user = nullptr;
        {
            std::lock_guard lock{ callbackMutex };
            callbacks.erase(id);
        }
        if (dispose != nullptr) dispose(data);
    }
};

KbModuleStatus EngineModuleCallback::Invoke(float deltaSeconds, KbModuleError* error) const noexcept {
    return state_ ? state_->Invoke(deltaSeconds, error) : KB_MODULE_STALE_HANDLE;
}
bool EngineModuleCallback::IsActive() const noexcept { return state_ && state_->active.load(); }
EngineModuleCallback RetainEngineModuleCallback(KbModuleRegistration registration) {
    EngineModuleCallback result;
    std::lock_guard lock{ callbackMutex };
    if (const auto found = callbacks.find(registration); found != callbacks.end())
        result.state_ = found->second.lock();
    return result;
}

namespace {
class CallbackSystem final : public kb::scene::SceneSystem {
public:
    explicit CallbackSystem(std::shared_ptr<ModuleCallbackState> state) : state_(std::move(state)) {}
    void OnUpdate(kb::scene::SceneSystemContext& context) override {
        KbModuleError error{};
        const auto status = state_->Invoke(context.DeltaSeconds(), &error);
        if (status != KB_MODULE_OK && status != KB_MODULE_STALE_HANDLE)
            context.ReportError("SDK module callback failed: " + Message(error));
    }
private:
    std::shared_ptr<ModuleCallbackState> state_;
};

class SdkModule final : public IEngineModule {
    struct SceneBinding {
        kb::scene::Scene* scene = nullptr;
        std::unordered_map<KbModuleEntity, kb::scene::SceneEntity> entities;
    };
    struct Registration {
        kb::scene::Scene* scene = nullptr;
        kb::scene::SceneSystemHandle system;
        std::shared_ptr<ModuleCallbackState> state;
    };
public:
    SdkModule(EngineModuleLibrary library, KbModuleApi api, EngineModuleMetadata metadata)
        : library_(std::move(library)), api_(api), metadata_(std::move(metadata)) {
        host_ = { sizeof(KbModuleHostApi), KB_MODULE_SDK_VERSION, this,
            &CreateEntity, &DestroyEntity, &GetPosition, &SetPosition, &RegisterTick, &UnregisterTick };
    }
    ~SdkModule() override {
        // The host must not be destroyed from inside scene dispatch.
        while (!scenes_.empty()) CleanupScene(scenes_.begin()->first);
        if (instance_ != nullptr) api_.destroy(instance_);
    }
    void Create() {
        KbModuleError error{};
        const auto status = api_.create(&host_, &instance_, &error);
        Check(status, error, "create");
        if (instance_ == nullptr) throw std::runtime_error("SDK module create returned no instance");
    }
    EngineModuleMetadata Metadata() const override { return metadata_; }
    void OnLoad(EngineModuleContext&) override { Lifecycle(api_.load, "load"); }
    void OnEnable() override { Lifecycle(api_.enable, "enable"); }
    void OnDisable() override { if (api_.disable) api_.disable(instance_); }
    void OnUnload() override { DetachAll(); if (api_.unload) api_.unload(instance_); }
    void OnSceneAttach(kb::scene::Scene& scene) override {
        for (const auto& [id, binding] : scenes_) {
            static_cast<void>(id);
            if (binding.scene == &scene) return;
        }
        const auto id = nextHandle.fetch_add(1U);
        scenes_.emplace(id, SceneBinding{ &scene, {} });
        acceptingScene_ = id;
        try {
            KbModuleError error{};
            const auto status = api_.attach ? api_.attach(instance_, id, &error) : KB_MODULE_OK;
            acceptingScene_ = 0U;
            Check(status, error, "attach");
        } catch (...) {
            acceptingScene_ = 0U;
            CleanupScene(id);
            throw;
        }
    }
    void OnSceneDetach(kb::scene::Scene& scene) override {
        for (const auto& [id, binding] : scenes_) {
            if (binding.scene != &scene) continue;
            RemoveSceneCallbacks(id);
            if (api_.detach) api_.detach(instance_, id);
            CleanupScene(id);
            return;
        }
    }
private:
    void Lifecycle(KbModuleLifecycle function, const char* name) {
        if (function == nullptr) return;
        KbModuleError error{};
        const auto status = function(instance_, &error);
        Check(status, error, name);
    }
    void DetachAll() {
        while (!scenes_.empty()) OnSceneDetach(*scenes_.begin()->second.scene);
    }
    void RemoveSceneCallbacks(KbModuleScene scene) {
        std::vector<KbModuleRegistration> ids;
        for (const auto& [id, registration] : registrations_)
            if (registration.state->scene == scene) ids.push_back(id);
        for (const auto id : ids) {
            const auto status = Remove(id);
            if (status != KB_MODULE_OK && status != KB_MODULE_STALE_HANDLE)
                throw std::logic_error("SDK module cannot detach during active callback dispatch");
        }
    }
    void CleanupScene(KbModuleScene id) {
        RemoveSceneCallbacks(id);
        auto& binding = scenes_.at(id);
        for (const auto& [token, entity] : binding.entities) {
            static_cast<void>(token);
            binding.scene->Entities().Destroy(entity);
        }
        scenes_.erase(id);
    }
    KbModuleStatus Remove(KbModuleRegistration id) {
        const auto found = registrations_.find(id);
        if (found == registrations_.end()) return KB_MODULE_STALE_HANDLE;
        auto& registration = found->second;
        if (registration.state->invoking != 0U) return KB_MODULE_BUSY;
        if (!registration.scene->Runtime().RemoveSceneSystem(registration.system) &&
            registration.scene->Runtime().HasSceneSystem(registration.system)) return KB_MODULE_BUSY;
        auto state = registration.state;
        registrations_.erase(found);
        state->Deactivate();
        return KB_MODULE_OK;
    }
    template<class F>
    static KbModuleStatus Guard(void* context, KbModuleError* error, F&& function) noexcept {
        if (context == nullptr) return KB_MODULE_INVALID_ARGUMENT;
        auto& self = *static_cast<SdkModule*>(context);
        if (std::this_thread::get_id() != self.owner_) return KB_MODULE_WRONG_THREAD;
        try { return function(self); }
        catch (const std::exception& exception) { Error(error, exception.what()); }
        catch (...) { Error(error, "engine SDK operation failed"); }
        return KB_MODULE_INTERNAL_ERROR;
    }
    static KbModuleStatus KB_MODULE_CALL CreateEntity(void* context, KbModuleScene scene,
        KbModuleString name, KbModuleEntity* output, KbModuleError* error) noexcept {
        if (output == nullptr) return KB_MODULE_INVALID_ARGUMENT;
        *output = 0U;
        return Guard(context, error, [&](SdkModule& self) -> KbModuleStatus {
            const auto found = self.scenes_.find(scene);
            if (found == self.scenes_.end()) return KB_MODULE_STALE_HANDLE;
            auto& binding = found->second;
            const auto id = nextHandle.fetch_add(1U);
            auto [entry, inserted] = binding.entities.emplace(id, kb::scene::SceneEntity{});
            static_cast<void>(inserted);
            try { entry->second = binding.scene->Entities().CreateEntity({ .name = CopyString(name) }); }
            catch (...) { binding.entities.erase(entry); throw; }
            if (!entry->second.IsValid()) { binding.entities.erase(entry); return KB_MODULE_INTERNAL_ERROR; }
            *output = id;
            return KB_MODULE_OK;
        });
    }
    static KbModuleStatus KB_MODULE_CALL DestroyEntity(void* context, KbModuleScene scene,
        KbModuleEntity entity, KbModuleError* error) noexcept {
        return Guard(context, error, [&](SdkModule& self) -> KbModuleStatus {
            const auto binding = self.scenes_.find(scene);
            if (binding == self.scenes_.end()) return KB_MODULE_STALE_HANDLE;
            const auto found = binding->second.entities.find(entity);
            if (found == binding->second.entities.end()) return KB_MODULE_STALE_HANDLE;
            binding->second.scene->Entities().Destroy(found->second);
            binding->second.entities.erase(found);
            return KB_MODULE_OK;
        });
    }
    template<class F>
    static KbModuleStatus WithEntity(SdkModule& self, KbModuleScene scene, KbModuleEntity entity, F&& function) {
        const auto binding = self.scenes_.find(scene);
        if (binding == self.scenes_.end()) return KB_MODULE_STALE_HANDLE;
        const auto found = binding->second.entities.find(entity);
        if (found == binding->second.entities.end() ||
            !binding->second.scene->Entities().IsAlive(found->second)) return KB_MODULE_STALE_HANDLE;
        return function(*binding->second.scene, found->second);
    }
    static KbModuleStatus KB_MODULE_CALL GetPosition(void* context, KbModuleScene scene,
        KbModuleEntity entity, KbModuleVec3* output, KbModuleError* error) noexcept {
        if (output == nullptr) return KB_MODULE_INVALID_ARGUMENT;
        return Guard(context, error, [&](SdkModule& self) {
            return WithEntity(self, scene, entity, [&](kb::scene::Scene& target, kb::scene::SceneEntity id) -> KbModuleStatus {
                const auto* transform = target.Transforms().TryGet(id);
                if (transform == nullptr) return KB_MODULE_STALE_HANDLE;
                *output = { transform->localPosition.x, transform->localPosition.y, transform->localPosition.z };
                return KB_MODULE_OK;
            });
        });
    }
    static KbModuleStatus KB_MODULE_CALL SetPosition(void* context, KbModuleScene scene,
        KbModuleEntity entity, KbModuleVec3 position, KbModuleError* error) noexcept {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return KB_MODULE_INVALID_ARGUMENT;
        return Guard(context, error, [&](SdkModule& self) {
            return WithEntity(self, scene, entity, [&](kb::scene::Scene& target, kb::scene::SceneEntity id) -> KbModuleStatus {
                const auto* current = target.Transforms().TryGet(id);
                if (current == nullptr) return KB_MODULE_STALE_HANDLE;
                auto transform = *current;
                transform.localPosition = { position.x, position.y, position.z };
                target.Transforms().Set(id, transform);
                return KB_MODULE_OK;
            });
        });
    }
    static KbModuleStatus KB_MODULE_CALL RegisterTick(void* context, KbModuleScene scene, KbModuleTick tick,
        void* user, KbModuleRelease release, KbModuleRegistration* output, KbModuleError* error) noexcept {
        if (tick == nullptr || output == nullptr) return KB_MODULE_INVALID_ARGUMENT;
        *output = 0U;
        return Guard(context, error, [&](SdkModule& self) -> KbModuleStatus {
            const auto binding = self.scenes_.find(scene);
            if (binding == self.scenes_.end()) return KB_MODULE_STALE_HANDLE;
            if (self.acceptingScene_ != scene) return KB_MODULE_BUSY;
            const auto id = nextHandle.fetch_add(1U);
            auto state = std::make_shared<ModuleCallbackState>();
            state->id = id; state->scene = scene; state->tick = tick; state->user = user;
            {
                std::lock_guard lock{ callbackMutex };
                callbacks.emplace(id, state);
            }
            try {
                auto [entry, inserted] = self.registrations_.emplace(id,
                    Registration{ binding->second.scene, {}, state });
                static_cast<void>(inserted);
                entry->second.system = binding->second.scene->Runtime().AddSceneSystem(
                    std::make_unique<CallbackSystem>(state));
                if (!entry->second.system.IsValid()) throw std::runtime_error("scene rejected SDK callback registration");
            } catch (...) {
                self.registrations_.erase(id);
                std::lock_guard lock{ callbackMutex };
                callbacks.erase(id);
                throw;
            }
            state->release = release;
            state->active.store(true);
            *output = id;
            return KB_MODULE_OK;
        });
    }
    static KbModuleStatus KB_MODULE_CALL UnregisterTick(void* context,
        KbModuleRegistration id, KbModuleError* error) noexcept {
        return Guard(context, error, [&](SdkModule& self) { return self.Remove(id); });
    }
    EngineModuleLibrary library_;
    KbModuleApi api_;
    EngineModuleMetadata metadata_;
    KbModuleHostApi host_{};
    void* instance_ = nullptr;
    const std::thread::id owner_ = std::this_thread::get_id();
    KbModuleScene acceptingScene_ = 0U;
    std::unordered_map<KbModuleScene, SceneBinding> scenes_;
    std::unordered_map<KbModuleRegistration, Registration> registrations_;
};
} // namespace

std::unique_ptr<IEngineModule> CreateSdkModule(EngineModuleLibrary library, KbModuleGetApi getApi,
    std::string_view requestedName, std::vector<std::string>& diagnostics) {
    std::unique_ptr<SdkModule> module;
    try {
        KbModuleApi api{};
        KbModuleError error{};
        const auto status = getApi(KB_MODULE_SDK_VERSION, sizeof(api), &api, &error);
        Check(status, error, "API negotiation");
        if (api.size != sizeof(api) || api.version != KB_MODULE_SDK_VERSION ||
            api.pointer_size != sizeof(void*) || api.create == nullptr || api.destroy == nullptr ||
            api.loading_phase > 3U || api.dependency_count > 64U ||
            (api.dependency_count != 0U && api.dependencies == nullptr))
            throw std::runtime_error("invalid SDK module API layout or version");
        EngineModuleMetadata metadata;
        metadata.name = CopyString(api.name);
        if (metadata.name.empty() || (!requestedName.empty() && requestedName != metadata.name))
            throw std::runtime_error("SDK module identity does not match the project reference");
        metadata.version = api.module_version;
        metadata.loadingPhase = static_cast<EngineModuleLoadingPhase>(api.loading_phase);
        for (std::uint32_t index = 0U; index < api.dependency_count; ++index) {
            auto dependency = CopyString(api.dependencies[index]);
            if (dependency.empty()) throw std::runtime_error("empty SDK module dependency");
            metadata.dependencies.push_back(std::move(dependency));
        }
        module = std::make_unique<SdkModule>(std::move(library), api, std::move(metadata));
        module->Create();
        return module;
    } catch (const std::exception& error) {
        diagnostics.push_back("SDK module '" + std::string{ requestedName } + "': " + error.what());
    } catch (...) {
        diagnostics.push_back("SDK module violated the no-exception contract");
    }
    return nullptr;
}
} // namespace kb::modules
