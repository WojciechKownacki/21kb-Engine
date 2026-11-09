#include "EcsTestSuites.hpp"

#include <cstdlib>

namespace kb::tests { void RunSceneDeterministicReplayTests(); }

int main() {
    kb::tests::RunEcsDeterministicReplayTests();
    kb::tests::RunSceneDeterministicReplayTests();
    return EXIT_SUCCESS;
}
