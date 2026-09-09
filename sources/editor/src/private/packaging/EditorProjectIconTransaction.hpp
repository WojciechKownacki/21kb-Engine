#pragma once

#include <filesystem>
#include <string>

namespace kb::editor {

class EditorProjectIconTransaction final {
public:
    EditorProjectIconTransaction() = default;
    ~EditorProjectIconTransaction();

    EditorProjectIconTransaction(const EditorProjectIconTransaction&) = delete;
    EditorProjectIconTransaction& operator=(const EditorProjectIconTransaction&) = delete;

    [[nodiscard]] bool Publish(
        const std::filesystem::path& source,
        const std::filesystem::path& projectRoot,
        std::string& error);
    [[nodiscard]] bool Rollback(std::string& error);
    void Commit() noexcept;

private:
    std::filesystem::path projectRoot_;
    std::filesystem::path destination_;
    std::filesystem::path backup_;
    bool active_ = false;
    bool hadPrevious_ = false;
};

} // namespace kb::editor
