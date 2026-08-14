#include "scene/ParticleEditorHostSessionStore.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {
namespace {

constexpr std::string_view kHeader = "21kb Particle Editor Session 1";

[[nodiscard]] std::filesystem::path Normalized(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? std::filesystem::path{} : absolute.lexically_normal();
}

[[nodiscard]] bool IsWithin(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    const std::filesystem::path normalizedRoot = Normalized(root);
    const std::filesystem::path normalizedCandidate = Normalized(candidate);
    if (normalizedRoot.empty() || normalizedCandidate.empty()) return false;
    auto rootIt = normalizedRoot.begin();
    auto candidateIt = normalizedCandidate.begin();
    for (; rootIt != normalizedRoot.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == normalizedCandidate.end() || *rootIt != *candidateIt) return false;
    }
    return true;
}

[[nodiscard]] bool ValidArea(int area) noexcept {
    return area >= static_cast<int>(DockArea::Left) && area <= static_cast<int>(DockArea::Floating);
}

[[nodiscard]] bool Replace(const std::filesystem::path& source, const std::filesystem::path& target) noexcept {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return !error;
#endif
}

} // namespace

ParticleEditorHostSessionLoadResult ParticleEditorHostSessionStore::Load(
    const std::filesystem::path& statePath,
    const std::filesystem::path& projectRoot) {
    ParticleEditorHostSessionLoadResult result;
    std::error_code error;
    if (!std::filesystem::exists(statePath, error)) {
        if (error) result.error = "particle editor session state could not be inspected";
        return result;
    }
    const std::uintmax_t bytes = std::filesystem::file_size(statePath, error);
    if (error || bytes == 0U || bytes > kMaximumBytes) {
        result.error = "particle editor session state has an invalid size";
        return result;
    }
    std::ifstream input{statePath, std::ios::binary};
    std::string source(static_cast<std::size_t>(bytes), '\0');
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (!input) {
        result.error = "particle editor session state could not be read";
        return result;
    }
    std::istringstream stream{source};
    std::string header;
    std::getline(stream, header);
    int visible = 0;
    int area = 0;
    std::string key;
    std::string documentPath;
    ParticleEditorHostSession session;
    if (header != kHeader || !(stream >> key >> visible) || key != "visible" ||
        !(stream >> key >> area) || key != "area" || !ValidArea(area) ||
        !(stream >> key >> session.floatingRect.x >> session.floatingRect.y >>
            session.floatingRect.width >> session.floatingRect.height) || key != "rect" ||
        !(stream >> key >> std::quoted(documentPath)) || key != "path" ||
        (visible != 0 && visible != 1) || session.floatingRect.width < 260 ||
        session.floatingRect.height < 180) {
        result.error = "particle editor session state is malformed";
        return result;
    }
    stream >> std::ws;
    if (!stream.eof()) {
        result.error = "particle editor session state contains trailing records";
        return result;
    }
    session.documentPath = std::filesystem::path{documentPath};
    if (!session.documentPath.empty() && !IsWithin(projectRoot, session.documentPath)) {
        result.error = "particle editor session path escapes the project root";
        return result;
    }
    session.visible = visible != 0;
    session.area = static_cast<DockArea>(area);
    result.session = std::move(session);
    result.found = true;
    return result;
}

bool ParticleEditorHostSessionStore::Save(
    const std::filesystem::path& statePath,
    const std::filesystem::path& projectRoot,
    const ParticleEditorHostSession& session,
    std::string& error) {
    error.clear();
    const std::filesystem::path normalizedDocumentPath = session.documentPath.empty()
        ? std::filesystem::path{}
        : Normalized(session.documentPath);
    if (!session.documentPath.empty() &&
        (normalizedDocumentPath.empty() || !IsWithin(projectRoot, normalizedDocumentPath))) {
        error = "particle editor session path escapes the project root";
        return false;
    }
    if (!ValidArea(static_cast<int>(session.area)) || session.floatingRect.width < 260 ||
        session.floatingRect.height < 180) {
        error = "particle editor session layout is invalid";
        return false;
    }
    std::ostringstream stream;
    stream << kHeader << '\n'
           << "visible " << (session.visible ? 1 : 0) << '\n'
           << "area " << static_cast<int>(session.area) << '\n'
           << "rect " << session.floatingRect.x << ' ' << session.floatingRect.y << ' '
           << session.floatingRect.width << ' ' << session.floatingRect.height << '\n'
           << "path " << std::quoted(normalizedDocumentPath.generic_string()) << '\n';
    const std::string bytes = stream.str();
    if (bytes.empty() || bytes.size() > kMaximumBytes) {
        error = "particle editor session state exceeds its bounded size";
        return false;
    }
    std::error_code filesystemError;
    if (statePath.has_parent_path()) {
        std::filesystem::create_directories(statePath.parent_path(), filesystemError);
    }
    if (filesystemError) {
        error = "particle editor session directory could not be created";
        return false;
    }
    std::filesystem::path temporary = statePath;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, filesystemError);
            error = "particle editor session temporary file could not be written";
            return false;
        }
    }
    if (!Replace(temporary, statePath)) {
        std::filesystem::remove(temporary, filesystemError);
        error = "particle editor session atomic replacement failed";
        return false;
    }
    return true;
}

} // namespace kb::editor
