#include "EditorApplication.hpp"

int main() {
    kb::editor::EditorApplication app;
    if (!app.Initialize()) {
        return 1;
    }

    app.Run();
    return 0;
}
