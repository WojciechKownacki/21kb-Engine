#pragma once

#include "engine/assets/Map2DAsset.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneTilemapRuntime.hpp"

#include <memory>
#include <optional>
#include <string>

namespace kb::world2d {

// Owns per-entity map copies and disposable chunk resources. Authored map assets remain shared.
// Mutations are visible to queries immediately; geometry and physics update before simulation.
class Map2DRuntime final : public kb::scene::SceneSystem, public kb::scene::SceneTilemapRuntime {
public:
    static constexpr std::uint32_t ChunkSize = 16U;
    Map2DRuntime();
    ~Map2DRuntime() override;
    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override;
    void OnUpdateEnd(kb::scene::SceneSystemContext& context) override;
    kb::scene::SceneFixedUpdatePhase FixedUpdatePhase() const noexcept override { return kb::scene::SceneFixedUpdatePhase::PrepareSimulation; }
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    [[nodiscard]] const kb::assets::Map2DAsset* Map(kb::scene::SceneEntity entity) const override;
    [[nodiscard]] std::uint64_t Revision(kb::scene::SceneEntity entity) const override;
    [[nodiscard]] std::optional<kb::assets::Map2DTile> Tile(kb::scene::SceneEntity entity,
        std::uint32_t x, std::uint32_t y, std::int32_t floor = 0) const;
    [[nodiscard]] bool SetTile(kb::scene::SceneEntity entity, std::uint32_t x, std::uint32_t y,
        std::int32_t floor, const kb::assets::Map2DTile& tile, std::string* error = nullptr) override;
    [[nodiscard]] bool PaintGround(kb::scene::SceneEntity entity, std::uint32_t x, std::uint32_t y,
        std::int32_t floor, std::string_view brush, std::string* error = nullptr) override;
    [[nodiscard]] bool EraseTile(kb::scene::SceneEntity entity, std::uint32_t x, std::uint32_t y,
        std::int32_t floor = 0, std::string* error = nullptr);
    [[nodiscard]] std::size_t RebuiltChunksLastUpdate() const noexcept;

private:
    void Refresh(kb::scene::SceneSystemContext& context, bool advanceTime);
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::world2d
