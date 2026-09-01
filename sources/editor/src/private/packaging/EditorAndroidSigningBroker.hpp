#pragma once

#include <filesystem>
#include <string>

namespace kb::editor {

struct EditorAndroidSigningResult {
    bool succeeded = false;
    std::string message;
    std::string toolOutput;
};

class EditorAndroidSigningBroker final {
public:
    EditorAndroidSigningBroker() = delete;
    [[nodiscard]] static EditorAndroidSigningResult Execute(
        const std::filesystem::path& requestFile,
        const std::filesystem::path& responseFile,
        const std::filesystem::path& expectedJobsRoot,
        const std::filesystem::path& expectedKeystore,
        const std::string& expectedAlias,
        std::string& storePassword,
        std::string& keyPassword,
        void* processJob);
    [[nodiscard]] static bool IsTrustedJavaExecutable(const std::filesystem::path& path);
    static void SecureClear(std::string& value) noexcept;
};

} // namespace kb::editor
