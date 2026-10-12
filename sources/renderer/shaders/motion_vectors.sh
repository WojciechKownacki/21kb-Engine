#ifndef KB_MOTION_VECTORS_SH
#define KB_MOTION_VECTORS_SH

// The motion vector pass (Unreal's velocity pass), shared by the builtin and the material graph programs.
// A mesh rasterizes with this frame's jittered u_viewProj - exactly as the depth prepass drew it - and
// writes the jitter-free screen motion between its previous and current positions.

// Previous frame view-projection, without jitter.
uniform mat4 u_motionPreviousViewProjection;
// This frame's view-projection without jitter (u_viewProj carries the TAA jitter).
uniform mat4 u_motionCurrentViewProjection;
// x: first previous-model row of the draw, y: 1 / rows of the row texture, z: 1 when the draw uploaded
// previous models (some instance moved this frame), 0 when every instance is where it was.
uniform vec4 u_motionPreviousModelInfo;

// How far behind the prepass depth a fragment may lie and still be the surface the prepass kept: the
// rounding two programs computing the same position can differ by (relative), and a 24-bit depth step.
#define KB_MOTION_DEPTH_RELATIVE_TOLERANCE 0.00001
#define KB_MOTION_DEPTH_ABSOLUTE_TOLERANCE 0.0000001

#if BGFX_SHADER_TYPE_VERTEX
// The model the instance `instance` (gl_InstanceID) had in the previous frame: its uploaded row when the draw
// carries previous models, otherwise its current model (c0..c3, the columns of the instance data).
mat4 KbMotionPreviousModel(sampler2D rows, float instance, vec4 c0, vec4 c1, vec4 c2, vec4 c3)
{
    float rowY = (u_motionPreviousModelInfo.x + instance + 0.5) * u_motionPreviousModelInfo.y;
    vec4 r0 = texture2DLod(rows, vec2(0.125, rowY), 0.0);
    vec4 r1 = texture2DLod(rows, vec2(0.375, rowY), 0.0);
    vec4 r2 = texture2DLod(rows, vec2(0.625, rowY), 0.0);
    vec4 r3 = texture2DLod(rows, vec2(0.875, rowY), 0.0);
    bool uploaded = u_motionPreviousModelInfo.z > 0.5;
    return mtxFromCols(
        uploaded ? vec4(r0.xyz, 0.0) : c0,
        uploaded ? vec4(r1.xyz, 0.0) : c1,
        uploaded ? vec4(r2.xyz, 0.0) : c2,
        uploaded ? vec4(r3.xyz, 1.0) : c3);
}
#endif // BGFX_SHADER_TYPE_VERTEX

#if BGFX_SHADER_TYPE_FRAGMENT
// The visibility test of the pass: Unreal renders velocity with a near-or-equal depth test against the
// prepass depth. Reversed Z stores nearer surfaces as larger depths, so a fragment is the visible surface
// when its depth is not smaller than the stored one; an occluded surface behind it is rejected at any
// distance. `fragmentDepth` is the depth the prepass wrote for this fragment (gl_FragCoord.z, or the pixel
// depth offset depth of a material that moves it).
bool KbMotionSurfaceVisible(sampler2D sceneDepth, vec2 fragCoord, float fragmentDepth)
{
    vec2 depthUv = fragCoord / max(u_viewRect.zw, vec2(1.0, 1.0));
    float storedDepth = texture2D(sceneDepth, depthUv).x;
    float tolerance = max(storedDepth * KB_MOTION_DEPTH_RELATIVE_TOLERANCE, KB_MOTION_DEPTH_ABSOLUTE_TOLERANCE);
    return fragmentDepth >= storedDepth - tolerance;
}

// The screen-space (UV) motion from the previous position to the current one.
vec2 KbMotionVelocity(vec4 currentClip, vec4 previousClip)
{
    vec2 currentNdc = currentClip.xy / max(abs(currentClip.w), 0.000001);
    vec2 previousNdc = previousClip.xy / max(abs(previousClip.w), 0.000001);
    vec2 currentUv = vec2(currentNdc.x * 0.5 + 0.5, 0.5 - currentNdc.y * 0.5);
    vec2 previousUv = vec2(previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5);
    return currentUv - previousUv;
}
#endif // BGFX_SHADER_TYPE_FRAGMENT

#endif // KB_MOTION_VECTORS_SH
