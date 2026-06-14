#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <memory>

namespace kb::editor {

void EditorSceneBgfxViewport::ViewportSessionStore::Clear() noexcept {
    sessions_.clear();
    nextViewportIndex_ = 0;
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::ViewportSessionStore::Ensure(
    HWND host,
    std::uint64_t key,
    std::uint32_t maxViewportIndex) {
    if (host == nullptr) {
        return nullptr;
    }

    if (ViewportSession* existing = key == 0U ? Find(host, key) : FindByKey(key); existing != nullptr) {
        existing->host = host;
        return existing;
    }

    const std::uint32_t viewportIndex = nextViewportIndex_;
    if (viewportIndex > maxViewportIndex) {
        return nullptr;
    }
    ++nextViewportIndex_;

    std::unique_ptr<ViewportSession> session = std::make_unique<ViewportSession>();
    session->host = host;
    session->key = key;
    session->viewportIndex = viewportIndex;
    sessions_.push_back(std::move(session));
    return sessions_.back().get();
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::ViewportSessionStore::Find(HWND host, std::uint64_t key) noexcept {
    if (host == nullptr) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(sessions_, [host, key](const std::unique_ptr<ViewportSession>& session) {
        return session != nullptr && session->host == host && session->key == key;
    });
    return iter == sessions_.end() ? nullptr : iter->get();
}

EditorSceneBgfxViewport::ViewportSession* EditorSceneBgfxViewport::ViewportSessionStore::FindByKey(std::uint64_t key) noexcept {
    if (key == 0U) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(sessions_, [key](const std::unique_ptr<ViewportSession>& session) {
        return session != nullptr && session->key == key;
    });
    return iter == sessions_.end() ? nullptr : iter->get();
}

void EditorSceneBgfxViewport::ViewportSessionStore::MarkHostNotPresented(HWND host) noexcept {
    for (const std::unique_ptr<ViewportSession>& session : sessions_) {
        if (session != nullptr && session->host == host) {
            session->presentedInCurrentPaint = false;
        }
    }
}

void EditorSceneBgfxViewport::ViewportSessionStore::MarkAllNotPresented() noexcept {
    for (const std::unique_ptr<ViewportSession>& session : sessions_) {
        if (session != nullptr) {
            session->presentedInCurrentPaint = false;
        }
    }
}

void EditorSceneBgfxViewport::ViewportSessionStore::ShutdownFramebuffers() noexcept {
    for (const std::unique_ptr<ViewportSession>& session : sessions_) {
        if (session != nullptr) {
            session->sceneTarget.Shutdown();
            session->postProcessTargets.Shutdown();
        }
    }
}

void EditorSceneBgfxViewport::ViewportSessionStore::ResetSubmittedSceneRevisions() noexcept {
    for (const std::unique_ptr<ViewportSession>& session : sessions_) {
        if (session != nullptr) {
            session->submittedSceneRevision = 0U;
        }
    }
}

} // namespace kb::editor

#endif
