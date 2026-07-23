#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialGraphShaderPrewarmer.hpp"

#include <cstdint>
#include <vector>

namespace kb::render::tests {
namespace {

void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

// Build a bgfx compiled-shader blob (version 11, i.e. with the texInfo + texFormat uniform fields)
// for the given stage byte, one uniform, and a known bytecode payload. Mirrors what shaderc emits and
// what ShaderD3D11::create consumes, so the parser is exercised against the real on-disk layout.
std::vector<std::uint8_t> BuildBlob(char stage, const std::vector<std::uint8_t>& code) {
    std::vector<std::uint8_t> blob;
    blob.push_back(static_cast<std::uint8_t>(stage));
    blob.push_back(static_cast<std::uint8_t>('S'));
    blob.push_back(static_cast<std::uint8_t>('H'));
    blob.push_back(11U);              // version
    AppendU32(blob, 0x11111111U);     // hashIn
    AppendU32(blob, 0x22222222U);     // hashOut (version >= 6)
    AppendU16(blob, 1U);              // uniform count
    // uniform[0]
    blob.push_back(3U);               // nameSize
    blob.push_back(static_cast<std::uint8_t>('s'));
    blob.push_back(static_cast<std::uint8_t>('_'));
    blob.push_back(static_cast<std::uint8_t>('x'));
    blob.push_back(0U);               // type
    blob.push_back(1U);               // num
    AppendU16(blob, 0U);              // regIndex
    AppendU16(blob, 1U);              // regCount
    AppendU16(blob, 0U);              // texInfo (version >= 8)
    AppendU16(blob, 0U);              // texFormat (version >= 10)
    AppendU32(blob, static_cast<std::uint32_t>(code.size())); // shaderSize
    blob.insert(blob.end(), code.begin(), code.end());
    blob.push_back(0U);               // trailing null (ShaderD3D11 skips shaderSize + 1)
    blob.push_back(0U);               // numAttrs = 0
    return blob;
}

void RunFragmentBlobParsesToBytecode() {
    const std::vector<std::uint8_t> code{ 0xDEU, 0xADU, 0xBEU, 0xEFU, 0x01U, 0x02U };
    const std::vector<std::uint8_t> blob = BuildBlob('F', code);

    const std::optional<PrewarmShaderBytecode> parsed = ExtractBgfxShaderBytecode(std::span<const std::uint8_t>{ blob.data(), blob.size() });
    Require(parsed.has_value(), "A well-formed FSH blob must parse");
    Require(parsed->stage == PrewarmShaderStage::Fragment, "Stage byte 'F' must decode to Fragment");
    Require(parsed->size == static_cast<std::uint32_t>(code.size()), "Extracted bytecode size must match the declared shaderSize");
    Require(parsed->code == blob.data() + (blob.size() - code.size() - 2U), "Bytecode must point just past the uniform table + shaderSize field");
    const auto* bytes = static_cast<const std::uint8_t*>(parsed->code);
    Require(bytes[0] == 0xDEU && bytes[1] == 0xADU && bytes[2] == 0xBEU && bytes[5] == 0x02U, "Bytecode payload must be the exact DXBC bytes");
}

void RunVertexStageIsDiscriminated() {
    const std::vector<std::uint8_t> code{ 0xAAU, 0xBBU };
    const std::vector<std::uint8_t> blob = BuildBlob('V', code);
    const std::optional<PrewarmShaderBytecode> parsed = ExtractBgfxShaderBytecode(std::span<const std::uint8_t>{ blob.data(), blob.size() });
    Require(parsed.has_value(), "A well-formed VSH blob must parse");
    // Negative control against a constant/always-Fragment bug: the stage must actually reflect the magic.
    Require(parsed->stage == PrewarmShaderStage::Vertex, "Stage byte 'V' must decode to Vertex, not Fragment");
}

void RunGarbageMagicRejected() {
    // "BM\0\0" (a BMP header) is not a shader blob and must be rejected, not misparsed.
    const std::vector<std::uint8_t> blob{ 'B', 'M', 0U, 0U, 1U, 2U, 3U, 4U };
    const std::optional<PrewarmShaderBytecode> parsed = ExtractBgfxShaderBytecode(std::span<const std::uint8_t>{ blob.data(), blob.size() });
    Require(!parsed.has_value(), "Non-shader magic must be rejected");
}

void RunTruncatedBytecodeRejected() {
    // Declare a 100-byte payload but supply only 4 bytes: the bounds check must fail the parse rather
    // than hand back an out-of-range span that would feed garbage to CreatePixelShader.
    std::vector<std::uint8_t> blob = BuildBlob('F', std::vector<std::uint8_t>{ 1U, 2U, 3U, 4U });
    // Overwrite the shaderSize field (4 bytes immediately before the 4-byte payload + 2 trailing bytes).
    const std::size_t shaderSizeOffset = blob.size() - 4U - 2U - 4U;
    blob[shaderSizeOffset + 0U] = 100U;
    blob[shaderSizeOffset + 1U] = 0U;
    blob[shaderSizeOffset + 2U] = 0U;
    blob[shaderSizeOffset + 3U] = 0U;
    const std::optional<PrewarmShaderBytecode> parsed = ExtractBgfxShaderBytecode(std::span<const std::uint8_t>{ blob.data(), blob.size() });
    Require(!parsed.has_value(), "A shaderSize past the end of the blob must be rejected");
}

void RunEmptyBlobRejected() {
    const std::optional<PrewarmShaderBytecode> parsed = ExtractBgfxShaderBytecode(std::span<const std::uint8_t>{});
    Require(!parsed.has_value(), "An empty blob must be rejected");
}

} // namespace

void RunShaderPrewarmParseTests() {
    RunFragmentBlobParsesToBytecode();
    RunVertexStageIsDiscriminated();
    RunGarbageMagicRejected();
    RunTruncatedBytecodeRejected();
    RunEmptyBlobRejected();
}

} // namespace kb::render::tests
