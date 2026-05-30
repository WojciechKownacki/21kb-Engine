#include "ScenePrefabTestSuites.hpp"
#include "TestSuites.hpp"

namespace kb::tests {

void RunScenePrefabTests() {
    RunScenePrefabInstantiationTests();
    RunScenePrefabCaptureTests();
}

} // namespace kb::tests
