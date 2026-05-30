#pragma once

#include "engine/ecs/WorldSerialization.hpp"

namespace kb::ecs {

class World;

class SerializedWorldWriter {
public:
    explicit SerializedWorldWriter(const World& world) noexcept;

    [[nodiscard]] bool Write(SerializedWorld& output) const;

private:
    const World& world_;
};

} // namespace kb::ecs
