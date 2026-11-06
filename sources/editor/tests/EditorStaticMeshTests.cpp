#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"
#include "assets/EditorAssetOpenPolicy.hpp"
#include "rendering/StaticMeshEditorPanel.hpp"
#include "scene/StaticMeshEditorSession.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/BuiltInShapes.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

namespace kb::editor::tests {
namespace {

// A UV sphere of about 3,000 triangles: dense enough to be given a chain of levels.
std::string SphereObj(int segments, int rings) {
    std::string obj = "v 0 1 0\n";
    for (int ring = 1; ring < rings; ++ring) {
        const double latitude = 3.14159265358979 * ring / rings;
        for (int segment = 0; segment < segments; ++segment) {
            const double longitude = 6.28318530717959 * segment / segments;
            obj += "v " + std::to_string(std::sin(latitude) * std::cos(longitude)) + " " + std::to_string(std::cos(latitude)) + " " +
                std::to_string(std::sin(latitude) * std::sin(longitude)) + "\n";
        }
    }
    obj += "v 0 -1 0\n";
    const auto at = [&](int ring, int segment) { return 2 + (ring - 1) * segments + segment % segments; };
    const int south = 2 + (rings - 1) * segments;
    const auto face = [&](int a, int b, int c) { obj += "f " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) + "\n"; };
    for (int segment = 0; segment < segments; ++segment) face(1, at(1, segment + 1), at(1, segment));
    for (int ring = 1; ring + 1 < rings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            face(at(ring, segment), at(ring, segment + 1), at(ring + 1, segment));
            face(at(ring, segment + 1), at(ring + 1, segment + 1), at(ring + 1, segment));
        }
    }
    for (int segment = 0; segment < segments; ++segment) face(south, at(rings - 1, segment), at(rings - 1, segment + 1));
    return obj;
}

void WaitForBuild(StaticMeshEditorSession& session) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (session.Building()) {
        Require(std::chrono::steady_clock::now() < deadline, "The Static Mesh build did not finish");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        static_cast<void>(session.Tick(0.01F));
    }
}

bool Decreasing(const std::vector<std::uint64_t>& values) {
    for (std::size_t index = 1U; index < values.size(); ++index) {
        if (values[index] >= values[index - 1U]) return false;
    }
    return true;
}

void WriteBitmap(const std::filesystem::path& path, const BITMAPINFOHEADER& header, const void* pixels, std::size_t bytes) {
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(header);
    file.bfSize = static_cast<DWORD>(file.bfOffBits + bytes);
    std::ofstream output{ path, std::ios::binary };
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(bytes));
    Require(output.good(), "The Static Mesh snapshot did not write");
}

} // namespace

void RunEditorStaticMeshTests() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("kb_static_mesh_editor_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "Source");
    std::ofstream{ root / "Source" / "Ball.obj" } << SphereObj(48, 32);
    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root / "Project"), "The Static Mesh test project did not mount");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const auto imported = kb::assets::AssetImportService::ImportFiles(manager, std::array{ root / "Source" / "Ball.obj" }, "/Game/Models");
    Require(imported.Succeeded() && imported.items.size() == 1U, "The Static Mesh test model did not import");
    const kb::assets::AssetMetadata metadata = *manager.Registry().Find(imported.items.front().id);
    const std::filesystem::path container = metadata.physicalPath;
    Require(StaticMeshEditorSession::CanOpen(metadata) && EditorAssetOpenPolicy::CanOpen(metadata),
        "A model asset cannot be opened in the Static Mesh window");

    // Opening reads the stored settings and builds the chain on a worker.
    StaticMeshEditorSession session;
    Require(session.Open(scene, metadata) && session.ConsumeOpenRequest() && !session.ConsumeOpenRequest(),
        "The Static Mesh window did not open its asset once");
    Require(session.Editable() && session.Building() && !session.Modified() &&
            session.Draft() == kb::assets::AssetImportService::ReadMeshLod(container),
        "Opening did not read the stored levels of detail and start a build");
    WaitForBuild(session);
    Require(session.Problem().empty() && session.PreviewScene() != nullptr && session.Summary().levelTriangles.size() > 1U &&
            Decreasing(session.Summary().levelTriangles),
        "The default chain was not built and previewed");
    const std::uint64_t finest = session.Summary().levelTriangles.front();
    Require(finest > 2000U && session.Summary().vertices > 0U && session.Summary().sections >= 1U, "The mesh statistics are missing");

    // Auto to Manual keeps every level where it is; an edit then Apply writes the container and rebuilds the chain.
    const kb::assets::AssetMeshLodLevel autoSecond = session.Draft().Level(2U);
    session.SetManual(true);
    Require(session.Draft().manual && session.Draft().levels[1] == autoSecond && session.Modified(),
        "Switching to Manual moved a level");
    session.SetLevelCount(4U);
    session.Draft().levels[0] = { .trianglePercent = 60.0F, .errorPercent = 20.0F };
    session.Draft().levels[1] = { .trianglePercent = 30.0F, .errorPercent = 20.0F };
    session.Draft().levels[2] = { .trianglePercent = 10.0F, .errorPercent = 20.0F };
    Require(StaticMeshEditorSession::Validate(session.Draft()).empty(), "Valid Manual levels were refused");
    const kb::assets::AssetMeshLodSettings manual = session.Draft();
    Require(session.Generate(manager).empty() && session.Building() && !session.Modified() &&
            kb::assets::AssetImportService::ReadMeshLod(container) == manual,
        "Apply did not write the levels into the asset");
    WaitForBuild(session);
    const std::vector<std::uint64_t>& levels = session.Summary().levelTriangles;
    Require(session.Problem().empty() && levels.size() >= 2U && Decreasing(levels) &&
            levels[1] * 100U > levels[0] * 30U && levels[1] * 100U < levels[0] * 80U,
        "The Manual chain does not follow the stored levels");

    // Settings that cannot be stored are refused with a reason and leave the asset alone.
    session.Draft().levels[1].trianglePercent = 70.0F;
    const std::string reason = StaticMeshEditorSession::Validate(session.Draft());
    Require(!reason.empty() && reason.find("LOD2") != std::string::npos && session.Generate(manager) == reason &&
            session.Notice() == reason && !session.Building() && kb::assets::AssetImportService::ReadMeshLod(container) == manual,
        "Levels that do not fall were not refused with their reason");
    session.Draft() = manual;
    session.SetManual(false);
    session.Draft().keepPercent = 100.0F;
    Require(!StaticMeshEditorSession::Validate(session.Draft()).empty(), "An Auto share of 100% was accepted");
    session.Draft().keepPercent = 50.0F;

    // The ruler pins one level or returns to the distance-based choice.
    const std::uint64_t revision = session.PreviewRevision();
    session.SetPinned(1U);
    Require(session.Pinned() == 1U && session.PreviewRevision() != revision, "Pinning a level did not change the preview");
    session.SetPinned(std::nullopt);
    Require(!session.Pinned().has_value(), "Auto did not release the pinned level");

    // A mesh that is not an imported model opens read-only with the reason.
    Require(!kb::assets::BuiltInShapes().empty(), "The engine has no built-in shapes");
    const kb::assets::AssetMetadata shapeMetadata = kb::assets::BuiltInShapeMetadata(kb::assets::BuiltInShapes().front());
    Require(manager.RegisterAsset(shapeMetadata), "A built-in shape did not register");
    StaticMeshEditorSession shape;
    Require(shape.Open(scene, shapeMetadata), "A built-in shape did not open");
    WaitForBuild(shape);
    Require(!shape.Editable() && !shape.Problem().empty() && !shape.Generate(manager).empty() && shape.PreviewScene() != nullptr &&
            shape.Summary().levelTriangles.size() == 1U,
        "A built-in shape must preview but refuse editing");

    // The window paints from its session, and what it paints can be clicked.
    session.SetPinned(1U);
    session.Ui().advancedOpen = true;
    const RECT content{ 0, 0, 1180, 700 };
    BITMAPINFO info{};
    info.bmiHeader = { sizeof(BITMAPINFOHEADER), content.right, -content.bottom, 1, 32, BI_RGB, 0, 0, 0, 0, 0 };
    void* pixels = nullptr;
    const HBITMAP image = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    const HDC dc = CreateCompatibleDC(nullptr);
    Require(image != nullptr && dc != nullptr && pixels != nullptr, "The Static Mesh snapshot surface did not initialize");
    const HGDIOBJ previous = SelectObject(dc, image);
    StaticMeshEditorPanel::Paint(dc, content, session);
    GdiFlush();
    const auto* data = static_cast<const std::uint32_t*>(pixels);
    const RECT viewport = StaticMeshEditorPanel::ViewportRect(content);
    const std::size_t active = [&] {
        std::size_t count = 0U;
        for (LONG y = viewport.bottom; y < content.bottom; ++y) {
            for (LONG x = 0; x < viewport.right; ++x) count += (data[static_cast<std::size_t>(y) * content.right + x] & 0xFFFFFFU) == 0x234B70U ? 1U : 0U;
        }
        return count;
    }();
    Require(active > 200U, "The pinned level cell was not painted as active");
    char directory[MAX_PATH]{};
    const bool toDirectory = GetEnvironmentVariableA("KB_EDITOR_TEST_SNAPSHOT_DIR", directory, MAX_PATH) > 0U;
    const std::filesystem::path snapshot = (toDirectory ? std::filesystem::path{ directory } : root) / "static-mesh-editor.bmp";
    WriteBitmap(snapshot, info.bmiHeader, pixels, static_cast<std::size_t>(content.right) * content.bottom * 4U);
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(image);

    std::set<std::int64_t> pinnedByClick;
    for (LONG x = 0; x < viewport.right; x += 4) {
        session.SetPinned(std::nullopt);
        if (StaticMeshEditorPanel::PointerDown(content, x, viewport.bottom + 30, session, manager)) {
            pinnedByClick.insert(session.Pinned().has_value() ? static_cast<std::int64_t>(*session.Pinned()) : -1);
        }
    }
    Require(pinnedByClick.contains(-1) && pinnedByClick.contains(0) && pinnedByClick.contains(static_cast<std::int64_t>(levels.size()) - 1),
        "The ruler cells do not select Auto and every level");

    session.Close();
    shape.Close();
    Require(!session.IsOpen() && !session.Building() && session.PreviewScene() == nullptr, "Closing the window kept its session");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace kb::editor::tests
