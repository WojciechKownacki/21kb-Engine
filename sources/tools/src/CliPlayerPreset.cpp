#include "CliPlayerPreset.hpp"
#include "engine/scene/ScenePlayerPreset.hpp"
namespace kb::cli {
bool IsPlayerPreset(std::string_view item) noexcept { return kb::scene::IsPlayerPreset(item); }
kb::scene::SceneEntity AddPlayerPreset(kb::scene::Scene& scene, const std::filesystem::path& projectRoot,
    std::string_view item, std::string_view name, CommandIo io, bool box2d) {
    return kb::scene::AddPlayerPreset(scene, projectRoot, item, name, { io.out, io.err }, box2d);
}
} // namespace kb::cli
