#pragma once

#include "engine/ecs/WorldSerialization.hpp"

namespace kb::ecs {

class World;

class SerializedWorldRestorer {
public:
    explicit SerializedWorldRestorer(World& world) noexcept;

    [[nodiscard]] bool Restore(const SerializedWorld& source) const;

private:
    World& world_;
};

} // namespace kb::ecs
