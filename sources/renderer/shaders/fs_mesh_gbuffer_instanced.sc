$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal

#include <bgfx_shader.sh>
#include "gbuffer_contract.sh"

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

    vec4 metallicRoughness = texture2D(s_metallicRoughness, materialUv);
    float metallic = clamp(u_materialParams.x * metallicRoughness.b, 0.0, 1.0);
    float roughness = clamp(u_materialParams.y * metallicRoughness.g, 0.04, 1.0);
    float occlusionSample = texture2D(s_occlusion, materialUv).r;
    float occlusion = mix(1.0, occlusionSample, clamp(u_materialFlags.y, 0.0, 1.0));
    vec3 emissive = texture2D(s_emissive, materialUv).rgb * u_materialEmissive.rgb * u_materialEmissive.a;

    gl_FragData[0] = vec4(albedo.rgb, albedo.a);
    gl_FragData[1] = vec4(normal * 0.5 + 0.5, 1.0);
    gl_FragData[2] = vec4(metallic, roughness, occlusion, KbEncodeGBufferShadingModel(KB_GBUFFER_SHADING_MODEL_DEFAULT_LIT));
    gl_FragData[3] = vec4(emissive, 0.5);
}
