#include "ProjectCooker.hpp"

#include <filesystem>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void PrintUsage() {
    std::cerr << "usage: kb_cooker --project <Project.21kbproject|directory> "
                 "--target <profile> --output <file.kbpack> "
                 "[--shaderc <executable>] "
                 "[--engine-root <directory>] [--cache <directory>] "
                 "[--runtime-modules-output <directory>]\n";
}

[[nodiscard]] bool ReadValue(int argc, char** argv, int& index, std::string& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return !out.empty();
}

} // namespace

int main(int argc, char** argv) {
    kb::game::ProjectCookRequest request{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view option{ argv[index] };
        std::string value;
        if (option == "--project" && ReadValue(argc, argv, index, value)) {
            request.projectPath = std::filesystem::path{ value };
        } else if (option == "--target" && ReadValue(argc, argv, index, value)) {
            request.targetProfileId = std::move(value);
        } else if (option == "--output" && ReadValue(argc, argv, index, value)) {
            request.outputPackPath = std::filesystem::path{ value };
        } else if (option == "--shaderc" && ReadValue(argc, argv, index, value)) {
            request.shadercPath = std::filesystem::path{ value };
        } else if (option == "--engine-root" && ReadValue(argc, argv, index, value)) {
            request.engineRoot = std::filesystem::path{ value };
        } else if (option == "--cache" && ReadValue(argc, argv, index, value)) {
            request.cacheRoot = std::filesystem::path{ value };
        } else if (option == "--runtime-modules-output" && ReadValue(argc, argv, index, value)) {
            request.runtimeModulesOutputDirectory = std::filesystem::path{ value };
        } else {
            PrintUsage();
            return 2;
        }
    }
    if (request.projectPath.empty() || request.targetProfileId.empty() || request.outputPackPath.empty()) {
        PrintUsage();
        return 2;
    }

    kb::game::ProjectCookResult result{};
    try {
        result = kb::game::CookProject(request, std::cerr);
    } catch (const std::exception& exception) {
        std::cerr << "cook failed with an unhandled error: " << exception.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "cook failed with an unknown unhandled error\n";
        return 1;
    }
    if (!result.succeeded) {
        std::cerr << "cook failed: " << result.error << '\n';
        return 1;
    }
    std::cout << "cook succeeded: " << result.assetCount << " assets, "
              << result.textureArtifactCount << " texture variants, "
              << result.meshArtifactCount << " meshes, "
              << result.shaderArtifactCount << " material shaders, "
              << result.auxiliaryFileCount << " auxiliary files\n";
    return 0;
}
