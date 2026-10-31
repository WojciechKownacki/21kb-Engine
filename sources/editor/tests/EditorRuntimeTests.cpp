#include "EditorTestSuites.hpp"

#include <string_view>

int main(int argumentCount, char** arguments) {
    if (argumentCount==2 && std::string_view{arguments[1]}=="gameplay-graph") {
        kb::editor::tests::RunEditorGameplayGraphTests();return 0;
    }
    if (argumentCount==2 && std::string_view{arguments[1]}=="blend-space-2d") {
        kb::editor::tests::RunEditorBlendSpace2DTests();return 0;
    }
    if (argumentCount == 2 && std::string_view{arguments[1]} == "timeline") {
        kb::editor::tests::RunEditorTimelineTests(); return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "docking") {
        kb::editor::tests::RunEditorDockingTests(); return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "asset-browser") {
        kb::editor::tests::RunEditorAssetBrowserTests();
        return 0;
    }
    if (argumentCount == 2 && std::string_view{arguments[1]} == "audio-mixer-runtime") {
        kb::editor::tests::RunEditorAudioMixerRuntimeTests(); return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "hierarchy") {
        kb::editor::tests::RunEditorHierarchyTests();
        return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "play-session") {
        kb::editor::tests::RunEditorPlayModeSceneSessionTests();
        return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "script-editor") {
        kb::editor::tests::RunScriptEditorTests();
        return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "audio-mixer") {
        kb::editor::tests::RunEditorAudioMixerAuthoringTests();
        return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "audio-mixer-inspector") {
        kb::editor::tests::RunEditorAudioMixerInspectorTests();
        return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "scene-audio-inspector") {
        kb::editor::tests::RunEditorSceneAudioInspectorTests();
        return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "inspector") {
        kb::editor::tests::RunEditorInspectorTests();
        return 0;
    }
    kb::editor::tests::RunEditorTimelineTests();
    kb::editor::tests::RunEditorBlendSpace2DTests();
    kb::editor::tests::RunEditorGameplayGraphTests();
    kb::editor::tests::RunEditorCommandStackTests();
    kb::editor::tests::RunEditorPlayModeSceneSessionTests();
    kb::editor::tests::RunEditorHierarchyTests();
    kb::editor::tests::RunEditorAssetBrowserTests();
    kb::editor::tests::RunEditorAudioMixerAuthoringTests();
    kb::editor::tests::RunEditorAudioMixerInspectorTests();
    kb::editor::tests::RunEditorSceneAudioInspectorTests();
    kb::editor::tests::RunEditorViewportPreviewTests();
    kb::editor::tests::RunEditorMaterialGraphCanvasTests();
    kb::editor::tests::RunEditorMaterialGraphCookServiceTests();
    kb::editor::tests::RunEditorDockingTests();
    kb::editor::tests::RunEditorProjectTests();
    kb::editor::tests::RunEditorSettingsTests();
    kb::editor::tests::RunEditorLocalizationTests();
    kb::editor::tests::RunEditorInspectorTests();
    kb::editor::tests::RunEditorMaterialAssetAuthoringTests();
    kb::editor::tests::RunScriptEditorTests();
    kb::editor::tests::RunSvgPathTests();
    return 0;
}
