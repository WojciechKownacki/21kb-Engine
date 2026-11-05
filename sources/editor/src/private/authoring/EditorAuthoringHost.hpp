#pragma once
#include "kb/editor/authoring/EditorAuthoring.hpp"
#include <thread>
namespace kb::editor {
class EditorSceneContext;
class EditorAuthoringHost final : public IEditorAuthoringHost {
public:
    explicit EditorAuthoringHost(EditorSceneContext& context):context_(context),owner_(std::this_thread::get_id()) {}
    [[nodiscard]] bool Request(std::string_view request,std::string& response,std::string& error) override;
private:
    EditorSceneContext& context_;
    std::thread::id owner_;
};
}
