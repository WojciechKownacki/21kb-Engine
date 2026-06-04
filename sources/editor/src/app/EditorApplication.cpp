#include "kb/editor/EditorApplication.hpp"

#if defined(_WIN32)
#include "app/EditorApplicationLifecycle.hpp"
#include "app/EditorApplicationMessageLoop.hpp"
#include "app/EditorApplicationState.hpp"

#include <memory>

namespace kb::editor {

struct EditorApplication::Impl {
    bool Initialize();
    void Run();
    void Shutdown();

    EditorApplicationState state;
};

EditorApplication::EditorApplication()
    : impl_(std::make_unique<Impl>()) {
}

EditorApplication::~EditorApplication() {
    Shutdown();
}

bool EditorApplication::Initialize() {
    return impl_->Initialize();
}

void EditorApplication::Run() {
    impl_->Run();
}

void EditorApplication::Shutdown() {
    if (impl_ != nullptr) {
        impl_->Shutdown();
    }
}

bool EditorApplication::Impl::Initialize() {
    return EditorApplicationLifecycle::Initialize(state);
}

void EditorApplication::Impl::Run() {
    EditorApplicationMessageLoop::Run(state);
}

void EditorApplication::Impl::Shutdown() {
    EditorApplicationLifecycle::Shutdown(state);
}

} // namespace kb::editor

#else

#include <memory>

namespace kb::editor {

struct EditorApplication::Impl {
};

EditorApplication::EditorApplication()
    : impl_(std::make_unique<Impl>()) {
}

EditorApplication::~EditorApplication() = default;

bool EditorApplication::Initialize() {
    return false;
}

void EditorApplication::Run() {
}

void EditorApplication::Shutdown() {
}

} // namespace kb::editor

#endif
