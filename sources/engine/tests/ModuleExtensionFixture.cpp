#include "ModuleExtensionFixture.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/modules/EngineModuleContext.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneModuleComponents.hpp"

#include <fstream>
#include <optional>
#include <stdexcept>

namespace {

constexpr const char* kName = "Tests.ModuleExtensions";

class Loader final : public kb::assets::IAssetLoader {
public:
    std::string_view Type() const noexcept override { return "ModuleExtensionAsset"; }
    std::type_index PayloadType() const noexcept override { return typeid(kb::tests::ModuleExtensionAsset); }
    std::vector<std::string> Extensions() const override { return { ".kbtest" }; }
    kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override {
        auto asset = std::make_shared<kb::tests::ModuleExtensionAsset>();
        kb::assets::AssetLoadResult result;
        if (!request.ReadSourceBytes(asset->bytes, result.error)) return result;
        result.asset = std::move(asset);
        return result;
    }
    std::vector<kb::assets::AssetId> DiscoverDependencies(const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry&) const override {
        std::ifstream input{ metadata.physicalPath, std::ios::binary };
        std::uint64_t id = 0U;
        input.read(reinterpret_cast<char*>(&id), sizeof(id));
        return input && id != 0U ? std::vector<kb::assets::AssetId>{ kb::assets::AssetId{ id } } : std::vector<kb::assets::AssetId>{};
    }
};

constexpr kb::scene::SceneModuleComponentField kFields[] = {
    KB_SCENE_MODULE_FIELD(kb::tests::ModuleExtensionComponent, strength, kb::ecs::ComponentFieldType::Float32, "Strength", "How hard it hits."),
    KB_SCENE_MODULE_FIELD(kb::tests::ModuleExtensionComponent, count, kb::ecs::ComponentFieldType::Int32, "Count", "How many times."),
    KB_SCENE_MODULE_FIELD(kb::tests::ModuleExtensionComponent, armed, kb::ecs::ComponentFieldType::Bool, "Armed", "Whether it can fire."),
};

class Module final : public kb::modules::IEngineModule {
public:
    kb::modules::EngineModuleMetadata Metadata() const override { return { .name = kName }; }
    void OnLoad(kb::modules::EngineModuleContext& context) override { context_ = context; }
    void OnSceneAttach(kb::scene::Scene& scene) override {
        if (!context_->RegisterAssetLoader(scene.Assets().Manager(), std::make_unique<Loader>()) ||
            !context_->RegisterSceneComponent<kb::tests::ModuleExtensionComponent>(
                scene, { .name = "ModuleExtension", .label = "Module Extension", .category = "Tests", .fields = kFields }))
            throw std::runtime_error("Module extension registration failed");
    }
    void OnSceneDetach(kb::scene::Scene& scene) override {
        static_cast<void>(scene.Assets().Manager().UnregisterLoader("ModuleExtensionAsset"));
        static_cast<void>(context_->UnregisterSceneComponent(scene, "ModuleExtension"));
    }

private:
    std::optional<kb::modules::EngineModuleContext> context_;
};

} // namespace

#ifdef KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT
KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT
#endif
extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() { return kb::modules::kEngineModuleAbiVersion; }
extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() { return kName; }
extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() { return new Module{}; }
extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) { delete module; }
