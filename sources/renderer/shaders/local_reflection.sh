#ifndef KB_LOCAL_REFLECTION_SH
#define KB_LOCAL_REFLECTION_SH

// Local reflection probes. Every probe's radiance lives in one HDR atlas (SceneLocalReflectionAtlas): per
// probe an octahedral map for each of a few roughness levels. The probe tables hold up to
// KB_LOCAL_REFLECTION_MAX probes (u_localReflectionControl.x of them in use); a surface blends the two whose
// influence weighs most.
#define KB_LOCAL_REFLECTION_MAX 16
#define KB_LOCAL_REFLECTION_LEVELS 5

#if defined(KB_LOCAL_REFLECTION_DEFERRED)
SAMPLER2D(s_localReflection,14);
#else
SAMPLER2D(s_localReflection,11);
#endif
// xyz centre, w intensity (0 while the probe is not ready in the atlas).
uniform vec4 u_localReflectionPositions[KB_LOCAL_REFLECTION_MAX];
// xyz half extents, w shape (0 infinite, 1 box, 2 sphere).
uniform vec4 u_localReflectionExtents[KB_LOCAL_REFLECTION_MAX];
// x atlas slot, y sphere influence radius, z 1 with parallax correction.
uniform vec4 u_localReflectionParameters[KB_LOCAL_REFLECTION_MAX];
// Per roughness level: x, y of its tile inside a slot, z its size in texels, w the border in texels.
uniform vec4 u_localReflectionLevels[KB_LOCAL_REFLECTION_LEVELS];
// x slot width, y slot height (texels), zw 1 / atlas size.
uniform vec4 u_localReflectionAtlas;
// x probes in use, y 1 to blend two probes, z slots per atlas row, w last roughness level.
uniform vec4 u_localReflectionControl;

float KbLocalReflectionWeight(vec3 P, vec4 center, vec4 extent, vec4 parameters)
{
    if (center.w <= 0.0) return 0.0;
    vec3 delta = P - center.xyz;
    if (extent.w < 0.5) return 1.0;
    if (extent.w > 1.5) return clamp(1.0 - length(delta) / max(parameters.y,0.0001),0.0,1.0);
    vec3 distances = abs(delta) / max(extent.xyz,vec3_splat(0.0001));
    return clamp(1.0 - max(max(distances.x,distances.y),distances.z),0.0,1.0);
}

vec3 KbLocalReflectionRay(vec3 P, vec3 ray, vec4 center, vec4 extent, vec4 parameters)
{
    if (parameters.z < 0.5 || extent.w < 0.5) return ray;
    vec3 delta = P - center.xyz;
    float distance;
    if (extent.w > 1.5) {
        float along = dot(delta,ray);
        distance = -along + sqrt(max(along * along - dot(delta,delta) + parameters.y * parameters.y,0.0));
    } else {
        vec3 safeRay = mix(vec3_splat(-1.0),vec3_splat(1.0),step(vec3_splat(0.0),ray)) * max(abs(ray),vec3_splat(0.00001));
        vec3 farHit = max((extent.xyz - delta) / safeRay,(-extent.xyz - delta) / safeRay);
        distance = min(min(farHit.x,farHit.y),farHit.z);
    }
    return normalize(delta + ray * max(distance,0.0));
}

// The octahedral map of a direction, 0..1 across the map (the build in fs_local_reflection_atlas.sc inverts it).
vec2 KbOctahedralEncode(vec3 direction)
{
    vec3 d = direction / max(abs(direction.x) + abs(direction.y) + abs(direction.z),0.00001);
    vec2 e = d.xy;
    if (d.z < 0.0) {
        vec2 side = vec2(e.x >= 0.0 ? 1.0 : -1.0,e.y >= 0.0 ? 1.0 : -1.0);
        e = (vec2(1.0,1.0) - abs(e.yx)) * side;
    }
    return e * 0.5 + vec2(0.5,0.5);
}

vec3 KbLocalReflectionLevel(float slot, float row, float level, vec2 uv)
{
    vec4 rect = u_localReflectionLevels[int(level)];
    vec2 origin = vec2((slot - row * u_localReflectionControl.z) * u_localReflectionAtlas.x,row * u_localReflectionAtlas.y);
    vec2 texel = origin + rect.xy + vec2_splat(rect.w) + uv * rect.z;
    return texture2DLod(s_localReflection,texel * u_localReflectionAtlas.zw,0.0).rgb;
}

// The probe's radiance in a direction, between its two nearest roughness levels.
vec3 KbLocalReflectionProbe(int index, vec3 P, vec3 ray, float roughness)
{
    vec4 center = u_localReflectionPositions[index];
    vec4 extent = u_localReflectionExtents[index];
    vec4 parameters = u_localReflectionParameters[index];
    vec2 uv = KbOctahedralEncode(KbLocalReflectionRay(P,ray,center,extent,parameters));
    float level = clamp(roughness,0.0,1.0) * u_localReflectionControl.w;
    float lower = floor(level);
    float upper = min(lower + 1.0,u_localReflectionControl.w);
    float row = floor((parameters.x + 0.5) / u_localReflectionControl.z);
    vec3 color = mix(KbLocalReflectionLevel(parameters.x,row,lower,uv),KbLocalReflectionLevel(parameters.x,row,upper,uv),level - lower);
    return color * center.w;
}

vec3 KbLocalReflection(vec3 P, vec3 ray, float roughness, vec3 environment)
{
    float weight0 = 0.0;
    float weight1 = 0.0;
    int index0 = 0;
    int index1 = 0;
    for (int i = 0; i < KB_LOCAL_REFLECTION_MAX; ++i) {
        if (float(i) > u_localReflectionControl.x - 0.5) break;
        float weight = KbLocalReflectionWeight(P,u_localReflectionPositions[i],u_localReflectionExtents[i],u_localReflectionParameters[i]);
        if (weight > weight0) {
            weight1 = weight0;
            index1 = index0;
            weight0 = weight;
            index0 = i;
        } else if (weight > weight1) {
            weight1 = weight;
            index1 = i;
        }
    }
    if (u_localReflectionControl.y < 0.5) weight1 = 0.0;
    float sum = weight0 + weight1;
    if (sum <= 0.00001) return environment;
    vec3 color = KbLocalReflectionProbe(index0,P,ray,roughness) * weight0;
    if (weight1 > 0.0) color += KbLocalReflectionProbe(index1,P,ray,roughness) * weight1;
    return mix(environment,color / sum,clamp(sum,0.0,1.0));
}

#endif
