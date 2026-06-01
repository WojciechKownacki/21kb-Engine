#include "kb/render/SimpleTriangle.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/ViewIdPolicy.hpp"

namespace kb::render {
namespace {

struct PosColorVertex {
    float x;
    float y;
    float z;
    std::uint32_t abgr;
};

constexpr PosColorVertex kTriangleVertices[] = {
    {0.0F, 0.5F, 0.0F, 0xff0000ffU},
    {-0.5F, -0.5F, 0.0F, 0xff00ff00U},
    {0.5F, -0.5F, 0.0F, 0xffff0000U},
};

constexpr std::uint16_t kTriangleIndices[] = {
    0,
    1,
    2,
};

} // namespace

SimpleTriangle::~SimpleTriangle() {
    Shutdown();
}

void SimpleTriangle::Shutdown() {
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(vertexBuffer_)) {
        bgfx::destroy(vertexBuffer_);
        vertexBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(indexBuffer_)) {
        bgfx::destroy(indexBuffer_);
        indexBuffer_ = BGFX_INVALID_HANDLE;
    }
    initialized_ = false;
}

bool SimpleTriangle::Initialize() {
    if (initialized_) {
        return true;
    }

    program_ = ShaderLoader::LoadProgram("vs_mesh.sc", "fs_mesh.sc");
    if (!bgfx::isValid(program_)) {
        return false;
    }

    layout_
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    vertexBuffer_ = bgfx::createVertexBuffer(bgfx::makeRef(kTriangleVertices, sizeof(kTriangleVertices)), layout_);
    indexBuffer_ = bgfx::createIndexBuffer(bgfx::makeRef(kTriangleIndices, sizeof(kTriangleIndices)));
    if (!bgfx::isValid(vertexBuffer_) || !bgfx::isValid(indexBuffer_)) {
        Shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

void SimpleTriangle::Submit() const {
    if (!initialized_) {
        return;
    }

    float identity[16] = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };

    bgfx::setTransform(identity);
    bgfx::setState(BGFX_STATE_DEFAULT);
    bgfx::setVertexBuffer(0, vertexBuffer_);
    bgfx::setIndexBuffer(indexBuffer_);
    bgfx::submit(ViewId::Scene3D, program_);
}

} // namespace kb::render
