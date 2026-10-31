#include "Sprite2DRuntime.hpp"
#include "Map2DEditing.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace kb::world2d {
namespace {

using kb::assets::AssetId;
using kb::assets::AssetMetadata;
using kb::scene::SceneEntity;
using kb::scene::Sprite2DComponent;

constexpr float kSortingOrderLift = kb::render::RenderMap2DMeshBuilder::kSortingOrderLift;

[[nodiscard]] std::uint32_t Channel(float value) noexcept {
    return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

[[nodiscard]] std::uint32_t PackTint(const Sprite2DComponent& sprite) noexcept {
    return (Channel(sprite.tintR) << 24U) | (Channel(sprite.tintG) << 16U) | (Channel(sprite.tintB) << 8U) | Channel(sprite.tintA);
}

} // namespace

std::uint32_t Sprite2DFrameCell(const Sprite2DComponent& sprite, double seconds) noexcept {
    if (sprite.frameCount <= 1U) return sprite.cell;
    const double steps = std::isfinite(seconds) && seconds > 0.0 ? std::floor(seconds * static_cast<double>(sprite.framesPerSecond)) : 0.0;
    const double count = static_cast<double>(sprite.frameCount);
    const double frame = sprite.loop ? std::fmod(steps, count) : std::min(steps, count - 1.0);
    return sprite.cell + static_cast<std::uint32_t>(std::max(0.0, frame));
}

std::optional<Sprite2DSheetLayout> Sprite2DLayout(const Sprite2DComponent& sprite, std::uint32_t imageWidth, std::uint32_t imageHeight) noexcept {
    Sprite2DSheetLayout layout;
    layout.cellWidth = sprite.cellWidth != 0U ? sprite.cellWidth : imageWidth;
    layout.cellHeight = sprite.cellHeight != 0U ? sprite.cellHeight : (sprite.cellWidth != 0U ? sprite.cellWidth : imageHeight);
    if (layout.cellWidth == 0U || layout.cellHeight == 0U || imageWidth == 0U || imageHeight == 0U) return std::nullopt;
    const std::uint64_t usableWidth = imageWidth > 2ULL * sprite.margin ? imageWidth - 2ULL * sprite.margin : 0ULL;
    const std::uint64_t usableHeight = imageHeight > 2ULL * sprite.margin ? imageHeight - 2ULL * sprite.margin : 0ULL;
    layout.columns = static_cast<std::uint32_t>((usableWidth + sprite.spacing) / (layout.cellWidth + sprite.spacing));
    layout.rows = static_cast<std::uint32_t>((usableHeight + sprite.spacing) / (layout.cellHeight + sprite.spacing));
    if (layout.columns == 0U || layout.rows == 0U) return std::nullopt;
    return layout;
}

std::optional<kb::assets::Map2DAsset> BuildSprite2DMap(const Sprite2DComponent& sprite, std::string_view texturePath,
    std::string_view materialPath, std::uint32_t imageWidth, std::uint32_t imageHeight, std::uint32_t cell, std::string* error) {
    const auto fail = [error](const char* text) -> std::optional<kb::assets::Map2DAsset> {
        if (error != nullptr) *error = text;
        return std::nullopt;
    };
    if (texturePath.empty()) return fail("the sprite has no image");
    const auto layout = Sprite2DLayout(sprite, imageWidth, imageHeight);
    if (!layout) return fail("the image is smaller than one sprite cell");
    if (cell >= layout->columns * layout->rows) return fail("the sprite cell lies outside the image");
    kb::assets::Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(1U, 1U);
    map.background = false;
    map.floorHeight = 0.0F;
    map.tileSize = static_cast<float>(layout->cellWidth) / sprite.pixelsPerUnit;
    map.orientation = sprite.wall ? kb::assets::Map2DOrientation::Wall : kb::assets::Map2DOrientation::Flat;
    map.sheets.push_back(kb::assets::Map2DSheet{
        .texture = std::string{ texturePath },
        .material = std::string{ materialPath },
        .cellSize = layout->cellWidth,
        .columns = layout->columns,
        .rows = layout->rows,
        .cellHeight = layout->cellHeight == layout->cellWidth ? 0U : layout->cellHeight,
        .margin = sprite.margin,
        .spacing = sprite.spacing,
        .imageWidth = imageWidth,
        .imageHeight = imageHeight,
    });
    // The pivot is a fraction of the sprite from its top left; a flipped sprite mirrors about the pivot.
    const float pivotX = sprite.flipX ? 1.0F - sprite.pivotX : sprite.pivotX;
    const float pivotY = sprite.flipY ? 1.0F - sprite.pivotY : sprite.pivotY;
    const float height = static_cast<float>(layout->cellHeight) / static_cast<float>(layout->cellWidth);
    kb::assets::Map2DSprite tile{};
    tile.sheet = 0U;
    tile.cell = cell;
    tile.flipX = sprite.flipX;
    tile.flipY = sprite.flipY;
    tile.tint = PackTint(sprite);
    tile.offsetX = 0.5F - pivotX;
    tile.offsetY = height * (1.0F - pivotY) - (sprite.wall ? 0.0F : 0.5F);
    map.tiles[kb::assets::Map2DTileKey(0U, 0U, 0)].items.push_back(tile);
    if (!kb::assets::IsMap2DAssetValid(map, error)) return std::nullopt;
    return map;
}

bool AssignSprite2DImage(kb::scene::Scene& scene, SceneEntity entity, const AssetMetadata* image) {
    Sprite2DComponent* sprite = scene.Components().Sprites2D().TryGet(entity);
    if (sprite == nullptr) return false;
    sprite->imageAssetId = image == nullptr ? 0U : image->id.value;
    sprite->imageWidth = 0U;
    sprite->imageHeight = 0U;
    if (image != nullptr && !image->physicalPath.empty()) {
        if (const auto size = ReadMap2DImageSize(image->physicalPath)) {
            sprite->imageWidth = size->first;
            sprite->imageHeight = size->second;
        }
    }
    scene.Components().Sprites2D().MarkModified(entity);
    return true;
}

struct Sprite2DRuntime::State {
    struct Key {
        Sprite2DComponent sprite{};
        std::uint32_t cell = 0U;
        std::uint32_t imageWidth = 0U;
        std::uint32_t imageHeight = 0U;
        std::string texture;
        std::string material;
        bool operator==(const Key&) const = default;
    };
    struct Item {
        SceneEntity visual{};
        AssetId mesh{};
        std::optional<Key> applied;
        double clock = 0.0;
        float seenTime = -1.0F;
        std::uint32_t cell = 0U;
        std::uint64_t version = 0U;
        std::string reported;
    };
    std::map<SceneEntity, Item> items;
    std::map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> imageSizes;
    std::size_t rebuilt = 0U;

    static void Release(kb::scene::Scene& scene, Item& item) {
        if (scene.Entities().IsAlive(item.visual)) scene.Entities().Destroy(item.visual);
        if (item.mesh.IsValid()) static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(item.mesh));
        item.visual = {};
        item.mesh = {};
        item.applied.reset();
    }
};

Sprite2DRuntime::Sprite2DRuntime() : state_(std::make_unique<State>()) {}
Sprite2DRuntime::~Sprite2DRuntime() = default;

std::size_t Sprite2DRuntime::RebuiltLastUpdate() const noexcept { return state_->rebuilt; }

SceneEntity Sprite2DRuntime::VisualOf(SceneEntity owner) const {
    const auto found = state_->items.find(owner);
    return found == state_->items.end() ? SceneEntity{} : found->second.visual;
}

std::optional<std::uint32_t> Sprite2DRuntime::CurrentCell(SceneEntity owner) const {
    const auto found = state_->items.find(owner);
    if (found == state_->items.end() || !found->second.applied) return std::nullopt;
    return found->second.cell;
}

void Sprite2DRuntime::OnUpdate(kb::scene::SceneSystemContext& context) {
    auto& scene = context.GetScene();
    auto& assets = scene.Assets().Manager();
    state_->rebuilt = 0U;
    struct Owner { SceneEntity entity; Sprite2DComponent sprite; };
    std::vector<Owner> owners;
    context.EcsWorld().CreateQuery<Sprite2DComponent>().ForEach(
        [](SceneEntity entity, const Sprite2DComponent& value, void* output) {
            static_cast<std::vector<Owner>*>(output)->push_back(Owner{ entity, value });
        }, &owners);

    for (auto it = state_->items.begin(); it != state_->items.end();) {
        const bool alive = scene.Entities().IsAlive(it->first) &&
            std::ranges::any_of(owners, [&](const Owner& owner) { return owner.entity == it->first; });
        if (alive) { ++it; continue; }
        State::Release(scene, it->second);
        it = state_->items.erase(it);
    }

    // A host without a mesh loader (a headless run with no renderer) has nothing to draw with; the clocks still run.
    const bool canDraw = assets.HasLoaderForType("RenderMesh");
    const bool playing = scene.Runtime().IsPlaying();
    const double step = playing ? std::max(0.0F, context.DeltaSeconds()) : 0.0;
    for (const Owner& owner : owners) {
        State::Item& item = state_->items[owner.entity];
        const Sprite2DComponent& sprite = owner.sprite;
        const auto report = [&](const std::string& text) {
            if (item.reported == text) return;
            item.reported = text;
            context.ReportError("Sprite 2D: " + text);
        };
        if (!kb::scene::IsSprite2DComponentValid(sprite)) { report("the sprite holds an invalid value"); State::Release(scene, item); continue; }
        if (sprite.time != item.seenTime) { item.clock = sprite.time; item.seenTime = sprite.time; }
        if (sprite.playing && sprite.frameCount > 1U) item.clock += step;
        if (!sprite.visible || !canDraw) { State::Release(scene, item); item.reported.clear(); continue; }

        const AssetMetadata* image = assets.Registry().Find(AssetId{ sprite.imageAssetId });
        if (image == nullptr) {
            // A new sprite has no picture yet, which is not an error; a picture that is not registered is.
            if (sprite.imageAssetId != 0U) report("the image asset is not registered");
            State::Release(scene, item);
            continue;
        }
        std::uint32_t width = sprite.imageWidth;
        std::uint32_t height = sprite.imageHeight;
        if (width == 0U || height == 0U) {
            const auto known = state_->imageSizes.find(sprite.imageAssetId);
            if (known != state_->imageSizes.end()) {
                width = known->second.first;
                height = known->second.second;
            } else {
                if (!image->physicalPath.empty()) {
                    if (const auto size = ReadMap2DImageSize(image->physicalPath)) { width = size->first; height = size->second; }
                }
                if (width == 0U || height == 0U) {
                    const auto texture = assets.Load<kb::render::RenderTextureAssetData>(image->id);
                    if (texture.IsLoaded()) { width = texture->width; height = texture->height; }
                }
                if (width != 0U && height != 0U) state_->imageSizes[sprite.imageAssetId] = { width, height };
            }
        }
        if (width == 0U || height == 0U) { report("the size of the image is not known (set imageWidth and imageHeight)"); State::Release(scene, item); continue; }

        std::string materialPath;
        if (sprite.materialAssetId != 0U) {
            if (const AssetMetadata* material = assets.Registry().Find(AssetId{ sprite.materialAssetId })) {
                materialPath = kb::assets::NormalizeAssetPath(material->virtualPath);
            }
        }
        State::Key key;
        key.sprite = sprite;
        key.sprite.time = 0.0F;
        key.cell = Sprite2DFrameCell(sprite, item.clock);
        key.imageWidth = width;
        key.imageHeight = height;
        key.texture = kb::assets::NormalizeAssetPath(image->virtualPath);
        key.material = materialPath;
        item.cell = key.cell;
        const bool meshAlive = item.mesh.IsValid() && scene.Entities().IsAlive(item.visual) &&
            assets.AcquireLoaded<kb::render::RenderMeshAssetData>(item.mesh).IsLoaded();
        if (item.applied && *item.applied == key && meshAlive) continue;

        std::string error;
        auto map = BuildSprite2DMap(sprite, key.texture, key.material, width, height, key.cell, &error);
        if (!map) { report(error); State::Release(scene, item); continue; }
        auto mesh = kb::render::RenderMap2DMeshBuilder::Build(*map, static_cast<float>(sprite.sortingOrder) * kSortingOrderLift, 0.0);
        if (!mesh) { report("the sprite geometry could not be built"); continue; }
        if (!scene.Entities().IsAlive(item.visual)) {
            item.visual = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
                .name = "Sprite 2D Visual", .parent = scene.Entities().Object(owner.entity),
                .visibility = { .mode = kb::scene::VisibilityMode::Inherit } });
        }
        if (!item.mesh.IsValid()) {
            item.mesh = kb::assets::MakeAssetId("sprite2d-runtime:" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ":" + std::to_string(owner.entity.Id()));
        }
        const std::string path = "/Runtime/Sprite2D/" + std::to_string(item.mesh.value);
        if (!assets.RegisterGeneratedAsset(AssetMetadata{ .id = item.mesh, .type = "RenderMesh", .virtualPath = path, .contentHash = ++item.version }) ||
            !assets.PublishRuntimeAsset(item.mesh, std::make_shared<kb::render::RenderMeshAssetData>(std::move(*mesh)))) {
            report("the sprite mesh could not be published: " + assets.LastError());
            continue;
        }
        scene.Components().MeshRenderers().Set(item.visual,
            kb::scene::MeshRendererComponent{ .meshAssetId = item.mesh.value, .castsShadow = false, .receivesShadow = true, .transient = true });
        item.applied = key;
        item.reported.clear();
        ++state_->rebuilt;
    }
}

void Sprite2DRuntime::OnDestroy(kb::scene::SceneSystemContext& context) {
    auto& scene = context.GetScene();
    for (auto& [owner, item] : state_->items) {
        static_cast<void>(owner);
        State::Release(scene, item);
    }
    state_->items.clear();
}

} // namespace kb::world2d
