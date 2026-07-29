#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace kb::core {
struct ReadSnapshot { std::uint64_t revision=0U; };
struct RuntimeCommand { std::uint64_t target=0U; std::uint32_t kind=0U; };
class CommandQueue final { public: void Enqueue(RuntimeCommand command){pending_.push_back(command);} [[nodiscard]] std::vector<RuntimeCommand> Drain(){auto result=std::move(pending_);pending_.clear();return result;} private:std::vector<RuntimeCommand> pending_; };
} // namespace kb::core
