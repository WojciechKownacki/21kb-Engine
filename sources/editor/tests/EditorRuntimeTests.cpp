#include "EditorTestSuites.hpp"

int main() {
    kb::editor::tests::RunEditorCommandStackTests();
    kb::editor::tests::RunEditorPlayModeSceneSessionTests();
    kb::editor::tests::RunEditorHierarchyTests();
    kb::editor::tests::RunEditorAssetBrowserTests();
    kb::editor::tests::RunEditorViewportPreviewTests();
    kb::editor::tests::RunEditorDockingTests();
    kb::editor::tests::RunEditorProjectTests();
    kb::editor::tests::RunEditorInspectorTests();
    kb::editor::tests::RunEditorMaterialAssetAuthoringTests();
    kb::editor::tests::RunScriptEditorTests();
    kb::editor::tests::RunSvgPathTests();
    return 0;
}
