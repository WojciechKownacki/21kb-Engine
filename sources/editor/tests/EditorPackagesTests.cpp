#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "kb/packages/Packages.hpp"
#include "project/EditorPackages.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

// The model of the Packages panel, without a window: the lists, Add, Update, Remove and Verify through the shared package
// library, and a refusal that names both requirements and changes nothing.
namespace {

namespace fs = std::filesystem;
using kb::editor::EditorPackageRow;
using kb::editor::EditorPackages;

void Check(bool condition, const std::string& message) {
    kb::editor::tests::Require(condition, message.c_str());
}

void WriteFile(const fs::path& path, const std::string& text) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

// A package published into the registry folder with the library the panel uses.
void Publish(const fs::path& registry, const std::string& name, const std::string& version, const std::string& dependencies) {
    const fs::path source = registry.parent_path() / "Sources" / name / version;
    WriteFile(source / "package.json", "{\"name\":\"" + name + "\",\"version\":\"" + version + "\",\"dependencies\":{" + dependencies + "}}");
    WriteFile(source / "Data" / "item.txt", name + " " + version);
    kb::packages::IndexEntry entry;
    std::string error;
    Check(kb::packages::Publish(registry, source, entry, error), "publishing " + name + " " + version + " failed: " + error);
}

// Pumps the model until its operation has finished.
void Settle(EditorPackages& model, const fs::path& project) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 60 };
    do {
        static_cast<void>(model.Pump(project));
        if (!model.Busy()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
    } while (std::chrono::steady_clock::now() < deadline);
    Check(false, "a package operation did not finish");
}

[[nodiscard]] const EditorPackageRow* Row(const EditorPackages& model, const std::string& name) {
    for (const EditorPackageRow& row : model.Rows()) {
        if (row.name == name) {
            return &row;
        }
    }
    return nullptr;
}

[[nodiscard]] bool Has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

namespace kb::editor::tests {

void RunEditorPackagesTests() {
    const fs::path base = fs::temp_directory_path() / "21kb_editor_packages_test";
    std::error_code error;
    fs::remove_all(base, error);
    const fs::path project = base / "Project";
    const fs::path registry = base / "Registry";
    fs::create_directories(project, error);
    Publish(registry, "edit.a", "1.0.0", "\"edit.b\":\"~1.0\"");
    Publish(registry, "edit.b", "1.0.3", "");
    Publish(registry, "edit.b", "2.0.0", "");
    Publish(registry, "edit.c", "1.0.0", "\"edit.b\":\"^2\"");

    EditorPackages model;
    Settle(model, project);
    Check(model.Rows().empty() && model.Registry().empty(), "a project without packages.json shows no packages and no registry");
    // Add before a registry is set says what to do.
    model.Add("edit.a");
    Settle(model, project);
    Check(model.MessageIsError() && Has(model.Message(), "registry"), "Add without a registry must ask for one: " + model.Message());

    model.SetRegistry(registry.string());
    Settle(model, project);
    const EditorPackageRow* offered = Row(model, "edit.c");
    Check(!model.Registry().empty() && model.Rows().size() == 3U && offered != nullptr && offered->installed.empty() && offered->latest == "1.0.0" &&
            Row(model, "edit.b")->latest == "2.0.0",
        "the panel must list what the registry offers");

    model.Add("edit.a");
    Settle(model, project);
    const EditorPackageRow* added = Row(model, "edit.a");
    const EditorPackageRow* needed = Row(model, "edit.b");
    Check(!model.MessageIsError() && Has(model.Message(), "Installed edit.a 1.0.0, edit.b 1.0.3") && added != nullptr && added->installed == "1.0.0" && added->asked &&
            needed != nullptr && needed->installed == "1.0.3" && !needed->asked && Has(needed->note, "edit.a") &&
            fs::exists(project / "Assets" / "Packages" / "edit.b" / "Data" / "item.txt"),
        "Add must install the package and what it needs, and say so: " + model.Message());

    // A conflict is explained in plain words and the project stays as it was.
    kb::packages::Lock before;
    std::string readError;
    Check(kb::packages::ReadLock(project, before, readError), readError);
    model.Add("edit.c");
    Settle(model, project);
    kb::packages::Lock after;
    Check(kb::packages::ReadLock(project, after, readError), readError);
    Check(model.MessageIsError() && Has(model.Message(), "edit.a 1.0.0 needs edit.b ~1.0") && Has(model.Message(), "edit.c 1.0.0 needs edit.b ^2") &&
            Has(model.Message(), "Nothing was changed") && before == after && !fs::exists(project / "Assets" / "Packages" / "edit.c") && Row(model, "edit.c")->installed.empty(),
        "a conflict must name both requirements and change nothing: " + model.Message());

    // Update takes a newer version the project's range allows.
    Publish(registry, "edit.a", "1.1.0", "\"edit.b\":\"~1.0\"");
    model.Refresh();
    Settle(model, project);
    Check(Row(model, "edit.a")->updatable && Row(model, "edit.a")->latest == "1.1.0" && !Row(model, "edit.b")->updatable, "a newer version in range must be offered");
    model.Update({});
    Settle(model, project);
    Check(Has(model.Message(), "Updated edit.a 1.0.0 -> 1.1.0") && Row(model, "edit.a")->installed == "1.1.0" && !Row(model, "edit.a")->updatable, "Update must install the newer version: " + model.Message());

    // Verify finds a file changed by hand.
    model.Verify();
    Settle(model, project);
    Check(!model.MessageIsError() && Has(model.Message(), "All 2 packages") && model.Problems().empty(), "verify of untouched packages must pass: " + model.Message());
    WriteFile(project / "Assets" / "Packages" / "edit.b" / "Data" / "item.txt", "changed");
    model.Verify();
    Settle(model, project);
    Check(model.MessageIsError() && model.Problems().size() == 1U && model.Problems()[0] == "modified edit.b/Data/item.txt", "verify must name the changed file: " + model.Message());

    // Remove: a dependency stays while the package that needs it is there.
    model.Remove("edit.b");
    Settle(model, project);
    Check(model.MessageIsError() && Has(model.Message(), "needed by edit.a") && Row(model, "edit.b")->installed == "1.0.3", "a needed package must be kept: " + model.Message());
    model.Remove("edit.a");
    Settle(model, project);
    Check(!model.MessageIsError() && Has(model.Message(), "Removed edit.a 1.1.0, edit.b 1.0.3") && Row(model, "edit.a")->installed.empty() && !fs::exists(project / "Assets" / "Packages"),
        "Remove must take the package and what only it needed: " + model.Message());
    fs::remove_all(base, error);
}

} // namespace kb::editor::tests
