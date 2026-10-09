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
void RunTransformWriteBenchmark();
void RunSceneUITests();
void RunSceneUIBuildFrameBenchmark();
void RunSceneSystemTests();
void RunScenePrefabTests();
void RunProjectSceneTests();
void RunLargeNonAdditiveSceneTransitionBenchmark();
void RunScriptRuntimeTests();
void RunScriptApiCatalogTests();
void RunVisualGraphTests();
void RunInputTests();
void RunEngineModuleTests();
void RunEngineLibraryTests();
void RunEngineMathTests();
void RunAnimationRuntimeTests();
void RunSkeletonAssetTests();
void RunSkeletalMeshAssetTests();
void RunTimelineRuntimeTests();
void RunLocalizationTests();
void RunUITextLineBreakingTests();
void RunCrashReportConsentTests();
void RunNavigationRuntimeTests();
void RunNavigationMeshTests();
void RunNavigationCrowdBenchmark();
void RunPortalVisibilityTests();
void RunMotionSkeletonRuleTests();
void RunWorldPartitionTests();
void RunLargeWorldTests();

} // namespace kb::tests
