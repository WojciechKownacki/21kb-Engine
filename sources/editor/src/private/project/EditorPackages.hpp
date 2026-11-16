#pragma once

#include "kb/packages/Packages.hpp"

#include <cstddef>
#include <filesystem>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {

// One line of the Packages panel: a package of the registry or of the project.
struct EditorPackageRow {
    std::string name;
    // The installed version; empty when the project does not have it.
    std::string installed;
    // The newest version the registry has; empty when the registry does not list the package.
    std::string latest;
    // A short sentence for a person: what asked for it, or that a newer version can be installed.
    std::string note;
    // The project asked for it (so Remove applies); false for a package only another one needs.
    bool asked = false;
    bool updatable = false;
};

// What the open project knows about the signature of its registry's index.
enum class EditorRegistryTrust { Unknown, Unsigned, Signed };

// What the Packages panel shows and does: the packages of the registry and of the open project, and the operations on
// them (add, update, remove, verify, set the registry), each run in the background by the shared package library.
// The editor has one (Instance); the project it shows is the open one.
class EditorPackages final {
public:
    [[nodiscard]] static EditorPackages& Instance();

    // Asks for a fresh reading of the registry and the project as soon as no operation is running.
    void Refresh() noexcept { refreshRequested_ = true; }
    // Takes a finished operation and starts the reading that was asked for. True when what the panel shows changed.
    [[nodiscard]] bool Pump(const std::filesystem::path& projectRoot);

    [[nodiscard]] bool Busy() const noexcept { return job_.valid(); }
    [[nodiscard]] const std::vector<EditorPackageRow>& Rows() const noexcept { return rows_; }
    // The registry as packages.json names it; empty when none is set.
    [[nodiscard]] const std::string& Registry() const noexcept { return registry_; }
    // Signed: the index carries a valid signature by a key packages.json trusts. Unsigned: the project trusts no key, so
    // the registry is used as it is. Unknown: no registry, or it was refused.
    [[nodiscard]] EditorRegistryTrust RegistryTrust() const noexcept { return trust_; }
    // What the last operation did, or why it was refused, in plain words.
    [[nodiscard]] const std::string& Message() const noexcept { return message_; }
    [[nodiscard]] bool MessageIsError() const noexcept { return messageIsError_; }
    // The files verify found changed, one line each, with the result of the last verify.
    [[nodiscard]] const std::vector<std::string>& Problems() const noexcept { return problems_; }

    // True once after an operation changed the project's native modules: the scene is then rebuilt with them, which also
    // lets go of a module that was removed.
    [[nodiscard]] bool TakeModulesChanged() noexcept { return std::exchange(modulesChanged_, false); }

    // The operations; each is ignored while another runs.
    void Add(std::string name);
    // An empty name moves every installed package.
    void Update(std::string name);
    void Remove(std::string name);
    void Verify();
    void SetRegistry(std::string location);

    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    [[nodiscard]] std::size_t Selected() const noexcept { return selected_; }
    void Select(std::size_t index) noexcept { selected_ = index; }
    int scroll = 0;

private:
    enum class Operation { Read, Add, Update, Remove, Verify, SetRegistry };

    // Everything a finished operation hands to the panel.
    struct Outcome {
        std::vector<EditorPackageRow> rows;
        std::string registry;
        EditorRegistryTrust trust = EditorRegistryTrust::Unknown;
        std::string message;
        bool isError = false;
        std::vector<std::string> problems;
        // An operation replaces what the panel said before; a plain reading only fills the lists.
        bool replacesMessage = false;
        bool modulesChanged = false;
    };

    void Start(Operation operation, std::string argument);
    [[nodiscard]] static Outcome Run(const std::filesystem::path& projectRoot, Operation operation, const std::string& argument);

    std::filesystem::path root_;
    std::vector<EditorPackageRow> rows_;
    std::string registry_;
    EditorRegistryTrust trust_ = EditorRegistryTrust::Unknown;
    std::string message_;
    bool messageIsError_ = false;
    std::vector<std::string> problems_;
    bool refreshRequested_ = true;
    bool modulesChanged_ = false;
    std::size_t selected_ = kNone;
    std::future<Outcome> job_;
};

} // namespace kb::editor
