#include "EditorTestSuites.hpp"

int main() {
    kb::editor::tests::RunEditorHierarchyTests();
    kb::editor::tests::RunSvgPathTests();
    return 0;
}
