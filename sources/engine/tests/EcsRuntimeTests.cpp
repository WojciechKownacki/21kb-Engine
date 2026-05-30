#include "EcsTestSuites.hpp"
#include "TestSuites.hpp"

namespace kb::tests {

void RunEcsRuntimeTests() {
    RunEcsComponentApiTests();
    RunEcsQueryTests();
    RunEcsConfigTests();
}

} // namespace kb::tests
