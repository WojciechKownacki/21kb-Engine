#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorHierarchyRowModel.hpp"
#include "scene/EditorHierarchySelectionState.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>

namespace {

using kb::editor::EditorHierarchyRow;
using kb::editor::EditorHierarchyRowBuilder;
using kb::editor::EditorHierarchyRowModel;
using kb::scene::SceneEntity;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kViewRows = 60U;

[[nodiscard]] double Ms(Clock::time_point from) {
    return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

[[nodiscard]] double Median(std::vector<double> values) {
    std::ranges::sort(values);
    return values[values.size() / 2U];
}

[[nodiscard]] std::size_t PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    return counters.PrivateUsage;
}

[[nodiscard]] std::string Environment(const char* name) {
    char* value = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result{ value };
    std::free(value);
    return result;
}

// A flat scene of roots, or a nested one of about three levels with the same fan-out at each level.
struct Population {
    std::vector<kb::scene::SceneObject> objects;
    std::size_t fan = 0U;
};

[[nodiscard]] Population Populate(kb::scene::Scene& scene, std::size_t count, bool nested) {
    Population population;
    population.fan = nested ? static_cast<std::size_t>(std::ceil(std::cbrt(static_cast<double>(count)))) : 0U;
    population.objects.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        kb::scene::SceneObjectDesc desc{ .name = "Entity " + std::to_string(index) };
        if (nested && index >= population.fan) {
            desc.parent = population.objects[(index - population.fan) / population.fan];
        }
        population.objects.push_back(scene.Entities().CreateObject(desc));
    }
    return population;
}

// The rows the model lists must be the rows the full builder produces.
void ExpectSameRows(EditorHierarchyRowModel& model, const kb::scene::Scene& scene,
    const EditorHierarchyRowBuilder::CollapsedEntitySet& collapsed, std::string_view query, const char* message) {
    model.Sync(scene, collapsed, query, true);
    const std::vector<EditorHierarchyRow> expected = EditorHierarchyRowBuilder::Build(scene, collapsed, query);
    bool same = true;
    for (std::size_t first = 0U; first < expected.size() && same; first += 97U) {
        const std::vector<EditorHierarchyRow>& window = model.Window(first, 97U);
        same = window.size() == std::min<std::size_t>(97U, expected.size() - first);
        for (std::size_t index = 0U; index < window.size() && same; ++index) {
            const EditorHierarchyRow& want = expected[first + index];
            same = window[index].entity == want.entity && window[index].depth == want.depth &&
                window[index].name == want.name && window[index].hasChildren == want.hasChildren &&
                window[index].expanded == want.expanded;
        }
    }
    kb::editor::tests::Require(same && model.Count() == expected.size(), message);
}

void RunModelMatchesBuilderTest() {
    kb::scene::Scene scene;
    Population population = Populate(scene, 12000U, true);
    EditorHierarchyRowModel model;
    EditorHierarchyRowBuilder::CollapsedEntitySet collapsed;
    ExpectSameRows(model, scene, collapsed, "", "Model rows differ from the builder on a nested 12000 entity scene");
    kb::editor::tests::Require(model.Count() == scene.Entities().Count(), "Row count must equal the entity count with nothing collapsed");

    // Collapsing a few groups, one inside another, hides their rows and keeps the others.
    collapsed.insert(population.objects[3].Entity().Id());
    collapsed.insert(population.objects[5].Entity().Id());
    collapsed.insert(population.objects[(5 + population.fan) * 1U].Entity().Id());
    collapsed.insert(population.objects[population.fan + 40U].Entity().Id());
    ExpectSameRows(model, scene, collapsed, "", "Model rows differ from the builder with collapsed groups");

    // Collapsing the entities that own most rows counts the visible rows instead of the hidden ones.
    for (std::size_t index = 0U; index < population.fan + population.fan * population.fan; ++index) {
        collapsed.insert(population.objects[index].Entity().Id());
    }
    ExpectSameRows(model, scene, collapsed, "", "Model rows differ from the builder with every group collapsed");
}

void RunLargeCollapsedGroupsTest() {
    kb::scene::Scene scene;
    const Population population = Populate(scene, 100000U, true);
    EditorHierarchyRowModel model;
    EditorHierarchyRowBuilder::CollapsedEntitySet collapsed;
    for (std::size_t index = 0U; index < population.fan; ++index) {
        collapsed.insert(population.objects[index].Entity().Id());
    }
    ExpectSameRows(model, scene, collapsed, "", "Model rows differ from the builder with large collapsed groups");
    kb::editor::tests::Require(model.Count() == population.fan, "Collapsing every top group must leave only those rows");
}

void RunSelectionOverModelTest() {
    kb::scene::Scene scene;
    const Population population = Populate(scene, 10000U, true);
    EditorHierarchyRowModel model;
    EditorHierarchyRowBuilder::CollapsedEntitySet collapsed;
    collapsed.insert(population.objects[2].Entity().Id());
    model.Sync(scene, collapsed, "", true);
    const std::vector<EditorHierarchyRow> all = EditorHierarchyRowBuilder::Build(scene, collapsed, "");

    kb::editor::EditorHierarchySelectionState expected;
    kb::editor::EditorHierarchySelectionState actual;
    (void)expected.SelectRow(all, 4200U, false, false);
    (void)actual.SelectRow(model.Window(4200U, 1U), 0U, false, false);
    (void)expected.SelectRow(all, 4210U, false, true);
    const std::size_t anchor = model.IndexOfNear(actual.Anchor(), 4210U);
    kb::editor::tests::Require(anchor == 4200U, "The range anchor is not found at its row");
    (void)actual.SelectRow(model.Window(anchor, 11U), 10U, false, true);
    kb::editor::tests::Require(actual.SelectedEntities() == expected.SelectedEntities() && actual.Primary() == expected.Primary(),
        "A range selection over the model differs from the same selection over every row");
    (void)expected.SelectRow(all, 4100U, true, false);
    (void)actual.SelectRow(model.Window(4100U, 1U), 0U, true, false);
    kb::editor::tests::Require(actual.SelectedEntities() == expected.SelectedEntities() && actual.Primary() == expected.Primary(),
        "A Ctrl selection over the model differs from the same selection over every row");

    kb::editor::tests::Require(model.IndexOfNear(all[9000U].entity, 0U) == 9000U && model.IndexOfNear(all[10U].entity, 9990U) == 10U,
        "An entity is not found at its row from a far hint");
    const SceneEntity hidden = population.objects[2 + population.fan * 3U].Entity();
    kb::editor::tests::Require(!model.IsListed(hidden) && model.IndexOfNear(hidden, 0U) == EditorHierarchyRowModel::kNotListed,
        "An entity under a collapsed group must not be listed");
}

void RunSearchOverModelTest() {
    kb::scene::Scene scene;
    static_cast<void>(Populate(scene, 10000U, true));
    EditorHierarchyRowModel model;
    ExpectSameRows(model, scene, {}, "entity 77", "Search rows differ from the builder");
    kb::editor::tests::Require(model.Count() > 1U, "Search must list the matches and their ancestors");
    ExpectSameRows(model, scene, {}, "", "Clearing the search must list every row again");
}

void RunRefreshAfterChangeTest() {
    kb::scene::Scene scene;
    Population population = Populate(scene, 10000U, true);
    EditorHierarchyRowModel model;
    const EditorHierarchyRowBuilder::CollapsedEntitySet collapsed;
    model.Sync(scene, collapsed, "", true);

    scene.Entities().SetName(population.objects[7000].Entity(), "Renamed");
    ExpectSameRows(model, scene, collapsed, "", "A renamed entity is not shown with its new name");

    const SceneEntity added = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Added", .parent = population.objects[9] });
    kb::editor::tests::Require(added.IsValid(), "Creating a child failed");
    ExpectSameRows(model, scene, collapsed, "", "A child added to a group is not listed");

    // A root appended while the rows stay valid is listed without rebuilding them.
    model.Sync(scene, collapsed, "", true);
    const std::size_t before = model.Count();
    const SceneEntity root = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Appended Root" });
    model.AppendRoot(root);
    kb::editor::tests::Require(model.Count() == before + 1U && model.RowAt(before) != nullptr && model.RowAt(before)->entity == root,
        "An appended root is not the last row");
    ExpectSameRows(model, scene, collapsed, "", "Rows after an appended root differ from a rebuild");

    scene.Entities().Destroy(population.objects[5000]);
    ExpectSameRows(model, scene, collapsed, "", "A destroyed entity is still listed");
    scene.Entities().Destroy(population.objects[1]);
    ExpectSameRows(model, scene, collapsed, "", "A destroyed group still lists its children");
}

void RunReparentOverModelTest() {
    kb::scene::Scene scene;
    const Population population = Populate(scene, 10000U, true);
    EditorHierarchyRowModel model;
    EditorHierarchyRowBuilder::CollapsedEntitySet collapsed;
    kb::editor::tests::Require(scene.Hierarchy().SetParent(population.objects[3000], population.objects[8]) &&
            scene.Hierarchy().SetParent(population.objects[3001], population.objects[8]),
        "Reparenting failed");
    ExpectSameRows(model, scene, collapsed, "", "Reparented entities are not listed under their new group");
    kb::editor::tests::Require(model.IsListed(population.objects[3000].Entity()), "A reparented entity must be listed");
    collapsed.insert(population.objects[8].Entity().Id());
    model.Sync(scene, collapsed, "", true);
    kb::editor::tests::Require(!model.IsListed(population.objects[3000].Entity()), "An entity moved into a collapsed group must not be listed");
    kb::editor::tests::Require(scene.Hierarchy().SetParent(population.objects[3000].Entity(), SceneEntity{}),
        "Moving an entity to the roots failed");
    ExpectSameRows(model, scene, collapsed, "", "An entity moved to the roots is not listed last");
}

void RunTransientRowsOverModelTest() {
    kb::scene::Scene scene;
    const Population population = Populate(scene, 10000U, true);
    kb::scene::MeshRendererComponent transient{};
    transient.transient = true;
    scene.Components().MeshRenderers().Set(population.objects[9000].Entity(), transient);
    scene.Components().MeshRenderers().Set(population.objects[4].Entity(), transient);
    EditorHierarchyRowModel model;
    const EditorHierarchyRowBuilder::CollapsedEntitySet collapsed;
    model.Sync(scene, collapsed, "", true);
    std::size_t walked = 0U;
    model.ForEachEntity([&walked](SceneEntity) { ++walked; });
    const std::vector<EditorHierarchyRow> expected = EditorHierarchyRowBuilder::Build(scene, collapsed, "");
    kb::editor::tests::Require(walked == expected.size() && model.IsListed(population.objects[9000].Entity()) == false,
        "Transient entities and their children must not be listed");
    // The walk met every transient subtree, so the count is exact again.
    kb::editor::tests::Require(model.Count() == expected.size(), "The count must drop the transient subtrees once they are reached");
    ExpectSameRows(model, scene, collapsed, "", "Rows differ from the builder with transient entities");
}

// Time to know the rows for a first show, after one change at the top or in the middle, and for a page scroll.
void RunBench(std::size_t count, bool nested, bool full) {
    kb::scene::Scene scene;
    Population population = Populate(scene, count, nested);
    const EditorHierarchyRowBuilder::CollapsedEntitySet collapsed;
    std::vector<EditorHierarchyRow> rows;
    EditorHierarchyRowModel model;
    // The panel asks for the count and the rows in view after every change.
    const auto show = [&](std::size_t first) {
        if (full) {
            rows = EditorHierarchyRowBuilder::Build(scene, collapsed, "");
            std::vector<EditorHierarchyRow> view(rows.begin() + static_cast<std::ptrdiff_t>(std::min(first, rows.size())),
                rows.begin() + static_cast<std::ptrdiff_t>(std::min(rows.size(), first + kViewRows)));
            return view.size() + rows.size();
        }
        model.Sync(scene, collapsed, "", true);
        return model.Window(first, kViewRows).size() + model.Count();
    };
    const auto scroll = [&](std::size_t first) {
        if (full) {
            std::vector<EditorHierarchyRow> view(rows.begin() + static_cast<std::ptrdiff_t>(std::min(first, rows.size())),
                rows.begin() + static_cast<std::ptrdiff_t>(std::min(rows.size(), first + kViewRows)));
            return view.size();
        }
        return model.Window(first, kViewRows).size();
    };
    std::vector<double> firstShow, renameTop, renameMiddle, add, addChild, remove, page;
    std::size_t memory = 0U;
    std::size_t sink = 0U;
    for (int round = 0; round < 3; ++round) {
        const std::size_t before = PrivateBytes();
        auto start = Clock::now();
        sink += show(0U);
        firstShow.push_back(Ms(start));
        memory = round == 0 ? PrivateBytes() - before : memory;

        const std::size_t middle = count / 2U;
        sink += show(middle);
        start = Clock::now();
        sink += scroll(middle + 40U);
        page.push_back(Ms(start));

        scene.Entities().SetName(population.objects[count / 3U + static_cast<std::size_t>(round)].Entity(), "Renamed");
        start = Clock::now();
        sink += show(0U);
        renameTop.push_back(Ms(start));

        scene.Entities().SetName(population.objects[count / 3U + 10U + static_cast<std::size_t>(round)].Entity(), "Renamed");
        start = Clock::now();
        sink += show(middle);
        renameMiddle.push_back(Ms(start));

        static_cast<void>(scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Added Root" }));
        start = Clock::now();
        sink += show(middle);
        add.push_back(Ms(start));

        static_cast<void>(scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Added Child", .parent = population.objects[nested ? 2U : 0U] }));
        start = Clock::now();
        sink += show(middle);
        addChild.push_back(Ms(start));

        scene.Entities().Destroy(population.objects[count - 1U - static_cast<std::size_t>(round)]);
        start = Clock::now();
        sink += show(middle);
        remove.push_back(Ms(start));
    }
    std::printf("hierarchy-bench mode=%s shape=%s size=%zu first=%.3f renameTop=%.3f renameMiddle=%.3f add=%.3f addChild=%.3f remove=%.3f page=%.3f memMB=%.1f (%zu)\n",
        full ? "full" : "lazy", nested ? "nested" : "flat", count, Median(firstShow), Median(renameTop), Median(renameMiddle),
        Median(add), Median(addChild), Median(remove), Median(page), static_cast<double>(memory) / 1048576.0, sink);
}

void RunBenchSizes() {
    const std::string list = Environment("KB_HIERARCHY_BENCH_SIZES");
    if (list.empty()) {
        return;
    }
    const bool full = !Environment("KB_HIERARCHY_BENCH_FULL").empty();
    std::size_t begin = 0U;
    while (begin < list.size()) {
        std::size_t end = list.find(',', begin);
        end = end == std::string::npos ? list.size() : end;
        const std::size_t count = std::stoull(list.substr(begin, end - begin));
        begin = end + 1U;
        RunBench(count, false, full);
        RunBench(count, true, full);
    }
}

} // namespace

namespace kb::editor::tests {

void RunEditorHierarchyScaleTests() {
    RunModelMatchesBuilderTest();
    RunLargeCollapsedGroupsTest();
    RunSelectionOverModelTest();
    RunSearchOverModelTest();
    RunRefreshAfterChangeTest();
    RunReparentOverModelTest();
    RunTransientRowsOverModelTest();
    RunBenchSizes();
}

} // namespace kb::editor::tests
