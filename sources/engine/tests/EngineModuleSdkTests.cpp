#include "engine/modules/EngineModuleCallback.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/modules/EngineModuleLoader.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string Read(const std::filesystem::path& path) {
    std::ifstream input{ path };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}
KbModuleRegistration LastRegistration() {
    std::ifstream input{ KB_SDK_1_TRACE };
    std::string word;
    KbModuleRegistration result = 0U;
    while (input >> word) if (word == "registration") input >> result;
    return result;
}
kb::project::ProjectDescriptor Project(const std::filesystem::path& path) {
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back({ .name = "Tests.PublicSdk", .binaryPath = path.string(), .enabled = true });
    return project;
}
float Position(kb::scene::Scene& scene) {
    for (const auto entity : scene.Hierarchy().RootEntities())
        if (scene.Entities().Name(entity) == "SdkObject") return scene.Transforms().Get(entity).localPosition.x;
    throw std::runtime_error("SDK module did not create a real scene object");
}
void NegotiationTest() {
    kb::modules::EngineModuleLoader loader;
    auto loaded = loader.Load({ .key = "SdkNegotiation", .modulePath = KB_SDK_1_PATH });
    Require(loaded.Succeeded(), "SDK negotiation fixture did not load");
    std::vector<std::string> diagnostics;
    const auto getApi = reinterpret_cast<KbModuleGetApi>(loaded.library.FindSymbol(KB_MODULE_SDK_ENTRY, diagnostics));
    Require(getApi != nullptr, "SDK entry was not exported");
    KbModuleApi output;
    std::memset(&output, 0x5a, sizeof(output));
    std::array<unsigned char, sizeof(output)> before{};
    std::memcpy(before.data(), &output, sizeof(output));
    KbModuleError error{};
    for (const auto version : { 0U, KB_MODULE_SDK_VERSION + 1U }) {
        Require(getApi(version, sizeof(output), &output, &error) == KB_MODULE_UNSUPPORTED_VERSION &&
            std::memcmp(before.data(), &output, sizeof(output)) == 0, "Rejected SDK version modified the output");
    }
    for (const auto size : { 0U, static_cast<unsigned>(sizeof(output) - 1U), static_cast<unsigned>(sizeof(output) + 1U) }) {
        Require(getApi(KB_MODULE_SDK_VERSION, size, &output, &error) == KB_MODULE_UNSUPPORTED_VERSION &&
            std::memcmp(before.data(), &output, sizeof(output)) == 0, "Rejected SDK layout modified the output");
    }
    Require(getApi(KB_MODULE_SDK_VERSION, sizeof(output), &output, &error) == KB_MODULE_OK &&
        output.size == sizeof(output) && output.create && output.destroy, "Supported SDK API did not negotiate");
}
void RuntimeTest() {
    std::filesystem::remove(KB_SDK_1_TRACE);
    std::filesystem::remove(KB_SDK_2_TRACE);
    const auto folder = std::filesystem::temp_directory_path() / "21kb_module_sdk_runtime";
    std::filesystem::create_directories(folder);
    const auto working = folder / std::filesystem::path{ KB_SDK_1_PATH }.filename();
    std::filesystem::copy_file(KB_SDK_1_PATH, working, std::filesystem::copy_options::overwrite_existing);
    kb::scene::Scene first;
    kb::scene::Scene second;
    const auto firstSystems = first.Runtime().SceneSystemCount();
    const auto secondSystems = second.Runtime().SceneSystemCount();
    const auto firstEntities = first.Entities().Count();
    const auto secondEntities = second.Entities().Count();
    kb::modules::EngineModuleCallback retained;
    {
        kb::modules::EngineModuleHost host{ Project(working) };
        host.Load(first.Runtime().EcsWorld());
        Require(host.IsActive("Tests.PublicSdk") && host.Diagnostics().empty(), "Separate C SDK plugin did not load");
        host.AttachScene(first);
        host.AttachScene(second);
        Require(first.Runtime().SceneSystemCount() == firstSystems + 1U &&
            second.Runtime().SceneSystemCount() == secondSystems + 1U, "SDK callbacks did not attach once to each scene");
        retained = kb::modules::RetainEngineModuleCallback(LastRegistration());
        Require(retained.IsActive() && retained.Invoke(0.5F) == KB_MODULE_OK && Position(second) == 0.5F,
            "A retained SDK callback did not update the engine scene through the C API");
        KbModuleStatus foreignThread = KB_MODULE_OK;
        std::thread thread{ [&] { foreignThread = retained.Invoke(1.0F); } };
        thread.join();
        Require(foreignThread == KB_MODULE_WRONG_THREAD && Position(second) == 0.5F, "SDK callback ran on the wrong thread");
        static_cast<void>(first.Runtime().Update(0.01F));
        Require(Position(first) > 0.0F, "SDK callback was not invoked by the scene scheduler");
        std::array<kb::scene::Scene*, 2U> scenes{ &first, &second };
        std::filesystem::copy_file(KB_SDK_2_PATH, working, std::filesystem::copy_options::overwrite_existing);
        host.Reload(first.Runtime().EcsWorld(), scenes);
        Require(host.IsActive("Tests.PublicSdk") && !host.Diagnostics().empty() && retained.IsActive() &&
            retained.Invoke(0.5F) == KB_MODULE_OK && Position(second) == 1.0F,
            "An incompatible SDK replacement changed the current callback or scene state");
        Require(Read(KB_SDK_2_TRACE).empty(), "An incompatible SDK API reached the plugin factory");
        std::filesystem::copy_file(KB_SDK_1_PATH, working, std::filesystem::copy_options::overwrite_existing);
        for (int iteration = 0; iteration < 3; ++iteration) {
            host.Reload(first.Runtime().EcsWorld(), scenes);
            Require(host.Diagnostics().empty() && !retained.IsActive() &&
                retained.Invoke(1.0F) == KB_MODULE_STALE_HANDLE, "Reload invoked a retained callback from the old DLL");
            Require(first.Entities().Count() == firstEntities + 1U && second.Entities().Count() == secondEntities + 1U &&
                first.Runtime().SceneSystemCount() == firstSystems + 1U && second.Runtime().SceneSystemCount() == secondSystems + 1U,
                "SDK reload leaked registered entities or scene systems");
            retained = kb::modules::RetainEngineModuleCallback(LastRegistration());
            Require(retained.Invoke(0.25F) == KB_MODULE_OK && Position(second) == 0.25F,
                "SDK reload did not recreate working scene state");
        }
        host.Unload(); // Must also clean up when the caller did not detach first.
        Require(!retained.IsActive() && first.Entities().Count() == firstEntities && second.Entities().Count() == secondEntities &&
            first.Runtime().SceneSystemCount() == firstSystems && second.Runtime().SceneSystemCount() == secondSystems,
            "SDK unload retained callbacks, entities or scene systems");
    }
    Require(retained.Invoke(1.0F) == KB_MODULE_STALE_HANDLE, "A retained handle invoked code after host destruction");
    const auto trace = Read(KB_SDK_1_TRACE);
    const auto count = [&](std::string_view needle) {
        std::size_t result = 0U, offset = 0U;
        while ((offset = trace.find(needle, offset)) != std::string::npos) { ++result; offset += needle.size(); }
        return result;
    };
    Require(count("create\n") == 4U && count("destroy\n") == 4U && count("release\n") == 8U &&
        count("foreign entity refused\n") == 4U, "SDK ownership callbacks did not release every plugin allocation exactly once");
    std::filesystem::remove(working);
}
void FailureTests() {
    for (int failure : { 3, 4 }) {
        const auto* path = failure == 3 ? KB_SDK_3_PATH : KB_SDK_4_PATH;
        const auto* tracePath = failure == 3 ? KB_SDK_3_TRACE : KB_SDK_4_TRACE;
        std::filesystem::remove(tracePath);
        kb::scene::Scene scene;
        const auto systems = scene.Runtime().SceneSystemCount();
        const auto entities = scene.Entities().Count();
        {
            kb::modules::EngineModuleHost host{ Project(path) };
            host.Load(scene.Runtime().EcsWorld());
            if (failure == 3) {
                Require(host.ActiveCount() == 0U && !host.Diagnostics().empty(), "Failed SDK factory activated a module");
                Require(Read(tracePath) == "create\ndestroy\n", "Failed factory did not destroy its partial instance in its own runtime");
            } else {
                bool refused = false;
                try { host.AttachScene(scene); } catch (const std::exception&) { refused = true; }
                Require(refused && scene.Entities().Count() == entities && scene.Runtime().SceneSystemCount() == systems,
                    "Failed SDK attach retained engine-owned registrations");
                Require(Read(tracePath).find("release\n") != std::string::npos, "Failed SDK attach did not release callback-owned memory");
            }
        }
        Require(Read(tracePath).ends_with("destroy\n"), "SDK failure did not destroy the plugin instance");
    }
}
} // namespace

int main() {
    try { NegotiationTest(); RuntimeTest(); FailureTests(); std::cout << "Public module SDK tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
