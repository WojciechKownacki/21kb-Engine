#pragma once

namespace kb::tests {

void RunAssetRuntimeTests();
void RunAssetBakeTests();
void RunAssetPackTests();
void RunContentStreamingTests();
void RunSaveGameTests();
void RunSecurityTests();
void RunEcsRuntimeTests();
void RunSceneHierarchyTests();
void RunSceneHierarchyParentingTests();
void RunTransformWriteBenchmark();
void RunSceneUITests();
void RunSceneUIBuildFrameBenchmark();
void RunSceneSystemTests();
void RunScenePrefabTests();
void RunProjectSceneTests();
void RunLargeNonAdditiveSceneTransitionBenchmark();
void RunScriptRuntimeTests();
void RunScriptCollectionsTests();
void RunScriptSaveTests();
void RunScriptApiCatalogTests();
void RunVisualGraphTests();
void RunInputTests();
void RunEngineModuleTests();
void RunEngineLibraryTests();
void RunEngineMathTests();
void RunStaticLightmapBakerTests();
void RunAnimationRuntimeTests();
void RunSkeletonAssetTests();
void RunSkeletalMeshAssetTests();
void RunTimelineRuntimeTests();
void RunBlendSpace2DRuntimeTests();
void RunLocalizationTests();
void RunUITextLineBreakingTests();
void RunUIThemeTests();
void RunVideoMediaConversionTests();
void RunCrashReportConsentTests();
void RunNavigationRuntimeTests();
void RunNavigationMeshTests();
void RunNavigationCrowdBenchmark();
void RunNavigationHordeBenchmark();
void RunPortalVisibilityTests();
void RunMotionSkeletonRuleTests();
void RunWorldPartitionTests();
void RunLargeWorldTests();
void RunSceneBehaviourVariableTests();

} // namespace kb::tests
