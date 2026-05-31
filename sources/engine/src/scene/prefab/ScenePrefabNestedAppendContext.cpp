#include "scene/prefab/ScenePrefabNestedAppendContext.hpp"

#include "engine/scene/ScenePrefabHandle.hpp"
#include "scene/prefab/ScenePrefabNestedNodeMapper.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabOuterChildAppender.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] bool ContainsGuid(const std::vector<std::string>& stack, std::string_view guid) {
    return std::find(stack.begin(), stack.end(), guid) != stack.end();
}

} // namespace

ScenePrefabNestedAppendContext::ScenePrefabNestedAppendContext(const ScenePrefabRegistry& registry, const ScenePrefab& source, ScenePrefab& output, std::vector<std::string>& stack)
    : registry_(registry)
    , source_(source)
    , output_(output)
    , stack_(stack)
    , tree_(source) {}

void ScenePrefabNestedAppendContext::Append(std::uint32_t sourceIndex, std::uint32_t outputParent) {
    const ScenePrefabNodeDesc& sourceNode = source_.Nodes()[sourceIndex];
    if (!sourceNode.nestedPrefabGuid.empty() && !ContainsGuid(stack_, sourceNode.nestedPrefabGuid)) {
        const ScenePrefabHandle nestedHandle = registry_.FindByGuid(sourceNode.nestedPrefabGuid);
        const ScenePrefabRecord* nestedRecord = registry_.FindRecord(nestedHandle);
        if (nestedRecord != nullptr) {
            AppendNested(sourceIndex, outputParent, *nestedRecord);
            return;
        }
    }

    ScenePrefabNodeDesc node = sourceNode;
    node.parentNode = outputParent;
    const std::uint32_t outputIndex = output_.AddNode(std::move(node));
    for (const std::uint32_t child : tree_.Children(sourceIndex)) {
        Append(child, outputIndex);
    }
}

void ScenePrefabNestedAppendContext::AppendNested(std::uint32_t sourceIndex, std::uint32_t outputParent, const ScenePrefabRecord& nestedRecord) {
    stack_.push_back(nestedRecord.guid);
    ScenePrefab nestedPrefab = ScenePrefabNestedResolver::Resolve(registry_, nestedRecord.prefab, stack_);
    stack_.pop_back();

    const ScenePrefabNodeDesc& overlayRoot = source_.Nodes()[sourceIndex];
    const std::vector<std::uint32_t> sourceSubtree = tree_.CollectPreorder(sourceIndex);
    ScenePrefabNestedNodeMapping mapping = ScenePrefabNestedNodeMapper::Append(output_, source_, nestedPrefab, overlayRoot, sourceSubtree, outputParent);
    ScenePrefabOuterChildAppender::Append(source_, output_, mapping, outputParent);
}

} // namespace kb::scene
