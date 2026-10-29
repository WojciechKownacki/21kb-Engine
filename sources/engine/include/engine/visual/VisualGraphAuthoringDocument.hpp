#pragma once

#include "engine/visual/VisualGraphDocument.hpp"
#include "engine/visual/VisualGraphDiagnostic.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kb::visual {
struct VisualGraphNodePosition {
    std::uint32_t nodeId = 0;
    float x = 0, y = 0;
};
struct VisualGraphAuthoringSnapshot {
    VisualGraphAsset graph;
    std::vector<VisualGraphNodePosition> positions;
};

// The editor and command tools share edit validation, history and publication.
class VisualGraphAuthoringDocument final {
public:
    void Create(std::string name);
    [[nodiscard]] bool Load(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool Save(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] const VisualGraphAuthoringSnapshot& Snapshot() const noexcept;
    [[nodiscard]] bool Dirty() const noexcept;
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool Undo() noexcept;
    [[nodiscard]] bool Redo() noexcept;
    void BeginGroup() noexcept;
    void EndGroup() noexcept;
    [[nodiscard]] std::vector<VisualGraphDiagnostic> Validate() const;
    [[nodiscard]] std::uint32_t AddNode(const VisualGraphNodeCatalogEntry& entry, float x, float y, std::string& error);
    [[nodiscard]] bool RemoveNode(std::uint32_t id, std::string& error);
    [[nodiscard]] bool MoveNode(std::uint32_t id, float x, float y, std::string& error);
    [[nodiscard]] bool SetLiteral(std::uint32_t id, std::string_view text, std::string& error);
    [[nodiscard]] bool SetEvent(std::uint32_t id, VisualGraphLifecycleEvent event, std::string& error);
    [[nodiscard]] bool SetSymbol(std::uint32_t id, std::string text, std::string& error);
    [[nodiscard]] bool Connect(std::uint32_t from, std::string fromPin, std::uint32_t to, std::string toPin, std::string& error);
    [[nodiscard]] bool Disconnect(std::size_t index, std::string& error);
private:
    [[nodiscard]] bool Apply(const std::function<bool(VisualGraphAuthoringSnapshot&)>& edit, std::string& error);
    std::vector<VisualGraphAuthoringSnapshot> history_;
    std::size_t cursor_ = 0;
    std::optional<std::size_t> saved_;
    bool grouped_ = false, groupEdited_ = false;
};
} // namespace kb::visual
