#include "kb/editor/EditorApplication.hpp"

#include "project/EditorProjectPaths.hpp"

namespace {

void ConfigureProjectFromArguments(int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view{ argv[index] } == "--project") {
            kb::editor::EditorProjectPaths::SetProjectFile(argv[index + 1]);
            return;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    ConfigureProjectFromArguments(argc, argv);

    kb::editor::EditorApplication app;
    if (!app.Initialize()) {
        return 1;
    }

    app.Run();
    return 0;
}
