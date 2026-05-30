#pragma once

#include "engine/ecs/Entity.hpp"

#include <string>
#include <string_view>

struct ecs_world_t;

namespace kb::ecs {

class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    [[nodiscard]] Entity CreateEntity();
    [[nodiscard]] Entity CreateEntity(std::string_view name);

    void DestroyEntity(Entity entity) noexcept;

    [[nodiscard]] bool IsAlive(Entity entity) const noexcept;
    [[nodiscard]] std::string Name(Entity entity) const;

    [[nodiscard]] bool Progress(float deltaSeconds);
    void RequestQuit() noexcept;
    [[nodiscard]] bool ShouldQuit() const noexcept;

    [[nodiscard]] ecs_world_t* NativeHandle() noexcept;
    [[nodiscard]] const ecs_world_t* NativeHandle() const noexcept;

private:
    void Reset() noexcept;

    ecs_world_t* world_ = nullptr;
};

} // namespace kb::ecs
