#pragma once

#include "engine/scene/SceneDocumentService.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/streaming/BackgroundLoadService.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "scene/prefab/ScenePrefabReferenceResolver.hpp"
#include <future>
#include <memory>
#include <vector>

namespace kb::scene {
struct SceneStreamingState {
    struct Prepared {
        std::shared_ptr<const ScenePrefab> prefab;
        std::string name;
        std::string error;
        bool hasReferences = false;
    };
    struct Job {
        std::uint64_t id = 0U;
        kb::assets::AssetId assetId{};
        std::filesystem::path path;
        SceneEntity parent{};
        SceneEntity root{};
        SceneLoadStatus status = SceneLoadStatus::Loading;
        std::future<Prepared> preparation;
        Prepared prepared;
        std::shared_ptr<const ScenePrefab> source;
        std::vector<SceneObject> objects;
        ScenePrefabReferenceResolver::EntityMap references;
        std::vector<SceneEntity> unloadStack;
        std::size_t created = 0U;
        std::size_t activated = 0U;
        float progress = 0.0F;
        bool cancelled = false;
        std::string error;
    };
    SceneStreamingSettings settings;
    SceneStreamingStats stats;
    std::vector<Job> jobs;
    std::size_t nextJob = 0U;
    // Preparation and retirement run as jobs of the engine's background service, taken by the
    // first job that needs it. A job owns what it captured, so the scene may go first.
    std::shared_ptr<kb::assets::streaming::BackgroundLoadService> background;
};

class SceneStreamingService {
public:
    static std::uint64_t Load(Scene& scene, const std::filesystem::path& path, SceneEntity parent);
    static bool Unload(Scene& scene, std::uint64_t id);
    static void Pump(Scene& scene);
    static void CancelPending(Scene& scene);
    static SceneLoadStatus Status(const Scene& scene, std::uint64_t id) noexcept;
    static float Progress(const Scene& scene, std::uint64_t id) noexcept;
    static std::string Error(const Scene& scene, std::uint64_t id);
    static void Configure(Scene& scene, SceneStreamingSettings settings);
    static SceneStreamingStats Stats(const Scene& scene) noexcept;
};
}
