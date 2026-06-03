#include "EditorTestSuites.hpp"

int main() {
    kb::editor::tests::RunEditorHierarchyTests();
    kb::editor::tests::RunEditorAssetBrowserTests();
    kb::editor::tests::RunEditorViewportPreviewTests();
    kb::editor::tests::RunEditorDockingTests();
    kb::editor::tests::RunSvgPathTests();
    return 0;
}
