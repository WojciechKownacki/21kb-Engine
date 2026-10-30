#include "EcsTestSuites.hpp"
#include "SceneSystemTestSuites.hpp"

#include <cstdlib>

int main() {
    kb::tests::RunEcsSystemSchedulerTests();
    kb::tests::RunEcsWorkerPoolTests();
    kb::tests::RunSceneSystemParallelTests();
    return EXIT_SUCCESS;
}
