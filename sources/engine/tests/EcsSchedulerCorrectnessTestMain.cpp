#include "EcsTestSuites.hpp"

#include <cstdlib>

int main() {
    kb::tests::RunEcsSystemSchedulerTests();
    kb::tests::RunEcsWorkerPoolTests();
    return EXIT_SUCCESS;
}
