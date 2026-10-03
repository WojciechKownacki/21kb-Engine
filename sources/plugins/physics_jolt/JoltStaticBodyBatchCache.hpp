#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kb::physics_jolt {

// Validity only: Jolt body records and authored component data keep their
// existing owners. A structural change also invalidates the row ordering.
class JoltStaticBodyBatchCache {
public:
    struct Key {
        kb::scene::SceneEntity firstEntity{};
        std::size_t count = 0U;
        std::uint64_t structuralVersion = 0U;
        std::uint64_t transformVersion = 0U;
        std::uint64_t colliderVersion = 0U;
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };

    [[nodiscard]] bool Matches(std::size_t index, const Key& key) const noexcept {
        return index < keys_.size() && keys_[index] == key;
    }
    void Store(std::size_t index, const Key& key) {
        if (index == keys_.size()) keys_.push_back(key);
        else keys_[index] = key;
    }
    void Finish(std::size_t count) noexcept {
        if (count < keys_.size()) keys_.resize(count);
    }

private:
    std::vector<Key> keys_;
};

} // namespace kb::physics_jolt
