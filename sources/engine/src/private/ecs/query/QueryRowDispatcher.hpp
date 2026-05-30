#pragma once

#include "engine/ecs/Query.hpp"

namespace kb::ecs {

class QueryState;

class QueryRowDispatcher {
public:
    static void Execute(const QueryState& state, QueryRawVisitor visitor, void* context);
};

} // namespace kb::ecs
