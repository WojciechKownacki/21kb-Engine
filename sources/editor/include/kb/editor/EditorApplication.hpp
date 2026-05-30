#pragma once

#include <memory>

namespace kb::editor {

class EditorApplication {
public:
    EditorApplication();
    ~EditorApplication();

    EditorApplication(const EditorApplication&) = delete;
    EditorApplication& operator=(const EditorApplication&) = delete;
    EditorApplication(EditorApplication&&) = delete;
    EditorApplication& operator=(EditorApplication&&) = delete;

    bool Initialize();
    void Run();
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kb::editor
