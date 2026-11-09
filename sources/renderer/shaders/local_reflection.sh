#ifndef KB_LOCAL_REFLECTION_SH
#define KB_LOCAL_REFLECTION_SH

#if defined(KB_LOCAL_REFLECTION_DEFERRED)
SAMPLERCUBE(s_localReflection0,14);
SAMPLERCUBE(s_localReflection1,15);
#else
SAMPLERCUBE(s_localReflection0,11);
SAMPLERCUBE(s_localReflection1,13);
#endif
uniform vec4 u_localReflectionPositions[2];
uniform vec4 u_localReflectionExtents[2];
uniform vec4 u_localReflectionParameters[2];
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

vec3 KbLocalReflection(vec3 P, vec3 ray, float roughness, vec3 environment)
{
    float weight0 = u_localReflectionControl.x > 0.5 ? KbLocalReflectionWeight(P,u_localReflectionPositions[0],u_localReflectionExtents[0],u_localReflectionParameters[0]) : 0.0;
    float weight1 = u_localReflectionControl.x > 1.5 ? KbLocalReflectionWeight(P,u_localReflectionPositions[1],u_localReflectionExtents[1],u_localReflectionParameters[1]) : 0.0;
    if (u_localReflectionControl.y < 0.5) {
        if (weight0 >= weight1) weight1 = 0.0; else weight0 = 0.0;
    }
    float sum = weight0 + weight1;
    if (sum <= 0.00001) return environment;
    vec3 color0 = textureCubeLod(s_localReflection0,KbLocalReflectionRay(P,ray,u_localReflectionPositions[0],u_localReflectionExtents[0],u_localReflectionParameters[0]),roughness * u_localReflectionParameters[0].x).rgb * u_localReflectionPositions[0].w;
    vec3 color1 = textureCubeLod(s_localReflection1,KbLocalReflectionRay(P,ray,u_localReflectionPositions[1],u_localReflectionExtents[1],u_localReflectionParameters[1]),roughness * u_localReflectionParameters[1].x).rgb * u_localReflectionPositions[1].w;
    return mix(environment,(color0 * weight0 + color1 * weight1) / sum,clamp(sum,0.0,1.0));
}

#endif
