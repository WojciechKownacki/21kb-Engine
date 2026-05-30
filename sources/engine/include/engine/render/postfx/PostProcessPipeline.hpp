#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::render::postfx {

struct ResourceHandle {
    std::uint32_t id = 0;

    [[nodiscard]] bool IsValid() const noexcept { return id != 0; }

    auto operator<=>(const ResourceHandle&) const = default;
};

struct TextureDesc {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t mipCount = 1;
    std::uint8_t format = 0;
    std::uint64_t flags = 0;
};

struct ResourceDesc {
    std::string name;
    TextureDesc texture;
};

struct PassContext {
    std::unordered_map<std::uint32_t, ResourceDesc>* resources = nullptr;
};

class PostProcessPipeline {
public:
    using ExecuteFn = std::function<void(const PassContext&)>;

    struct PassDesc {
        std::string name;
        std::vector<ResourceHandle> reads;
        std::vector<ResourceHandle> writes;
        ExecuteFn execute;
    };

    PostProcessPipeline();
    ~PostProcessPipeline();

    PostProcessPipeline(const PostProcessPipeline& other);
    PostProcessPipeline& operator=(const PostProcessPipeline& other);
    PostProcessPipeline(PostProcessPipeline&&) noexcept;
    PostProcessPipeline& operator=(PostProcessPipeline&&) noexcept;

    ResourceHandle RegisterResource(ResourceDesc desc);
    void AddPass(PassDesc desc);
    void Compile();
    void Execute();

    [[nodiscard]] const std::vector<std::uint32_t>& GetExecutionOrder() const noexcept;

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace kb::render::postfx
