#include <bgfx_shader.sh>
#include "gbuffer_contract.sh"
#include "pbr_builtin_layers.sh"

SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_metallicRoughness, 2);
SAMPLER2D(s_occlusion, 3);
SAMPLER2D(s_emissive, 4);
uniform vec4 u_materialParams;
uniform vec4 u_materialEmissive;
uniform vec4 u_materialFlags;
uniform vec4 u_materialUvTransform;

void main()
{
    vec2 materialUv = v_texcoord0 * u_materialUvTransform.xy + u_materialUvTransform.zw;
    vec4 albedo = texture2D(s_albedo, materialUv) * v_color0;
    if (u_materialFlags.x > 0.5 && u_materialFlags.x < 1.5 && albedo.a < u_materialParams.w) {
        discard;
    }

    vec3 normal = normalize(v_normal);
    if (u_materialParams.z > 0.0) {
        vec3 normalSample = texture2D(s_normal, materialUv).xyz * 2.0 - 1.0;
        normalSample.xy *= u_materialParams.z;
        normal = normalize(v_tangent * normalSample.x + v_bitangent * normalSample.y + normal * normalSample.z);
    }

    if (u_materialFlags.z > 0.5 && !gl_FrontFacing) normal = -normal;
    vec4 metallicRoughness = texture2D(s_metallicRoughness, materialUv);
    float metallic = clamp(u_materialParams.x * metallicRoughness.b, 0.0, 1.0);
    float roughness = clamp(u_materialParams.y * metallicRoughness.g, 0.04, 1.0);
    float occlusionSample = texture2D(s_occlusion, materialUv).r;
    float occlusion = mix(1.0, occlusionSample, clamp(u_materialFlags.y, 0.0, 1.0));
    vec3 emissive = texture2D(s_emissive, materialUv).rgb * u_materialEmissive.rgb * u_materialEmissive.a;

    vec3 layers = KbBuiltinLayers(materialUv);
    float model = KB_GBUFFER_SHADING_MODEL_DEFAULT_LIT;
    float custom0 = 0.0;
    float custom1 = 0.0;
#if defined(KB_BUILTIN_LAYERED_GBUFFER)
    // RGB scattering tint and its strength; coat weight/roughness occupy the normal model slots.
    model = KB_GBUFFER_SHADING_MODEL_LAYERED_PBR;
    custom0 = layers.x;
    custom1 = layers.y;
    gl_FragData[4] = vec4(u_materialSubsurface.rgb, layers.z);
#else
    // Four-target fallback: coat takes precedence when both layers are authored.
    // A subsurface-only surface retains its strength and uses the base color as scattering tint.
    if (layers.x > 0.0) {
        model = KB_GBUFFER_SHADING_MODEL_CLEAR_COAT;
        custom0 = layers.x;
        custom1 = layers.y;
    } else if (layers.z > 0.0) {
        model = KB_GBUFFER_SHADING_MODEL_SUBSURFACE;
        custom0 = layers.z;
    }
#endif
    gl_FragData[0] = vec4(albedo.rgb, custom0);
    gl_FragData[1] = vec4(normal * 0.5 + 0.5, KbEncodeGBufferCustomData1(custom1));
    gl_FragData[2] = vec4(metallic, roughness, occlusion, KbEncodeGBufferShadingModel(model));
    gl_FragData[3] = vec4(emissive, 0.5);
}
