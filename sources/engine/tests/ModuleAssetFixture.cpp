#include "ModuleAssetFixture.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/modules/EngineModuleContext.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <chrono>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <thread>

namespace {
void Trace(const char* value) { std::ofstream{ KB_ASSET_TRACE, std::ios::app } << value << '\n'; }
std::shared_ptr<kb::tests::ModuleAssetValue> Payload() {
    return { new kb::tests::ModuleAssetValue{}, [](kb::tests::ModuleAssetValue* value) {
        Trace("payload destroyed"); delete value;
    } };
}
class Loader final : public kb::assets::IAssetLoader {
public:
    ~Loader() override { Trace("loader destroyed"); }
    std::string_view Type() const noexcept override { return "ModuleAsset"; }
    std::type_index PayloadType() const noexcept override { return typeid(kb::tests::ModuleAssetValue); }
    std::vector<std::string> Extensions() const override { return { ".moduleasset" }; }
    kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override {
        Trace("load entered");
        if (request.metadata.name == "Slow") {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
            while (!request.IsCancellationRequested() && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
            Trace(request.IsCancellationRequested() ? "load cancelled" : "load timeout");
        }
        return { .asset = Payload() };
    }
};
class Module final : public kb::modules::IEngineModule {
public:
    ~Module() override { Trace("module destroyed"); }
    kb::modules::EngineModuleMetadata Metadata() const override { return { .name = "Tests.AssetLifetime" }; }
    void OnLoad(kb::modules::EngineModuleContext& context) override { context_ = context; }
    void OnSceneAttach(kb::scene::Scene& scene) override {
        if (!context_->RegisterAssetLoader(scene.Assets().Manager(), std::make_unique<Loader>()))
            throw std::runtime_error("Module loader registration failed");
    }
    void OnSceneDetach(kb::scene::Scene& scene) override {
        static_cast<void>(scene.Assets().Manager().UnregisterLoader("ModuleAsset"));
    }
private:
    std::optional<kb::modules::EngineModuleContext> context_;
};
} // namespace

#ifdef KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT
KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT
#endif
extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() { return kb::modules::kEngineModuleAbiVersion; }
extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() { return "Tests.AssetLifetime"; }
extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() { return new Module{}; }
extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) { delete module; }
extern "C" KB_ENGINE_MODULE_EXPORT kb::assets::IAssetLoader* kb_test_asset_loader() { return new Loader{}; }
extern "C" KB_ENGINE_MODULE_EXPORT bool kb_test_publish_asset(kb::assets::AssetManager* manager) {
    return manager->PublishRuntimeAsset(kb::assets::AssetId{ 3 }, Payload());
}
