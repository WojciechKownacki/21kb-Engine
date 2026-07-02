$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_gbufferAlbedo, 0);
SAMPLER2D(s_gbufferNormal, 1);
SAMPLER2D(s_gbufferMaterial, 2);
uniform vec4 u_lightDirKind[4];
uniform vec4 u_lightColorIntensity[4];
uniform vec4 u_lightParams;
uniform vec4 u_ambientColor;
uniform vec4 u_environmentZenith;
uniform vec4 u_environmentGround;
uniform vec4 u_environmentParams;

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (vec3(1.0, 1.0, 1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EnvironmentColor(vec3 direction)
{
    float hemisphere = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 constantColor = u_ambientColor.rgb;
    vec3 hemisphereColor = mix(u_environmentGround.rgb, u_environmentZenith.rgb, hemisphere);
    return u_environmentParams.x < 1.5 ? constantColor : hemisphereColor;
}

void main()
{
    vec4 albedo = texture2D(s_gbufferAlbedo, v_texcoord0);
    vec3 normal = normalize(texture2D(s_gbufferNormal, v_texcoord0).xyz * 2.0 - 1.0);
    vec4 material = texture2D(s_gbufferMaterial, v_texcoord0);
    float metallic = clamp(material.x, 0.0, 1.0);
    float roughness = clamp(material.y, 0.04, 1.0);
    float occlusion = clamp(material.z, 0.0, 1.0);

    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), albedo.rgb, metallic);
    vec3 fresnel = FresnelSchlick(nDotV, f0);
    vec3 lighting = EnvironmentColor(normal) * albedo.rgb * (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * occlusion * u_environmentParams.y;

    for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
        if (float(lightIndex) < u_lightParams.x && u_lightDirKind[lightIndex].w < 0.5) {
            vec3 lightDir = normalize(-u_lightDirKind[lightIndex].xyz);
            float nDotL = max(dot(normal, lightDir), 0.0);
            vec3 radiance = u_lightColorIntensity[lightIndex].rgb * u_lightColorIntensity[lightIndex].a;
            lighting += albedo.rgb * radiance * nDotL * (1.0 - metallic);
        }
    }

    gl_FragColor = vec4(lighting, albedo.a);
}
