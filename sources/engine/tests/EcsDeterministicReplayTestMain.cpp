#include "EcsTestSuites.hpp"

#include <cstdlib>

int main() {
    kb::tests::RunEcsDeterministicReplayTests();
    return EXIT_SUCCESS;
}
