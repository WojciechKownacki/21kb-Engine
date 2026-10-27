#include "SceneSystemTestSuites.hpp"
#include "TestSuites.hpp"

namespace kb::tests {

void RunSceneSystemTests() {
    RunSystemLifecycleTests();
    RunSceneSystemTransformSyncTests();
    RunPhysicsSceneSystemTests();
    RunAudioSceneSystemTests();
    RunSceneSystemParallelTests();
    RunScriptNetApiTests();
}

} // namespace kb::tests
