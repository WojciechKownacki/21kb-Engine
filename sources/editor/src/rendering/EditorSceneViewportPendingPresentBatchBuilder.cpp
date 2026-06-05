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

        const std::uint64_t viewportKey = present.session == nullptr ? present.settings.viewportKey : present.session->key;
        PendingPresentBatch* batch = FindBatch(batches, present.host, viewportKey);
        if (batch == nullptr) {
            PendingPresentBatch created{};
            created.host = present.host;
            created.viewportKey = viewportKey;
            created.surfaceRect = present.surfaceRect;
            batches.push_back(std::move(created));
            batch = &batches.back();
        } else {
            UnionRect(&batch->surfaceRect, &batch->surfaceRect, &present.surfaceRect);
        }
        batch->presents.push_back(&present);
    }

    return batches;
}

EditorSceneBgfxViewport::PendingPresentBatch* EditorSceneBgfxViewport::PendingPresentBatchBuilder::FindBatch(
    std::vector<PendingPresentBatch>& batches,
    HWND host,
    std::uint64_t viewportKey) noexcept {
    const auto iter = std::ranges::find_if(batches, [host, viewportKey](const PendingPresentBatch& batch) {
        return batch.host == host && batch.viewportKey == viewportKey;
    });
    return iter == batches.end() ? nullptr : &*iter;
}

} // namespace kb::editor

#endif
