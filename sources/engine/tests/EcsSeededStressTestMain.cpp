#include "EcsTestSuites.hpp"

#include <cstdlib>

int main() {
    kb::tests::RunEcsSeededStressTests();
    return EXIT_SUCCESS;
}
