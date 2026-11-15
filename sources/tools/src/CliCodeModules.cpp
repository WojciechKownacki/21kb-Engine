// kb_cli code modules: the engine's code map as JSON. The modules (the folders of sources/) and the first-level
// subsystems of the big ones are read from the source tree and the CMake files when the command runs: folders,
// include roots, build and test targets, ctest names and the dependencies found in #include lines. Only what a
// tree cannot say (one line of responsibility and a few entry points per entry) is written by hand, in
// sources/tools/code_map.json; `modules --check` fails when that file and the tree disagree.
#include "CliCommands.hpp"

#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::cli {
namespace {

namespace fs = std::filesystem;
using kb::core::JsonValue;

constexpr std::string_view kMapFile = "sources/tools/code_map.json";
constexpr std::size_t kHeadBytes = 16U * 1024U;
constexpr std::size_t kMaxEntryPoints = 8U;
constexpr std::size_t kMaxSpecificTests = 12U;

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lowered{ text };
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

[[nodiscard]] std::string_view Trimmed(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
    return text;
}

[[nodiscard]] bool IsWordCharacter(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] bool ContainsWord(std::string_view text, std::string_view word) noexcept {
    for (std::size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1U)) {
        if ((at == 0U || !IsWordCharacter(text[at - 1U])) && (at + word.size() >= text.size() || !IsWordCharacter(text[at + word.size()]))) {
            return true;
        }
    }
    return false;
}

// Folders that hold output, caches or other people's code, never the engine's own.
[[nodiscard]] bool Skipped(std::string_view name) noexcept {
    return name.starts_with('.') || name == "Saved" || name == "build" || name == "third_party" || name == "__pycache__";
}

[[nodiscard]] std::vector<std::string> ChildDirectories(const fs::path& directory) {
    std::vector<std::string> names;
    std::error_code error;
    for (fs::directory_iterator entry{ directory, error }, end; !error && entry != end; entry.increment(error)) {
        const std::string name = entry->path().filename().string();
        if (entry->is_directory(error) && !Skipped(name)) names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}

[[nodiscard]] bool IsDirectory(const fs::path& path) {
    std::error_code error;
    return fs::is_directory(path, error);
}

[[nodiscard]] bool IsFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

[[nodiscard]] std::string Relative(const fs::path& root, const fs::path& path) {
    return path.lexically_relative(root).generic_string();
}

[[nodiscard]] std::string ReadText(const fs::path& path, std::size_t limit) {
    std::ifstream stream{ path, std::ios::binary };
    std::string text(limit, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(limit));
    text.resize(static_cast<std::size_t>(std::max<std::streamsize>(stream.gcount(), 0)));
    return text;
}

[[nodiscard]] std::vector<std::string> Tokens(std::string_view text, std::string_view separators) {
    std::vector<std::string> tokens;
    std::string token;
    for (const char c : text) {
        if (separators.find(c) != std::string_view::npos) {
            if (!token.empty()) tokens.push_back(std::move(token));
            token.clear();
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(std::move(token));
    return tokens;
}

// ---- CMake: the literal target and test names of a module's CMakeLists.txt ---------------------------------------

struct CmakeNames {
    std::vector<std::string> build;       // libraries and executables that are not tests, in file order
    std::vector<std::string> testTargets; // executables named as tests or benchmarks
    std::vector<std::string> ctest;       // add_test names
};

// The words of the call whose '(' precedes `at`, up to its matching ')'.
[[nodiscard]] std::vector<std::string> CallWords(std::string_view text, std::size_t at) {
    std::vector<std::string> words;
    std::string word;
    int depth = 0;
    bool quoted = false;
    for (; at < text.size(); ++at) {
        const char c = text[at];
        if (c == '"') quoted = !quoted;
        if (!quoted && c == '(') ++depth;
        if (!quoted && c == ')') {
            if (depth == 0) break;
            --depth;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!word.empty()) words.push_back(std::move(word));
            word.clear();
            continue;
        }
        word += c;
    }
    if (!word.empty()) words.push_back(std::move(word));
    return words;
}

[[nodiscard]] bool IsLiteralName(std::string_view name) noexcept {
    return !name.empty() && name.find_first_of("$\"<>") == std::string_view::npos;
}

void AddUnique(std::vector<std::string>& list, const std::string& value) {
    if (std::ranges::find(list, value) == list.end()) list.push_back(value);
}

[[nodiscard]] CmakeNames ReadCmake(const fs::path& file) {
    std::string text;
    {
        std::ifstream stream{ file, std::ios::binary };
        for (std::string line; std::getline(stream, line);) {
            bool quoted = false;
            for (std::size_t at = 0U; at < line.size(); ++at) {
                if (line[at] == '"') quoted = !quoted;
                if (line[at] == '#' && !quoted) {
                    line.resize(at);
                    break;
                }
            }
            text += line;
            text += '\n';
        }
    }
    std::vector<std::string> targets;
    std::set<std::string> executables;
    std::set<std::string> helperTests;
    CmakeNames names;
    for (std::size_t open = text.find('('); open != std::string::npos; open = text.find('(', open + 1U)) {
        std::size_t end = open;
        while (end > 0U && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) --end;
        std::size_t begin = end;
        while (begin > 0U && IsWordCharacter(text[begin - 1U])) --begin;
        const std::string function = Lower(std::string_view{ text }.substr(begin, end - begin));
        if (function != "add_library" && function != "add_executable" && function != "add_test" &&
            !(function.starts_with("kb_add_") && function.find("test") != std::string::npos)) {
            continue;
        }
        const std::vector<std::string> words = CallWords(text, open + 1U);
        if (words.empty()) continue;
        if (function == "add_library" || function == "add_executable") {
            if (!IsLiteralName(words.front())) continue;
            AddUnique(targets, words.front());
            if (function == "add_executable") executables.insert(words.front());
        } else if (function == "add_test") {
            const auto nameAt = std::ranges::find(words, "NAME");
            if (nameAt != words.end() && nameAt + 1 != words.end() && IsLiteralName(*(nameAt + 1))) AddUnique(names.ctest, *(nameAt + 1));
        } else if (IsLiteralName(words.front())) {
            // A helper that creates a test executable and registers a test of the same name.
            AddUnique(targets, words.front());
            executables.insert(words.front());
            helperTests.insert(words.front());
            AddUnique(names.ctest, words.front());
        }
    }
    for (const std::string& target : targets) {
        const std::string lowered = Lower(target);
        const bool isTest = executables.contains(target) &&
            (helperTests.contains(target) || lowered.find("test") != std::string::npos || lowered.find("bench") != std::string::npos);
        AddUnique(isTest ? names.testTargets : names.build, target);
    }
    return names;
}

// `^(kb_engine|kb_ecs)(_|$)`: the first two words of every test name; one pattern that runs the whole group.
[[nodiscard]] std::string CtestPrefixPattern(const std::vector<std::string>& names) {
    std::set<std::string> prefixes;
    for (const std::string& name : names) {
        const std::vector<std::string> words = Tokens(name, "_");
        prefixes.insert(words.size() >= 3U ? words[0] + '_' + words[1] : name);
    }
    std::string pattern;
    for (const std::string& prefix : prefixes) pattern += (pattern.empty() ? "" : "|") + prefix;
    return "^(" + pattern + ")(_|$)";
}

// ---- the tree: modules and subsystems ----------------------------------------------------------------------------

struct Module {
    std::string name; // the folder below sources/, or plugins/<folder>
    fs::path directory;
    std::string includeRoot;      // relative to the engine root; empty when the module has no public headers
    std::string includeNamespace; // the folders every public include starts with, e.g. kb/render
    std::vector<std::string> subsystems;
    std::vector<std::string> directories; // first-level folders
    CmakeNames cmake;
};

struct Entry {
    std::string name; // engine, or engine/scene
    std::size_t module = 0U;
    std::string subsystem;
    std::vector<std::string> directories; // relative to the engine root
    std::set<std::string> dependsOn;
    std::set<std::string> pendingEntryPoints; // check only: entry points not yet seen in the entry's files
};

[[nodiscard]] bool IsSourceFile(const fs::path& path) {
    const std::string extension = Lower(path.extension().string());
    return extension == ".hpp" || extension == ".h" || extension == ".cpp" || extension == ".c" || extension == ".inl" ||
        extension == ".cc" || extension == ".mm";
}

// The namespace folders of the public headers: the single folders every include starts with (engine, kb/render).
[[nodiscard]] std::string IncludeNamespace(const fs::path& includeRoot) {
    std::string result;
    for (fs::path current = includeRoot;;) {
        std::error_code error;
        std::size_t count = 0U;
        std::string only;
        for (fs::directory_iterator entry{ current, error }, end; !error && entry != end; entry.increment(error)) {
            ++count;
            if (entry->is_directory(error)) only = entry->path().filename().string();
        }
        if (count != 1U || only.empty()) return result;
        result += (result.empty() ? "" : "/") + only;
        current /= only;
    }
}

[[nodiscard]] Module ReadModule(const fs::path& root, const fs::path& directory, std::string name) {
    Module module{ .name = std::move(name), .directory = directory };
    module.directories = ChildDirectories(directory);
    if (const fs::path include = directory / "include"; IsDirectory(include)) {
        module.includeRoot = Relative(root, include);
        module.includeNamespace = IncludeNamespace(include);
    }
    // A module with several source folders is described by them too: its first-level subsystems.
    const std::vector<std::string> sourceFolders = ChildDirectories(directory / "src");
    const std::size_t sourceCount = static_cast<std::size_t>(std::ranges::count_if(sourceFolders, [](const std::string& folder) { return folder != "private"; }));
    if (sourceCount >= 2U) {
        std::set<std::string> subsystems;
        for (const std::string& folder : sourceFolders) {
            if (folder != "private") subsystems.insert(folder);
        }
        if (!module.includeNamespace.empty()) {
            for (const std::string& folder : ChildDirectories(directory / "include" / module.includeNamespace)) subsystems.insert(folder);
        }
        module.subsystems.assign(subsystems.begin(), subsystems.end());
    }
    if (IsFile(directory / "CMakeLists.txt")) module.cmake = ReadCmake(directory / "CMakeLists.txt");
    return module;
}

// Every folder of sources/ is a module, except a container (plugins) whose folders with a CMakeLists.txt are the modules.
[[nodiscard]] std::vector<Module> FindModules(const fs::path& root) {
    std::vector<Module> modules;
    const fs::path sources = root / "sources";
    for (const std::string& name : ChildDirectories(sources)) {
        const std::size_t before = modules.size();
        if (!IsFile(sources / name / "CMakeLists.txt")) {
            for (const std::string& child : ChildDirectories(sources / name)) {
                if (IsFile(sources / name / child / "CMakeLists.txt")) modules.push_back(ReadModule(root, sources / name / child, name + '/' + child));
            }
        }
        if (modules.size() == before) modules.push_back(ReadModule(root, sources / name, name));
    }
    std::ranges::sort(modules, {}, &Module::name);
    return modules;
}

[[nodiscard]] std::vector<Entry> BuildEntries(const fs::path& root, const std::vector<Module>& modules) {
    std::vector<Entry> entries;
    for (std::size_t index = 0U; index < modules.size(); ++index) {
        const Module& module = modules[index];
        entries.push_back(Entry{ .name = module.name, .module = index, .directories = { Relative(root, module.directory) } });
        for (const std::string& subsystem : module.subsystems) {
            Entry entry{ .name = module.name + '/' + subsystem, .module = index, .subsystem = subsystem };
            std::vector<fs::path> candidates{ module.directory / "src" / subsystem, module.directory / "src" / "private" / subsystem };
            if (!module.includeNamespace.empty()) candidates.insert(candidates.begin(), module.directory / "include" / module.includeNamespace / subsystem);
            for (const fs::path& candidate : candidates) {
                if (IsDirectory(candidate)) entry.directories.push_back(Relative(root, candidate));
            }
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

// The entry an #include of a public header names: its subsystem when the module has them, else the module.
[[nodiscard]] std::string IncludedEntry(const std::vector<Module>& modules, std::string_view include) {
    for (const Module& module : modules) {
        if (module.includeNamespace.empty() || !include.starts_with(module.includeNamespace) ||
            include.size() <= module.includeNamespace.size() || include[module.includeNamespace.size()] != '/') {
            continue;
        }
        const std::string_view rest = include.substr(module.includeNamespace.size() + 1U);
        const std::size_t slash = rest.find('/');
        if (slash != std::string_view::npos) {
            const std::string subsystem{ rest.substr(0U, slash) };
            if (std::ranges::find(module.subsystems, subsystem) != module.subsystems.end()) return module.name + '/' + subsystem;
        }
        return module.name;
    }
    return {};
}

// The subsystem a file of `module` belongs to, from its path below the module folder.
[[nodiscard]] std::string SubsystemOf(const Module& module, const std::vector<std::string>& parts) {
    if (parts.size() >= 4U && parts[0] == "src" && parts[1] == "private") return parts[2];
    if (parts.size() >= 3U && parts[0] == "src") return parts[1];
    const std::size_t depth = static_cast<std::size_t>(std::ranges::count(module.includeNamespace, '/')) + 1U;
    if (!module.includeNamespace.empty() && parts.size() >= depth + 3U && parts[0] == "include") return parts[1U + depth];
    return {};
}

// One walk over the module's own files (not its tests): the #include lines say what it depends on, and in a
// check the whole file is read for the entry points the map names.
void ScanModule(const std::vector<Module>& modules, std::size_t moduleIndex, std::vector<Entry>& entries, bool check) {
    const Module& module = modules[moduleIndex];
    std::map<std::string, Entry*> byName;
    for (Entry& entry : entries) {
        if (entry.module == moduleIndex) byName[entry.name] = &entry;
    }
    std::error_code error;
    for (fs::recursive_directory_iterator item{ module.directory, fs::directory_options::skip_permission_denied, error }, end;
         !error && item != end; item.increment(error)) {
        const std::string name = item->path().filename().string();
        if (item->is_directory(error)) {
            if (Skipped(name) || name == "tests") item.disable_recursion_pending();
            continue;
        }
        if (!item->is_regular_file(error) || !IsSourceFile(item->path())) continue;
        std::vector<std::string> parts;
        for (const fs::path& part : item->path().lexically_relative(module.directory)) parts.push_back(part.generic_string());
        const std::string subsystem = SubsystemOf(module, parts);
        const std::string subsystemName = subsystem.empty() || std::ranges::find(module.subsystems, subsystem) == module.subsystems.end()
            ? std::string{} : module.name + '/' + subsystem;
        std::error_code sizeError;
        const std::uintmax_t size = fs::file_size(item->path(), sizeError);
        const std::string text = ReadText(item->path(), check && !sizeError ? static_cast<std::size_t>(size) : kHeadBytes);
        std::istringstream lines{ text };
        for (std::string line; std::getline(lines, line);) {
            std::string_view view = Trimmed(line);
            if (!view.starts_with('#')) continue;
            view = Trimmed(view.substr(1U));
            if (!view.starts_with("include")) continue;
            view = Trimmed(view.substr(7U));
            if (view.size() < 2U || (view.front() != '"' && view.front() != '<')) continue;
            const std::size_t close = view.find(view.front() == '"' ? '"' : '>', 1U);
            if (close == std::string_view::npos) continue;
            const std::string target = IncludedEntry(modules, view.substr(1U, close - 1U));
            if (target.empty()) continue;
            // A module depends on other modules; a subsystem on the entries (subsystems or modules) it includes.
            if (const std::string other = target.substr(0U, target.find('/')); other != module.name) byName[module.name]->dependsOn.insert(other);
            if (!subsystemName.empty() && target != subsystemName) byName[subsystemName]->dependsOn.insert(target);
        }
        if (!check) continue;
        // An entry point is a name the file declares or uses, or the name of the file itself.
        const std::string stem = item->path().stem().string();
        for (Entry* entry : { byName[module.name], subsystemName.empty() ? nullptr : byName[subsystemName] }) {
            if (entry == nullptr) continue;
            std::erase_if(entry->pendingEntryPoints, [&](const std::string& point) { return point == stem || ContainsWord(text, point); });
        }
    }
}

// ---- the hand-written part ---------------------------------------------------------------------------------------

struct Described {
    std::string responsibility;
    std::vector<std::string> entryPoints;
};

// The entries of sources/tools/code_map.json by name; problems are appended for a file that cannot be read.
[[nodiscard]] std::map<std::string, Described> ReadDescriptions(const fs::path& root, std::vector<std::string>& problems) {
    std::map<std::string, Described> described;
    const fs::path file = root / std::string{ kMapFile };
    JsonValue document;
    std::string error;
    if (!IsFile(file) || !JsonValue::Parse(ReadText(file, static_cast<std::size_t>(fs::file_size(file))), document, error)) {
        problems.push_back(std::string{ kMapFile } + " is missing or is not valid JSON" + (error.empty() ? std::string{} : ": " + error));
        return described;
    }
    const JsonValue* list = document.Find("entries");
    for (std::size_t index = 0U; list != nullptr && index < list->Size(); ++index) {
        const JsonValue* row = list->At(index);
        const JsonValue* name = row->Find("name");
        const JsonValue* responsibility = row->Find("responsibility");
        const JsonValue* points = row->Find("entryPoints");
        if (name == nullptr || name->GetKind() != JsonValue::Kind::String) {
            problems.push_back("map entry " + std::to_string(index) + " has no name");
            continue;
        }
        Described entry;
        if (responsibility != nullptr && responsibility->GetKind() == JsonValue::Kind::String) entry.responsibility = responsibility->AsString();
        for (std::size_t point = 0U; points != nullptr && point < points->Size(); ++point) entry.entryPoints.push_back(points->At(point)->AsString());
        if (!described.emplace(name->AsString(), std::move(entry)).second) problems.push_back("map entry " + name->AsString() + " is listed twice");
    }
    return described;
}

[[nodiscard]] JsonValue StringArray(const std::vector<std::string>& values) {
    auto array = JsonValue::MakeArray();
    for (const std::string& value : values) array.Append(JsonValue::MakeString(value));
    return array;
}

[[nodiscard]] JsonValue StringArray(const std::set<std::string>& values) {
    return StringArray(std::vector<std::string>{ values.begin(), values.end() });
}

[[nodiscard]] JsonValue TextOrNull(const std::string& text) {
    return text.empty() ? JsonValue::MakeNull() : JsonValue::MakeString(text);
}

// The names of `names` that have `word` among their words (split on '_' and '-').
[[nodiscard]] std::vector<std::string> NamesWithWord(const std::vector<std::string>& names, std::string_view word) {
    std::vector<std::string> found;
    for (const std::string& name : names) {
        const std::vector<std::string> words = Tokens(Lower(name), "_-");
        if (std::ranges::find(words, word) != words.end()) found.push_back(name);
    }
    return found;
}

[[nodiscard]] JsonValue EntryJson(const std::vector<Module>& modules, const Entry& entry, const Described* description) {
    const Module& module = modules[entry.module];
    const bool isModule = entry.subsystem.empty();
    auto row = JsonValue::MakeObject();
    row.Set("name", JsonValue::MakeString(entry.name));
    row.Set("kind", JsonValue::MakeString(isModule ? "module" : "subsystem"));
    if (!isModule) row.Set("module", JsonValue::MakeString(module.name));
    row.Set("responsibility", description != nullptr ? TextOrNull(description->responsibility) : JsonValue::MakeNull());
    row.Set("entryPoints", StringArray(description != nullptr ? description->entryPoints : std::vector<std::string>{}));
    row.Set("directories", StringArray(entry.directories));
    if (isModule) row.Set("folders", StringArray(module.directories));
    row.Set("includeRoot", TextOrNull(module.includeRoot));
    std::string prefix;
    if (!module.includeNamespace.empty()) prefix = module.includeNamespace + '/' + (isModule ? std::string{} : entry.subsystem + '/');
    row.Set("includePrefix", TextOrNull(prefix));
    if (isModule) {
        row.Set("subsystems", StringArray(module.subsystems));
        row.Set("buildTargets", StringArray(module.cmake.build));
        row.Set("testTargets", StringArray(module.cmake.testTargets));
        row.Set("ctestPattern", module.cmake.ctest.empty() ? JsonValue::MakeNull() : JsonValue::MakeString(CtestPrefixPattern(module.cmake.ctest)));
    } else {
        // A subsystem is built into the module's main target; its tests are the ones that name it, else the module's.
        row.Set("buildTargets", StringArray(module.cmake.build.empty() ? std::vector<std::string>{} : std::vector<std::string>{ module.cmake.build.front() }));
        const std::string word = Lower(entry.subsystem);
        std::vector<std::string> tests = NamesWithWord(module.cmake.ctest, word);
        std::vector<std::string> targets = NamesWithWord(module.cmake.testTargets, word);
        const bool specific = !tests.empty() && tests.size() <= kMaxSpecificTests;
        if (!specific) {
            tests = module.cmake.ctest;
            targets = module.cmake.testTargets;
        }
        if (targets.size() > kMaxSpecificTests) targets.resize(kMaxSpecificTests);
        row.Set("testTargets", StringArray(targets));
        row.Set("testsFrom", JsonValue::MakeString(specific ? "subsystem" : "module"));
        std::string pattern;
        if (specific) {
            for (const std::string& test : tests) pattern += (pattern.empty() ? "" : "|") + test;
            pattern = "^(" + pattern + ")$";
        } else if (!tests.empty()) {
            pattern = CtestPrefixPattern(tests);
        }
        row.Set("ctestPattern", TextOrNull(pattern));
    }
    row.Set("dependsOn", StringArray(entry.dependsOn));
    return row;
}

// ---- the topic lookup --------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> Words(std::string_view text) {
    std::vector<std::string> words;
    std::string word;
    for (std::size_t at = 0U; at <= text.size(); ++at) {
        const char c = at < text.size() ? text[at] : ' ';
        const bool split = std::isalnum(static_cast<unsigned char>(c)) == 0 ||
            (!word.empty() && std::isupper(static_cast<unsigned char>(c)) != 0 && std::islower(static_cast<unsigned char>(word.back())) != 0);
        if (split && !word.empty()) words.push_back(Lower(word));
        if (split) word.clear();
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) word += c;
    }
    return words;
}

[[nodiscard]] bool HasWord(const std::vector<std::string>& words, std::string_view wanted) {
    return std::ranges::any_of(words, [wanted](const std::string& word) {
        return word == wanted || (wanted.size() >= 4U && word.starts_with(wanted));
    });
}

// How well an entry answers a topic: every word must appear, a name or entry point says more than a sentence.
[[nodiscard]] std::size_t TopicScore(const std::vector<std::string>& topic, const Entry& entry, const Described* description,
    const JsonValue& row) {
    std::vector<std::string> nameWords = Words(entry.name);
    std::vector<std::string> pointWords;
    std::vector<std::string> textWords = description != nullptr ? Words(description->responsibility) : std::vector<std::string>{};
    if (description != nullptr) {
        for (const std::string& point : description->entryPoints) {
            // The words of the name, and the whole name, so that "navmesh" finds NavMeshAsset.
            const std::vector<std::string> words = Words(point);
            pointWords.insert(pointWords.end(), words.begin(), words.end());
            pointWords.push_back(Lower(point));
        }
    }
    std::vector<std::string> otherWords;
    for (const char* field : { "directories", "buildTargets", "testTargets" }) {
        const JsonValue* values = row.Find(field);
        for (std::size_t index = 0U; values != nullptr && index < values->Size(); ++index) {
            const std::vector<std::string> words = Words(values->At(index)->AsString());
            otherWords.insert(otherWords.end(), words.begin(), words.end());
        }
    }
    std::size_t score = 0U;
    for (const std::string& word : topic) {
        std::size_t wordScore = 0U;
        if (HasWord(nameWords, word)) wordScore += 12U;
        if (HasWord(pointWords, word)) wordScore += 6U;
        if (HasWord(textWords, word)) wordScore += 3U;
        if (HasWord(otherWords, word)) wordScore += 1U;
        if (wordScore == 0U) return 0U;
        score += wordScore;
    }
    return score;
}

// ---- the command -------------------------------------------------------------------------------------------------

struct Map {
    std::vector<Module> modules;
    std::vector<Entry> entries;
    std::map<std::string, Described> described;
    std::vector<std::string> problems;
};

[[nodiscard]] Map ReadMap(const fs::path& root, bool check) {
    Map map;
    map.modules = FindModules(root);
    map.entries = BuildEntries(root, map.modules);
    map.described = ReadDescriptions(root, map.problems);
    if (check) {
        for (Entry& entry : map.entries) {
            const auto found = map.described.find(entry.name);
            if (found != map.described.end()) entry.pendingEntryPoints.insert(found->second.entryPoints.begin(), found->second.entryPoints.end());
        }
    }
    for (std::size_t index = 0U; index < map.modules.size(); ++index) ScanModule(map.modules, index, map.entries, check);
    return map;
}

[[nodiscard]] std::size_t DocumentedCount(const Map& map) {
    return static_cast<std::size_t>(std::ranges::count_if(map.entries, [&map](const Entry& entry) {
        const auto found = map.described.find(entry.name);
        return found != map.described.end() && !found->second.responsibility.empty();
    }));
}

int RunCheck(const fs::path& root, CommandIo io) {
    Map map = ReadMap(root, true);
    std::vector<std::string> problems = std::move(map.problems);
    std::set<std::string> known;
    for (const Entry& entry : map.entries) {
        known.insert(entry.name);
        const Module& module = map.modules[entry.module];
        const auto found = map.described.find(entry.name);
        if (found == map.described.end()) {
            problems.push_back("no map entry for " + entry.name + " (" + entry.directories.front() + "); add it to " + std::string{ kMapFile });
            continue;
        }
        if (found->second.responsibility.empty()) problems.push_back(entry.name + ": the entry has no responsibility");
        if (found->second.entryPoints.empty() || found->second.entryPoints.size() > kMaxEntryPoints) {
            problems.push_back(entry.name + ": name between 1 and " + std::to_string(kMaxEntryPoints) + " entry points, not an index of everything");
        }
        for (const std::string& point : entry.pendingEntryPoints) problems.push_back(entry.name + ": entry point " + point + " is not in the source any more");
        if (entry.subsystem.empty() && IsFile(module.directory / "CMakeLists.txt") && module.cmake.build.empty()) {
            problems.push_back(entry.name + ": its CMakeLists.txt names no library or executable");
        }
    }
    for (const auto& [name, description] : map.described) {
        if (!known.contains(name)) problems.push_back("map entry " + name + " names no module or subsystem folder of the source tree");
    }
    auto result = JsonValue::MakeObject();
    result.Set("schema", JsonValue::MakeString("21kb.code-map-check/v1"));
    result.Set("ok", JsonValue::MakeBool(problems.empty()));
    auto coverage = JsonValue::MakeObject();
    coverage.Set("directories", JsonValue::MakeNumber(static_cast<double>(map.entries.size())));
    coverage.Set("withEntry", JsonValue::MakeNumber(static_cast<double>(DocumentedCount(map))));
    result.Set("coverage", std::move(coverage));
    result.Set("problems", StringArray(problems));
    io.out << result.Dump() << '\n';
    return problems.empty() ? 0 : 1;
}

} // namespace

int RunCodeModules(const std::filesystem::path& root, const ArgumentList& arguments, CommandIo io) {
    if (arguments.Flag("--check")) return RunCheck(root, io);
    const Map map = ReadMap(root, false);
    const std::string topicText = arguments.Option("--topic").value_or(std::string{});
    const std::vector<std::string> topic = Words(topicText);
    const std::size_t limit = static_cast<std::size_t>(std::clamp(std::atoi(arguments.Option("--limit").value_or(topic.empty() ? "1000" : "10").c_str()), 1, 1000));

    struct Row { std::size_t score = 0U; std::string text; std::string name; };
    std::vector<Row> rows;
    for (const Entry& entry : map.entries) {
        const auto found = map.described.find(entry.name);
        const Described* description = found == map.described.end() ? nullptr : &found->second;
        const JsonValue row = EntryJson(map.modules, entry, description);
        const std::size_t score = topic.empty() ? 1U : TopicScore(topic, entry, description, row);
        if (score != 0U) rows.push_back(Row{ .score = score, .text = row.Dump(), .name = entry.name });
    }
    if (!topic.empty()) {
        std::ranges::stable_sort(rows, [](const Row& left, const Row& right) { return left.score > right.score; });
        if (rows.empty()) {
            io.err << "error: no module or subsystem is about: " << topicText << "; kb_cli code find --text <words> searches the files\n";
            return 1;
        }
    }
    const std::size_t matched = rows.size();
    if (rows.size() > limit) rows.resize(limit);

    // One entry per line, so a reader can skim it and a parser still gets one JSON object.
    io.out << "{\"schema\":\"21kb.code-map/v1\",\"coverage\":{\"directories\":" << map.entries.size() << ",\"withEntry\":" << DocumentedCount(map) << "}";
    if (!topic.empty()) io.out << ",\"topic\":" << JsonValue::MakeString(topicText).Dump() << ",\"matched\":" << matched;
    io.out << ",\"next\":\"kb_cli code modules --topic <words> keeps the entries about a topic; kb_cli code find --text <words> names the files; "
              "kb_cli code show --symbol <Name> prints a declaration\",\n\"entries\":[\n";
    for (std::size_t index = 0U; index < rows.size(); ++index) io.out << rows[index].text << (index + 1U < rows.size() ? ",\n" : "\n");
    io.out << "]}\n";
    return 0;
}

} // namespace kb::cli
