#pragma once

#include <string_view>

namespace kb::scene {

enum class ScenePrefabAssetKind {
    Template,
    Variant,
};

struct ScenePrefabAssetFormat {
    static constexpr std::string_view Header = "21kb.prefab.v1";
    static constexpr std::string_view HeaderV2 = "21kb.prefab.v2";

    static constexpr std::string_view KindKey = "kind";
    static constexpr std::string_view TemplateKind = "template";
    static constexpr std::string_view VariantKind = "variant";
    static constexpr std::string_view GuidKey = "guid";
    static constexpr std::string_view NameKey = "name";
    static constexpr std::string_view BaseGuidKey = "baseGuid";
    static constexpr std::string_view OverridesKey = "overrides";
    static constexpr std::string_view OverrideMarker = "override";
    static constexpr std::string_view EndOverrideMarker = "endoverride";
    static constexpr std::string_view OverrideNodeKey = "node";
    static constexpr std::string_view OverrideNodeIdKey = "nodeId";
    static constexpr std::string_view OverridePropertyPathKey = "propertyPath";
    static constexpr std::string_view OverrideValueKey = "value";
    static constexpr std::string_view OverrideObjectReferenceKey = "objectReference";
    // LIB-092: the stable, within-instance node id of a "parent" override's
    // new-parent target — the portable counterpart to
    // OverrideObjectReferenceKey (a raw runtime entity id, which does not
    // survive a save/load round trip and is written only for
    // debugging/back-compat, never read back).
    static constexpr std::string_view OverrideObjectReferenceNodeIdKey = "objectReferenceNodeId";
    static constexpr std::string_view OverrideFlagKey = "flag";
    // LIB-092: a variant's added-child subtrees (ScenePrefabVariantAddedSubtree)
    // — the structural counterpart to the property override list. Each subtree
    // is a whole captured node list nested under a base node identified by its
    // stable id. The count key is optional and absent from variant files
    // written before this feature, so its absence reads as "zero added
    // subtrees" (a clean back-compat boundary), never a parse error.
    static constexpr std::string_view AddedChildrenKey = "addedChildren";
    static constexpr std::string_view AddedChildMarker = "addedchild";
    static constexpr std::string_view EndAddedChildMarker = "endaddedchild";
    static constexpr std::string_view AddedChildHostNodeIdKey = "hostNodeId";
    static constexpr std::string_view NestedPrefabGuidKey = "nestedPrefabGuid";
    static constexpr std::string_view NestedOverrideCountKey = "nestedOverrideCount";
    static constexpr std::string_view NestedOverridePrefix = "nestedOverride.";
    static constexpr std::string_view NodesKey = "nodes";
    static constexpr std::string_view NodeStableIdKey = "id";
    static constexpr std::string_view ParentKey = "parent";
    static constexpr std::string_view LocalPositionKey = "localPosition";
    static constexpr std::string_view LocalRotationKey = "localRotation";
    static constexpr std::string_view LocalScaleKey = "localScale";
    static constexpr std::string_view VisibleKey = "visible";

    static constexpr std::string_view NodeMarker = "node";
    static constexpr std::string_view EndNodeMarker = "endnode";
};

} // namespace kb::scene
