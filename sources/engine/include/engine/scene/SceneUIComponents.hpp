#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/ui/UIComponentCatalog.hpp"
#include "engine/ui/layout/UIAspectRatioFitter.hpp"

#include <cstddef>
#include <cstdint>

namespace kb::scene {

class Scene;

class SceneUIComponentQueries {
public:
    explicit SceneUIComponentQueries(const Scene& scene) noexcept;

    template <SceneUIComponent T>
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept {
        return HasRaw(entity, UIComponentTypeOf<T>::value);
    }

    template <SceneUIComponent T>
    [[nodiscard]] const T* TryGet(SceneEntity entity) const noexcept {
        return static_cast<const T*>(TryGetRaw(entity, UIComponentTypeOf<T>::value));
    }

private:
    [[nodiscard]] bool HasRaw(SceneEntity entity, UIComponentType type) const noexcept;
    [[nodiscard]] const void* TryGetRaw(SceneEntity entity, UIComponentType type) const noexcept;

    const Scene& scene_;
};

class SceneUIComponents {
public:
    explicit SceneUIComponents(Scene& scene) noexcept;

    template <SceneUIComponent T>
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept {
        return HasRaw(entity, UIComponentTypeOf<T>::value);
    }

    template <SceneUIComponent T>
    [[nodiscard]] const T* TryGet(SceneEntity entity) const noexcept {
        return static_cast<const T*>(TryGetRaw(entity, UIComponentTypeOf<T>::value));
    }

    template <SceneUIComponent T>
    [[nodiscard]] T* TryGet(SceneEntity entity) noexcept {
        return static_cast<T*>(TryGetMutableRaw(entity, UIComponentTypeOf<T>::value));
    }

    template <SceneUIComponent T>
    void Set(SceneEntity entity, const T& component) {
        SetRaw(entity, UIComponentTypeOf<T>::value, &component, sizeof(T));
    }

    template <SceneUIComponent T>
    void Remove(SceneEntity entity) noexcept {
        RemoveRaw(entity, UIComponentTypeOf<T>::value);
    }

    template <SceneUIComponent T>
    void MarkModified(SceneEntity entity) noexcept {
        MarkModifiedRaw(entity, UIComponentTypeOf<T>::value);
    }

private:
    [[nodiscard]] bool HasRaw(SceneEntity entity, UIComponentType type) const noexcept;
    [[nodiscard]] const void* TryGetRaw(SceneEntity entity, UIComponentType type) const noexcept;
    [[nodiscard]] void* TryGetMutableRaw(SceneEntity entity, UIComponentType type) noexcept;
    void SetRaw(SceneEntity entity, UIComponentType type, const void* component, std::size_t size);
    void RemoveRaw(SceneEntity entity, UIComponentType type) noexcept;
    void MarkModifiedRaw(SceneEntity entity, UIComponentType type) noexcept;

    Scene& scene_;
};

} // namespace kb::scene
