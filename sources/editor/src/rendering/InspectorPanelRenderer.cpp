#include "rendering/InspectorPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

#include <cstdio>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] const char* LightKindName(kb::scene::LightKind kind) noexcept {
    switch (kind) {
    case kb::scene::LightKind::Directional:
        return "Directional";
    case kb::scene::LightKind::Point:
        return "Point";
    case kb::scene::LightKind::Spot:
        return "Spot";
    }
    return "Unknown";
}

[[nodiscard]] const char* ProjectionName(kb::scene::CameraProjection projection) noexcept {
    switch (projection) {
    case kb::scene::CameraProjection::Perspective:
        return "Perspective";
    case kb::scene::CameraProjection::Orthographic:
        return "Orthographic";
    }
    return "Unknown";
}

} // namespace

void InspectorPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().IsAlive(selected)) {
        GdiDrawing::DrawTextBlock(dc, content, "No entity selected", GdiDrawing::ToColorRef(theme.textDisabled));
        return;
    }

    const kb::scene::TransformComponent transform = sceneContext.Scene().Transform(selected);
    const kb::scene::VisibilityComponent visibility = sceneContext.Scene().Visibility(selected);

    char buffer[1024]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "Entity: %s\nId: %llu\nVisible: %s\n\nTransform\nPosition: %.2f, %.2f, %.2f\nRotation: %.2f, %.2f, %.2f, %.2f\nScale: %.2f, %.2f, %.2f",
        sceneContext.Scene().Name(selected).c_str(),
        static_cast<unsigned long long>(selected.Id()),
        visibility.visible ? "true" : "false",
        transform.localPosition.x,
        transform.localPosition.y,
        transform.localPosition.z,
        transform.localRotation.x,
        transform.localRotation.y,
        transform.localRotation.z,
        transform.localRotation.w,
        transform.localScale.x,
        transform.localScale.y,
        transform.localScale.z);

    std::string text = buffer;

    if (const kb::scene::CameraComponent* camera = sceneContext.Scene().TryGetCamera(selected); camera != nullptr) {
        char component[256]{};
        std::snprintf(
            component,
            sizeof(component),
            "\n\nCamera\nProjection: %s\nFOV: %.1f\nClip: %.2f - %.1f\nPrimary: %s",
            ProjectionName(camera->projection),
            camera->verticalFovDegrees,
            camera->nearClip,
            camera->farClip,
            camera->primary ? "true" : "false");
        text += component;
    }

    if (const kb::scene::MeshRendererComponent* renderer = sceneContext.Scene().TryGetMeshRenderer(selected); renderer != nullptr) {
        char component[256]{};
        std::snprintf(
            component,
            sizeof(component),
            "\n\nMesh Renderer\nMesh: %llu\nMaterial: %llu\nCasts shadow: %s\nReceives shadow: %s",
            static_cast<unsigned long long>(renderer->meshAssetId),
            static_cast<unsigned long long>(renderer->materialAssetId),
            renderer->castsShadow ? "true" : "false",
            renderer->receivesShadow ? "true" : "false");
        text += component;
    }

    if (const kb::scene::LightComponent* light = sceneContext.Scene().TryGetLight(selected); light != nullptr) {
        char component[256]{};
        std::snprintf(
            component,
            sizeof(component),
            "\n\nLight\nKind: %s\nIntensity: %.2f\nRange: %.2f",
            LightKindName(light->kind),
            light->intensity,
            light->range);
        text += component;
    }

    GdiDrawing::DrawTextBlock(dc, content, text.c_str(), GdiDrawing::ToColorRef(theme.textSecondary));
}

} // namespace kb::editor

#endif
