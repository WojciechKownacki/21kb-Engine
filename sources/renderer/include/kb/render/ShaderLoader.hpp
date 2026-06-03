#pragma once

#include <bgfx/bgfx.h>

namespace kb::render {

class ShaderLoader {
public:
    ShaderLoader() = delete;

    [[nodiscard]] static bgfx::ShaderHandle Load(const char* name);
    [[nodiscard]] static bgfx::ProgramHandle LoadProgram(const char* vertexShader, const char* fragmentShader);
    [[nodiscard]] static bgfx::ProgramHandle LoadComputeProgram(const char* computeShader);
};

} // namespace kb::render
