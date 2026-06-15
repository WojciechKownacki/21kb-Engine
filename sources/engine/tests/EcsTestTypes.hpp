#pragma once

#include <cstddef>

struct EcsPosition {
    float x = 0.0F;
    float y = 0.0F;
};

struct EcsVelocity {
    float x = 0.0F;
    float y = 0.0F;
};

struct EcsQueryMarker {
    int value = 0;
};

struct EcsDisabled {
    int value = 0;
};

struct EcsIterationCounters {
    int visited = 0;
    float sumX = 0.0F;
};

struct EcsBatchCounters {
    int batches = 0;
    int visited = 0;
    std::size_t maxBatch = 0;
    float sumX = 0.0F;
};
