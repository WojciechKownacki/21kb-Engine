#include "profiler/ProfilerText.hpp"

#include "localization/EditorLocalization.hpp"

namespace kb::editor {

std::string_view ProfilerText::Counter(const kb::core::ProfilerCounterInfo& info) {
    const std::string_view id = info.id;
    if (id == "fps") return EditorText("profiler.counter.fps", "FPS");
    if (id == "frame_ms") return EditorText("profiler.counter.frame", "Frame time");
    if (id == "render_thread_ms") return EditorText("profiler.counter.render_thread", "Render thread");
    if (id == "gpu_ms") return EditorText("profiler.counter.gpu", "GPU");
    if (id == "draw_calls") return EditorText("profiler.counter.draw_calls", "Draw calls");
    if (id == "triangles") return EditorText("profiler.counter.triangles", "Triangles");
    if (id == "compute_dispatches") return EditorText("profiler.counter.compute", "Compute dispatches");
    if (id == "views") return EditorText("profiler.counter.views", "Render views");
    if (id == "process_memory_mb") return EditorText("profiler.counter.process_memory", "Process memory");
    if (id == "gpu_memory_mb") return EditorText("profiler.counter.gpu_memory", "GPU memory");
    if (id == "texture_memory_mb") return EditorText("profiler.counter.textures", "Textures");
    if (id == "render_target_memory_mb") return EditorText("profiler.counter.render_targets", "Render targets");
    if (id == "ecs_entities") return EditorText("profiler.counter.ecs_entities", "Entities");
    if (id == "ecs_chunks") return EditorText("profiler.counter.ecs_chunks", "Chunks");
    if (id == "ecs_payload_mb") return EditorText("profiler.counter.ecs_payload", "Native payload");
    if (id == "ecs_entity_records_mb") return EditorText("profiler.counter.ecs_records", "Entity records");
    if (id == "ecs_occupancy") return EditorText("profiler.counter.ecs_occupancy", "Chunk occupancy");
    if (id == "ecs_storage_allocations") return EditorText("profiler.counter.ecs_allocations", "Storage allocations");
    if (id == "ecs_query_cache_hits") return EditorText("profiler.counter.ecs_cache_hits", "Query cache hits");
    if (id == "ecs_query_cache_misses") return EditorText("profiler.counter.ecs_cache_misses", "Query cache misses");
    if (id == "ecs_query_prepare_ms") return EditorText("profiler.counter.ecs_prepare", "Query prepare");
    if (id == "ecs_query_kernels_ms") return EditorText("profiler.counter.ecs_kernels", "Query kernels");
    if (id == "ecs_query_traffic_mb") return EditorText("profiler.counter.ecs_traffic", "Query traffic");
    return info.label;
}

std::string_view ProfilerText::Group(std::string_view englishGroup) {
    if (englishGroup == "Frame") return EditorText("profiler.group.frame", "Frame");
    if (englishGroup == "Drawing") return EditorText("profiler.group.drawing", "Drawing");
    if (englishGroup == "Memory") return EditorText("profiler.group.memory", "Memory");
    if (englishGroup == "ECS") return EditorText("profiler.group.ecs", "ECS");
    return EditorText("profiler.group.custom", "Systems of the scene");
}

std::string_view ProfilerText::Area(kb::core::ProfilerArea area) {
    switch (area) {
    case kb::core::ProfilerArea::Cpu: return EditorText("profiler.area.cpu", "CPU");
    case kb::core::ProfilerArea::Gpu: return EditorText("profiler.area.gpu", "GPU");
    case kb::core::ProfilerArea::Render: return EditorText("profiler.area.render", "Rendering");
    case kb::core::ProfilerArea::Ecs: return EditorText("profiler.area.ecs", "ECS");
    case kb::core::ProfilerArea::Memory: return EditorText("profiler.area.memory", "Memory");
    }
    return {};
}

} // namespace kb::editor
