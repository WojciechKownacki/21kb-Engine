#pragma once

#include "engine/ecs/Entity.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

// The id each object keeps across saves, so a file records an object under the same id every time and the links
// between objects survive. An object has none until a save or a link needs one; a file read hands it the id the
// file holds. Ids are unique in a scene: new ones count up from the largest id seen. Dense by entity index.
class SceneStableObjectIds {
public:
    static constexpr std::uint64_t None = 0U;

    // The object's id, or None while it has none.
    [[nodiscard]] std::uint64_t Find(kb::ecs::Entity entity) const noexcept {
        const std::uint32_t index = kb::ecs::GeneratedEntityIndex(entity);
        return index < ids_.size() ? ids_[index] : None;
    }

    // The object's id; one is made when it has none. None for an entity that has no dense index.
    [[nodiscard]] std::uint64_t Ensure(kb::ecs::Entity entity) {
        const std::uint32_t index = kb::ecs::GeneratedEntityIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
            return None;
        }
        if (index >= ids_.size()) {
            ids_.resize(static_cast<std::size_t>(index) + 1U, None);
        }
        if (ids_[index] == None) {
            ids_[index] = next_++;
        }
        return ids_[index];
    }

    // Gives the object the id a file recorded for it.
    void Set(kb::ecs::Entity entity, std::uint64_t id) {
        const std::uint32_t index = kb::ecs::GeneratedEntityIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex || id == None) {
            return;
        }
        if (index >= ids_.size()) {
            ids_.resize(static_cast<std::size_t>(index) + 1U, None);
        }
        ids_[index] = id;
        if (id >= next_ && id < kLargestCounted) {
            next_ = id + 1U;
        }
    }

    // The id of every object by entity index (None where an object has none), for finding objects by id.
    [[nodiscard]] const std::vector<std::uint64_t>& Dense() const noexcept { return ids_; }

    // Takes the id off a destroyed object.
    void Clear(kb::ecs::Entity entity) noexcept {
        const std::uint32_t index = kb::ecs::GeneratedEntityIndex(entity);
        if (index < ids_.size()) {
            ids_[index] = None;
        }
    }

    // Notes of ids that two objects held and one had to give up (see ScenePrefabCaptureService).
    void NoteConflict(std::string message) {
        if (conflicts_.size() < kMaxConflictNotes) {
            conflicts_.push_back(std::move(message));
        }
    }
    [[nodiscard]] const std::vector<std::string>& Conflicts() const noexcept { return conflicts_; }

private:
    // UINT64_MAX marks an unresolved link, so the counter never reaches it.
    static constexpr std::uint64_t kLargestCounted = UINT64_MAX - 1U;
    static constexpr std::size_t kMaxConflictNotes = 64U;

    std::vector<std::uint64_t> ids_;
    std::uint64_t next_ = 1U;
    std::vector<std::string> conflicts_;
};

} // namespace kb::scene
