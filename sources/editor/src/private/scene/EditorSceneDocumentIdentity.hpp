#pragma once

#include <cstdint>

namespace kb::editor {

class EditorSceneDocumentIdentity {
public:
    [[nodiscard]] std::uint64_t Generation() const noexcept {
        return generation_;
    }

    void Advance() noexcept {
        ++generation_;
        if (generation_ == 0U) {
            generation_ = 1U;
        }
    }

private:
    std::uint64_t generation_ = 1U;
};

} // namespace kb::editor
