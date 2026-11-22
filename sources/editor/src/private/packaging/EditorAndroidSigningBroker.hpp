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
    // Makes a new upload key for the store: a PKCS12 key file at `keystore` (which must not exist yet) holding an
    // RSA 4096 key valid for 10000 days under `alias`, with `owner` as the certificate's name. The password reaches
    // the key tool on its standard input only and is cleared from `password`'s copies; a PKCS12 key uses the file's
    // password. Fails with a reason when Java is missing, the password is shorter than 6 characters, or the tool fails.
    [[nodiscard]] static bool CreateUploadKey(const std::filesystem::path& keystore, std::string_view alias,
        std::string_view owner, const std::string& password, std::string& error);
    static void SecureClear(std::string& value) noexcept;
};

} // namespace kb::editor
