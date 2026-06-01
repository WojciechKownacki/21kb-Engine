#pragma once

#include <bgfx/bgfx.h>

namespace kb::render {

class SimpleTriangle {
public:
    ~SimpleTriangle();

    SimpleTriangle(const SimpleTriangle&) = delete;
    SimpleTriangle& operator=(const SimpleTriangle&) = delete;

    SimpleTriangle() = default;

    [[nodiscard]] bool Initialize();
    void Shutdown();
    void Submit() const;

private:
    bool initialized_ = false;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_{};
};

} // namespace kb::render
