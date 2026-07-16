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

    [[nodiscard]] std::shared_ptr<const T> Shared() const noexcept {
        return asset_;
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

// LIB-158: a non-owning reference to a runtime asset. Unlike AssetHandle<T>
// (a strong holder that keeps the payload alive), a WeakAssetHandle<T> does
// NOT extend the payload's lifetime — it observes it. Lock() upgrades to a
// strong AssetHandle<T> if the payload is still alive (held by the cache
// under Retain policy, or by some other live AssetHandle), or returns an
// empty handle once every strong holder has dropped (the natural companion
// to AssetUnloadPolicy::ReleaseWhenUnreferenced). Id() survives expiry, so a
// caller can still re-Load the asset by id after its weak reference lapses.
template <typename T>
class WeakAssetHandle {
public:
    WeakAssetHandle() = default;

    explicit WeakAssetHandle(const AssetHandle<T>& handle) noexcept
        : id_(handle.Id())
        , asset_(handle.Shared()) {}

    WeakAssetHandle(AssetId id, std::weak_ptr<const T> asset) noexcept
        : id_(id)
        , asset_(std::move(asset)) {}

    [[nodiscard]] AssetId Id() const noexcept {
        return id_;
    }

    [[nodiscard]] bool Expired() const noexcept {
        return asset_.expired();
    }

    [[nodiscard]] AssetHandle<T> Lock() const noexcept {
        std::shared_ptr<const T> strong = asset_.lock();
        return strong == nullptr ? AssetHandle<T>{} : AssetHandle<T>{ id_, std::move(strong) };
    }

private:
    AssetId id_{};
    std::weak_ptr<const T> asset_{};
};

} // namespace kb::assets
