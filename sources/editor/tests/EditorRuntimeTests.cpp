#include "EditorTestSuites.hpp"

#include <string_view>

int main(int argumentCount, char** arguments) {
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "audio-mixer") {
        kb::editor::tests::RunEditorAudioMixerAuthoringTests();
        return 0;
    }
    if (argumentCount == 2 && std::string_view{ arguments[1] } == "audio-mixer-inspector") {
        kb::editor::tests::RunEditorAudioMixerInspectorTests();
        return 0;
    }
    kb::editor::tests::RunEditorCommandStackTests();
    kb::editor::tests::RunEditorPlayModeSceneSessionTests();
    kb::editor::tests::RunEditorHierarchyTests();
    kb::editor::tests::RunEditorAssetBrowserTests();
    kb::editor::tests::RunEditorAudioMixerAuthoringTests();
    kb::editor::tests::RunEditorAudioMixerInspectorTests();
    kb::editor::tests::RunEditorViewportPreviewTests();
    kb::editor::tests::RunEditorMaterialGraphCanvasTests();
    kb::editor::tests::RunEditorMaterialGraphCookServiceTests();
    kb::editor::tests::RunEditorDockingTests();
    kb::editor::tests::RunEditorProjectTests();
    kb::editor::tests::RunEditorInspectorTests();
    kb::editor::tests::RunEditorMaterialAssetAuthoringTests();
    kb::editor::tests::RunScriptEditorTests();
    kb::editor::tests::RunSvgPathTests();
    return 0;
}
