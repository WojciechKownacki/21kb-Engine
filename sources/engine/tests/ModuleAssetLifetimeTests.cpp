#include "ModuleAssetFixture.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/modules/EngineModuleLoader.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using Value = kb::tests::ModuleAssetValue;
void Require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
std::string Trace() {
    std::ifstream input{ KB_ASSET_TRACE };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}
void RegisterAssets(kb::assets::AssetManager& assets) {
    const auto folder = std::filesystem::temp_directory_path() / "21kb_module_asset_lifetime";
    std::filesystem::create_directories(folder);
    std::ofstream{ folder / "value.moduleasset" } << "42";
    Require(assets.Mounts().Mount("Fixture", folder), "Asset mount failed");
    for (std::uint64_t id = 1; id <= 3; ++id)
        Require(assets.RegisterAsset({ .id = { id }, .type = "ModuleAsset", .name = id == 2 ? "Slow" : "Value",
            .virtualPath = "Fixture/value" + std::to_string(id) + ".moduleasset", .physicalPath = folder / "value.moduleasset" }),
            "Asset metadata registration failed");
}
void Mapping(const std::filesystem::path& path, bool expected) {
#if defined(_WIN32)
    Require((GetModuleHandleW(path.c_str()) != nullptr) == expected, "Native library mapping lifetime is incorrect");
#else
    static_cast<void>(path); static_cast<void>(expected);
#endif
}
void DirectLifetimeTest() {
    std::filesystem::remove(KB_ASSET_TRACE);
    kb::modules::EngineModuleLoader loader;
    auto loaded = loader.Load({ .key = "AssetLease", .modulePath = KB_ASSET_PLUGIN });
    Require(loaded.Succeeded(), "Asset fixture did not load");
    const auto mappedPath = loaded.loadedPath;
    std::vector<std::string> diagnostics;
    const auto create = reinterpret_cast<kb::assets::IAssetLoader*(*)()>(loaded.library.FindSymbol("kb_test_asset_loader", diagnostics));
    const auto publish = reinterpret_cast<bool(*)(kb::assets::AssetManager*)>(loaded.library.FindSymbol("kb_test_publish_asset", diagnostics));
    Require(create && publish, "Asset fixture exports missing");
    auto lease = loaded.library.Retain();
    std::weak_ptr<void> weakLease = lease;
    kb::assets::AssetManager assets;
    RegisterAssets(assets);
    Require(assets.RegisterLoader(std::unique_ptr<kb::assets::IAssetLoader>{ create() }, lease), "Leased loader registration failed");
    lease.reset();
    auto retained = assets.Load<Value>({ 1 });
    Require(retained.IsLoaded() && retained->value == 42, "Plugin payload failed to load");
    Require(publish(&assets), "Plugin payload publication failed");
    auto published = assets.AcquireLoaded<Value>({ 3 });
    Require(published.IsLoaded(), "Published payload missing");
    kb::assets::WeakAssetHandle<Value> weak{ retained };
    loaded.library.Reset();
    Mapping(mappedPath, true);
    Require(assets.UnregisterLoader("ModuleAsset") && !assets.HasLoaderForType("ModuleAsset"), "Loader unregister failed");
    Require(!assets.Load<Value>({ 1 }).IsLoaded(), "Removed loader remained loadable through its cache");
    Require(!weakLease.expired() && retained->value == 42 && published->value == 42,
        "Unregister invalidated an external payload");
    retained = {};
    Require(weak.Expired() && !weakLease.expired(), "Payload ownership did not distinguish independent assets");
    published = {};
    Require(weakLease.expired(), "A weak asset handle pinned the library after all payloads were destroyed");
    Mapping(mappedPath, false);
    weak = {}; // The ownership control block must belong to the host after DLL unload.
    Require(Trace() == "load entered\nloader destroyed\npayload destroyed\npayload destroyed\n",
        "Loader or payload destructor order was incorrect");
}
void AsyncReplacementTest() {
    std::filesystem::remove(KB_ASSET_TRACE);
    kb::modules::EngineModuleLoader loader;
    auto loaded = loader.Load({ .key = "AssetAsyncLease", .modulePath = KB_ASSET_PLUGIN });
    Require(loaded.Succeeded(), "Async fixture did not load");
    const auto mappedPath = loaded.loadedPath;
    std::vector<std::string> diagnostics;
    const auto create = reinterpret_cast<kb::assets::IAssetLoader*(*)()>(loaded.library.FindSymbol("kb_test_asset_loader", diagnostics));
    Require(create != nullptr, "Async fixture export missing");
    auto lease = loaded.library.Retain();
    std::weak_ptr<void> weak = lease;
    kb::assets::AssetManager assets;
    RegisterAssets(assets);
    Require(assets.RegisterLoader(std::unique_ptr<kb::assets::IAssetLoader>{ create() }, lease), "Async registration failed");
    Require(assets.LoadAsync<Value>({ 2 }), "Async load did not start");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
    while (Trace().find("load entered") == std::string::npos && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    Require(Trace().find("load entered") != std::string::npos, "Async loader did not enter");
    loaded.library.Reset();
    // Replacement must wait for the old decoder, discard its result and keep the old DLL mapped.
    Require(assets.RegisterLoader(std::unique_ptr<kb::assets::IAssetLoader>{ create() }, lease), "Loader replacement failed");
    Require(Trace().find("load cancelled\npayload destroyed\nloader destroyed\n") != std::string::npos,
        "Replacement did not drain the old decoder before destroying its loader");
    lease.reset();
    Require(assets.UnregisterLoader("ModuleAsset"), "Async unregister failed");
    assets.PumpAsyncLoads();
    Require(!assets.IsLoaded({ 2 }) && !assets.Load<Value>({ 2 }).IsLoaded(), "Late async result published after unregister");
    Require(weak.expired(), "Async results retained the removed library");
    Mapping(mappedPath, false);
}
void HostReloadTest() {
    std::filesystem::remove(KB_ASSET_TRACE);
    const auto working = std::filesystem::temp_directory_path() / "21kb_asset_reload_fixture.dll";
    std::filesystem::copy_file(KB_ASSET_PLUGIN, working, std::filesystem::copy_options::overwrite_existing);
    kb::scene::Scene scene;
    auto& assets = scene.Assets().Manager();
    RegisterAssets(assets);
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back({ .name = "Tests.AssetLifetime", .binaryPath = working.string(), .enabled = true });
    kb::assets::AssetHandle<Value> retained;
    kb::assets::WeakAssetHandle<Value> weak;
    {
        kb::modules::EngineModuleHost host{ project };
        host.Load(scene.Runtime().EcsWorld());
        Require(host.IsActive("Tests.AssetLifetime"), "Asset module did not load");
        host.AttachScene(scene);
        retained = assets.Load<Value>({ 1 });
        weak = kb::assets::WeakAssetHandle<Value>{ retained };
        Require(retained.IsLoaded(), "Module context did not register a usable loader");
        std::array<kb::scene::Scene*, 1> scenes{ &scene };
        std::filesystem::remove(working);
        host.Reload(scene.Runtime().EcsWorld(), scenes);
        Require(!host.Diagnostics().empty() && host.IsActive("Tests.AssetLifetime") &&
            assets.Load<Value>({ 1 }).Get() == retained.Get(), "Failed replacement changed the active loader or its payload");
        std::filesystem::copy_file(KB_ASSET_PLUGIN, working, std::filesystem::copy_options::overwrite_existing);
        for (int i = 0; i < 3; ++i) {
            host.Reload(scene.Runtime().EcsWorld(), scenes);
            Require(host.Diagnostics().empty() && retained->value == 42, "Reload lost a retained plugin asset");
            auto current = assets.Load<Value>({ 1 });
            Require(current.IsLoaded() && current.Get() != retained.Get(), "Reload did not replace the loader payload");
        }
        host.DetachScene(scene);
        host.Unload();
        Require(!assets.HasLoaderForType("ModuleAsset") && retained->value == 42, "Module unload invalidated a retained payload");
    }
    retained = {};
    Require(weak.Expired(), "Retained payload did not release after host destruction");
    weak = {};
    std::filesystem::remove(working);
    const auto trace = Trace();
    const auto count = [&](std::string_view needle) {
        std::size_t n = 0, pos = 0;
        while ((pos = trace.find(needle, pos)) != std::string::npos) { ++n; pos += needle.size(); }
        return n;
    };
    Require(count("loader destroyed") == 4 && count("module destroyed") == 4 && count("payload destroyed") == 4,
        "Module reload leaked or double-destroyed plugin objects");
}
} // namespace
int main() {
    try {
        DirectLifetimeTest();
        std::filesystem::copy_file(KB_ASSET_TRACE, std::string{ KB_ASSET_TRACE } + ".direct", std::filesystem::copy_options::overwrite_existing);
        AsyncReplacementTest();
        std::filesystem::copy_file(KB_ASSET_TRACE, std::string{ KB_ASSET_TRACE } + ".async", std::filesystem::copy_options::overwrite_existing);
        HostReloadTest();
        std::cout << "Module asset lifetime tests passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
