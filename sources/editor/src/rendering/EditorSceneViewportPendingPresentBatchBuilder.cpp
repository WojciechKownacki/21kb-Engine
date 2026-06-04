#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {

std::vector<EditorSceneBgfxViewport::PendingPresentBatch> EditorSceneBgfxViewport::PendingPresentBatchBuilder::Build(
    std::span<const PendingPresent> pendingPresents) {
    std::vector<PendingPresentBatch> batches;
    batches.reserve(pendingPresents.size());

    for (const PendingPresent& present : pendingPresents) {
        if (present.host == nullptr) {
            continue;
        }

        PendingPresentBatch* batch = FindBatch(batches, present.host);
        if (batch == nullptr) {
            PendingPresentBatch created{};
            created.host = present.host;
            created.surfaceRect = present.destination;
            batches.push_back(std::move(created));
            batch = &batches.back();
        } else {
            UnionRect(&batch->surfaceRect, &batch->surfaceRect, &present.destination);
        }
        batch->presents.push_back(&present);
    }

    return batches;
}

EditorSceneBgfxViewport::PendingPresentBatch* EditorSceneBgfxViewport::PendingPresentBatchBuilder::FindBatch(
    std::vector<PendingPresentBatch>& batches,
    HWND host) noexcept {
    const auto iter = std::ranges::find_if(batches, [host](const PendingPresentBatch& batch) {
        return batch.host == host;
    });
    return iter == batches.end() ? nullptr : &*iter;
}

} // namespace kb::editor

#endif
