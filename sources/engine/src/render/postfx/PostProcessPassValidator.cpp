#include "render/postfx/PostProcessPassValidator.hpp"

#include <stdexcept>
#include <unordered_set>

namespace kb::render::postfx {

void PostProcessPassValidator::Validate(const PostProcessPipeline::PassDesc& desc, const std::unordered_map<std::uint32_t, ResourceDesc>& resources) const {
    if (desc.name.empty()) {
        throw std::invalid_argument("Pass name cannot be empty");
    }

    if (!desc.execute) {
        throw std::invalid_argument("Pass execute callback must be provided");
    }

    std::unordered_set<std::uint32_t> writeSet;
    writeSet.reserve(desc.writes.size());

    for (const auto write : desc.writes) {
        if (!write.IsValid() || !resources.contains(write.id)) {
            throw std::invalid_argument("Pass writes unknown resource");
        }
        if (!writeSet.insert(write.id).second) {
            throw std::invalid_argument("Pass writes same resource more than once");
        }
    }

    for (const auto read : desc.reads) {
        if (!read.IsValid() || !resources.contains(read.id)) {
            throw std::invalid_argument("Pass reads unknown resource");
        }
        if (writeSet.contains(read.id)) {
            throw std::invalid_argument("Read/write hazard in single pass is not allowed");
        }
    }
}

} // namespace kb::render::postfx
