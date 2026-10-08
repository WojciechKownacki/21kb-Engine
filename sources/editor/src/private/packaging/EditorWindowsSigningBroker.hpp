#pragma once

#include <filesystem>
#include <string>

namespace kb::editor {

struct EditorWindowsSigningResult {
    bool succeeded = false;
    std::string message;
    std::string signerThumbprint;
    std::string toolOutput;
};

// Signs the Windows images a package job copied into its own job directory with
// the PFX the author selected. The password stays in the editor: it reaches the
// signer only through that child process's standard input, never its command
// line, environment or the package script.
class EditorWindowsSigningBroker final {
public:
    EditorWindowsSigningBroker() = delete;
    [[nodiscard]] static EditorWindowsSigningResult Execute(
        const std::filesystem::path& requestFile,
        const std::filesystem::path& responseFile,
        const std::filesystem::path& expectedJobsRoot,
        const std::filesystem::path& expectedCertificate,
        const std::string& expectedTimestampUrl,
        const std::filesystem::path& signer,
        std::string& password,
        void* processJob);
    // The signer shipped beside the editor executable.
    [[nodiscard]] static std::filesystem::path DefaultSigner();
};

} // namespace kb::editor
