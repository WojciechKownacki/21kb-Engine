#pragma once

#include "engine/assets/AssetId.hpp"

#include <memory>

namespace kb::assets {

template <typename T>
class AssetHandle {
public:
    AssetHandle() = default;

    AssetHandle(AssetId id, std::shared_ptr<const T> asset) noexcept
        : id_(id)
        , asset_(std::move(asset)) {}

    [[nodiscard]] AssetId Id() const noexcept {
        return id_;
    }

    [[nodiscard]] bool IsLoaded() const noexcept {
        return id_.IsValid() && asset_ != nullptr;
    }

    [[nodiscard]] const T* Get() const noexcept {
        return asset_.get();
    }

    [[nodiscard]] const T& operator*() const noexcept {
        return *asset_;
    }

    [[nodiscard]] const T* operator->() const noexcept {
        return asset_.get();
    }

private:
    AssetId id_{};
    std::shared_ptr<const T> asset_{};
};

} // namespace kb::assets
