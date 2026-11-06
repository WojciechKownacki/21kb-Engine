#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
namespace kb::editor {
// Owner-thread editor authoring. Requests use the documented kb21 schema;
// the host owns scene mutations, validation, history and file persistence.
class IEditorAuthoringHost {
public:
    virtual ~IEditorAuthoringHost() = default;
    [[nodiscard]] virtual bool Request(std::string_view request,std::string& response,std::string& error) = 0;
};
class EditorPythonRuntime final {
public:
    EditorPythonRuntime();
    ~EditorPythonRuntime();
    EditorPythonRuntime(const EditorPythonRuntime&)=delete;
    EditorPythonRuntime& operator=(const EditorPythonRuntime&)=delete;
    [[nodiscard]] bool Execute(IEditorAuthoringHost& host,std::string_view code,std::string_view filename,std::string& error);
    [[nodiscard]] bool ExecuteFile(IEditorAuthoringHost& host,const std::filesystem::path& path,std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
