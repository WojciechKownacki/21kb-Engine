#ifndef KB_PBR_BUILTIN_ADVANCED_SH
#define KB_PBR_BUILTIN_ADVANCED_SH

uniform vec4 u_materialSheen; // linear RGB, Charlie roughness
uniform vec4 u_materialOptics; // transmission, anisotropy, rotation (radians), thickness
uniform vec4 u_materialAttenuation; // linear RGB, attenuation distance (0 = infinite)
SAMPLER2D(s_builtinSceneColor, 10);
uniform vec4 u_builtinSceneColorParams; // x: opaque scene snapshot available

float KbAnisotropicDistribution(vec3 N, vec3 H, float roughness, vec3 tangent)
{
    vec3 T = tangent - N * dot(tangent, N);
    if (dot(T,T) < 0.00001) T = cross(abs(N.y) < 0.99 ? vec3(0.0,1.0,0.0) : vec3(1.0,0.0,0.0), N);
    T = normalize(T);
    vec3 B = normalize(cross(N,T));
    float angle = u_materialOptics.z;
    vec3 rotatedT = T * cos(angle) + B * sin(angle);
    vec3 rotatedB = -T * sin(angle) + B * cos(angle);
    float alpha = max(roughness * roughness, 0.002);
    float aspect = sqrt(1.0 - 0.9 * u_materialOptics.y);
    float ax = alpha / aspect;
    float ay = alpha * aspect;
    vec3 h = vec3(dot(H,rotatedT) / ax, dot(H,rotatedB) / ay, max(dot(N,H),0.0));
    float denominator = dot(h,h);
    return 1.0 / max(3.14159265 * ax * ay * denominator * denominator, 0.000001);
}

vec3 KbSheenLight(vec3 N, vec3 V, vec3 L)
{
    float NoL = max(dot(N,L),0.0);
    float NoV = max(dot(N,V),0.0001);
    vec3 H = (V + L) / max(length(V + L),0.00001);
    float NoH = clamp(dot(N,H),0.0,1.0);
    float invRoughness = 1.0 / max(u_materialSheen.w,0.07);
    float distribution = (2.0 + invRoughness) * pow(max(1.0 - NoH * NoH,0.00001),0.5 * invRoughness) * 0.15915494;
    float visibility = 1.0 / max(4.0 * (NoL + NoV - NoL * NoV),0.0001);
    return u_materialSheen.rgb * (distribution * visibility * NoL);
}

vec3 KbTransmissionColor(vec3 N, vec3 V, vec3 albedo, vec3 environment, vec3 worldPos, vec2 fragmentPosition)
{
    vec3 behind = environment;
    if (u_builtinSceneColorParams.x > 0.5) {
        vec2 uv = (fragmentPosition - u_viewRect.xy) / max(u_viewRect.zw,vec2_splat(1.0));
        vec3 viewNormal = mul(u_view,vec4(N,0.0)).xyz;
        vec4 clip = mul(u_viewProj,vec4(worldPos,1.0));
        float ySign = dFdy(clip.y / clip.w) < 0.0 ? -1.0 : 1.0;
        vec2 offset = -viewNormal.xy * vec2(u_proj[0][0],u_proj[1][1]) * (0.025 + 0.025 * u_materialOptics.w);
        offset.y *= ySign;
        uv = clamp(uv + offset,vec2_splat(0.0),vec2_splat(1.0));
        behind = texture2DLod(s_builtinSceneColor,uv,0.0).rgb;
    }
    float pathLength = u_materialOptics.w / max(abs(dot(N,V)),0.1);
    vec3 attenuation = u_materialAttenuation.w > 0.0
        ? pow(abs(u_materialAttenuation.rgb),vec3_splat(pathLength / u_materialAttenuation.w)) : vec3_splat(1.0);
    return behind * albedo * attenuation;
}

#endif
