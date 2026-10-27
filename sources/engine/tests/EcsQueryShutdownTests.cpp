#include "engine/ecs/World.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
struct QueryValue {
    float value = 0.0F;
};

void Require(bool condition) {
    if (!condition) {
        std::fputs("Query shutdown fixture did not create a valid query\n", stderr);
        std::abort();
    }
}

// Constructed before main initializes the cache, so its query is released after cache teardown.
struct StaticQueryOwner {
    std::unique_ptr<kb::ecs::World> world;
    kb::ecs::Query<QueryValue> query;

    ~StaticQueryOwner() {
        query = {};
        world.reset();
        std::fputs("query_cache_shutdown late_release=complete\n", stderr);
    }
} gOwner;
} // namespace

int main() {
    gOwner.world = std::make_unique<kb::ecs::World>();
    {
        auto reusable = gOwner.world->CreateQuery<QueryValue>();
        Require(reusable.IsValid());
    }
    gOwner.query = gOwner.world->CreateQuery<QueryValue>();
    Require(gOwner.query.IsValid());
    {
        auto cached = gOwner.world->CreateQuery<QueryValue>();
        Require(cached.IsValid());
    }
    std::fputs("query_cache_shutdown active_query=retained cached_query=released\n", stderr);
    return EXIT_SUCCESS;
}
