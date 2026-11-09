#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace kb::script {

// A short how-to for one kind of task ("a cube with a material", "physics"), written for coding
// agents: the commands that do it, in order, and what to check. An agent reads the one guide its
// task needs instead of a whole manual, which keeps every request small.
struct ScriptAgentGuide {
    std::string_view name;     // file and topic name, e.g. "objects-and-materials"
    std::string_view title;
    std::string_view summary;  // one line for the index
    std::string_view body;     // Markdown
};

class ScriptAgentGuides final {
public:
    ScriptAgentGuides() = delete;

    [[nodiscard]] static std::span<const ScriptAgentGuide> All() noexcept;
    // The index: every guide's name and summary.
    [[nodiscard]] static std::string Index();
    // A guide by name, without regard to case; empty or "index" gives the index.
    [[nodiscard]] static std::optional<std::string> Topic(std::string_view name);
};

} // namespace kb::script
