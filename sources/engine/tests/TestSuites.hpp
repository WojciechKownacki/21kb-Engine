#pragma once

namespace kb::tests {

void RunAssetRuntimeTests();
void RunAssetBakeTests();
void RunAssetPackTests();
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

} // namespace kb::tests
