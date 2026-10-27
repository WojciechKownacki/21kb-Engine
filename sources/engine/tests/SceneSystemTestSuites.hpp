#pragma once

namespace kb::tests {

void RunSystemLifecycleTests();
void RunSceneSystemTransformSyncTests();
void RunSceneTransformLookupTests();
void RunSceneSingleCreateObjectAllocationTest();
void RunPhysicsSceneSystemTests();
void RunAudioSceneSystemTests();
void RunSceneSystemParallelTests();
void RunScriptNetApiTests();
void RunSceneSystemParallelBenchmark();

} // namespace kb::tests
