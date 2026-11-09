#include "engine/project/ProjectDescriptorReader.hpp"
#include "engine/project/ProjectDescriptorWriter.hpp"
#include <filesystem>

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const std::filesystem::path path{argv[1]};
    auto project = kb::project::ProjectDescriptorReader::Read(path);
    if (!project.succeeded) return 2;
    for (auto& plugin : project.descriptor.plugins) {
        if (plugin.name == "Tests.GraphFixture") {
            plugin.binaryPath = argv[2];
            return kb::project::ProjectDescriptorWriter::Write(path, project.descriptor) ? 0 : 3;
        }
    }
    project.descriptor.plugins.push_back({"Tests.GraphFixture", argv[2], true});
    return kb::project::ProjectDescriptorWriter::Write(path, project.descriptor) ? 0 : 3;
}
