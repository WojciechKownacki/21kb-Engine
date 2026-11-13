#pragma once

#include <chrono>
#include <cstdint>

namespace kb::render {

// How a submit brings the packaged meshes and textures a scene uses to the GPU the first time it needs them.
struct RuntimeResourceLoadingSettings {
    // false: a resource is read, decoded and created inside the submit that first references it, so a scene is
    // complete after one submit (tests, one-shot captures). true: the read and decode run on the background load
    // workers and the submit never waits for them; a resource that is not ready is left unbound and its draw is
    // skipped until it is, which a loading screen waits for through RuntimeSceneLoadProgress.
    bool asynchronous = false;
    // Time one submit may spend creating GPU resources whose data is ready; the rest wait for the next submit. The
    // first resource of a submit is always created, so loading always advances. Used when asynchronous.
    double creationBudgetMilliseconds = 4.0;
    // Resources one submit may create, whatever time is left. Creating a resource is cheap for the submit, but the
    // driver uploads it and prepares its first draw in the frame that follows, so this keeps that frame short on
    // slower GPUs. 0 means no limit. Used when asynchronous.
    std::uint32_t maxCreationsPerFrame = 64U;
};

// Where the meshes and textures a scene references stand after its last submit.
struct RuntimeSceneLoadProgress {
    // Referenced resources that have their GPU resource.
    std::uint32_t resolved = 0U;
    // Referenced resources still being read or decoded, or waiting for their turn in the frame's creation budget.
    std::uint32_t waiting = 0U;

    [[nodiscard]] constexpr bool Complete() const noexcept {
        return waiting == 0U;
    }
    // 1 when nothing is waiting.
    [[nodiscard]] constexpr float Fraction() const noexcept {
        const std::uint32_t total = resolved + waiting;
        return total == 0U ? 1.0F : static_cast<float>(resolved) / static_cast<float>(total);
    }
};

// What one submit's resource pass has spent and found. Cheap: the clock is read only when a resource is ready to be
// created.
class RuntimeResourceCreationBudget {
public:
    // Unlimited and synchronous.
    RuntimeResourceCreationBudget() = default;
    explicit RuntimeResourceCreationBudget(const RuntimeResourceLoadingSettings& settings)
        : asynchronous_{ settings.asynchronous },
          maxCreations_{ settings.maxCreationsPerFrame },
          deadline_{ std::chrono::steady_clock::now() +
              std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                  std::chrono::duration<double, std::milli>(settings.creationBudgetMilliseconds)) } {}

    [[nodiscard]] bool Asynchronous() const noexcept {
        return asynchronous_;
    }
    // True while another GPU resource may be created in this submit.
    [[nodiscard]] bool MayCreate() const noexcept {
        return !asynchronous_ || created_ == 0U ||
            ((maxCreations_ == 0U || created_ < maxCreations_) && std::chrono::steady_clock::now() < deadline_);
    }
    void NoteCreated() noexcept {
        ++created_;
        ++resolved_;
    }
    void NoteResolved() noexcept {
        ++resolved_;
    }
    void NoteWaiting() noexcept {
        ++waiting_;
    }
    [[nodiscard]] RuntimeSceneLoadProgress Progress() const noexcept {
        return RuntimeSceneLoadProgress{ .resolved = resolved_, .waiting = waiting_ };
    }

private:
    bool asynchronous_ = false;
    std::uint32_t maxCreations_ = 0U;
    std::chrono::steady_clock::time_point deadline_{};
    std::uint32_t created_ = 0U;
    std::uint32_t resolved_ = 0U;
    std::uint32_t waiting_ = 0U;
};

} // namespace kb::render
