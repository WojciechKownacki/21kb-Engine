#include "scene/prefab/ScenePrefabGuid.hpp"

#include "scene/prefab/ScenePrefabHasher.hpp"

#include <iomanip>
#include <sstream>

namespace kb::scene {

std::string ScenePrefabGuid::Create(std::string_view name, const ScenePrefab& prefab, std::uint64_t localId) {
    std::uint64_t hash = ScenePrefabHasher::Hash(prefab);
    for (const char character : name) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }

    std::ostringstream output;
    output << "prefab-" << std::hex << std::setw(16) << std::setfill('0') << hash << '-' << localId;
    return output.str();
}

} // namespace kb::scene
