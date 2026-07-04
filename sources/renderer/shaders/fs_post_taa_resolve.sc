$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
SAMPLER2D(s_history, 1);
SAMPLER2D(s_velocity, 2);
uniform vec4 u_temporalParams;

// Catmull-Rom history filtering (9 bilinear taps, Jimenez "Filmic SMAA/TAA", SIGGRAPH 2016):
// bilinear history resampling low-pass filters the accumulation a little more every frame,
// which reads as full-screen blur as soon as the camera moves. Catmull-Rom keeps the
// accumulation sharp and is exact at texel centers, so static pixels are untouched.
vec3 SampleHistoryCatmullRom(vec2 uv, vec2 texel)
{
    vec2 samplePos = uv / texel;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
    vec2 f = samplePos - texPos1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;
    vec2 texPos0 = (texPos1 - 1.0) * texel;
    vec2 texPos3 = (texPos1 + 2.0) * texel;
    vec2 texPos12 = (texPos1 + offset12) * texel;
    vec3 result =
        texture2D(s_history, vec2(texPos0.x, texPos0.y)).rgb * (w0.x * w0.y) +
        texture2D(s_history, vec2(texPos12.x, texPos0.y)).rgb * (w12.x * w0.y) +
        texture2D(s_history, vec2(texPos3.x, texPos0.y)).rgb * (w3.x * w0.y) +
        texture2D(s_history, vec2(texPos0.x, texPos12.y)).rgb * (w0.x * w12.y) +
        texture2D(s_history, vec2(texPos12.x, texPos12.y)).rgb * (w12.x * w12.y) +
        texture2D(s_history, vec2(texPos3.x, texPos12.y)).rgb * (w3.x * w12.y) +
        texture2D(s_history, vec2(texPos0.x, texPos3.y)).rgb * (w0.x * w3.y) +
        texture2D(s_history, vec2(texPos12.x, texPos3.y)).rgb * (w12.x * w3.y) +
        texture2D(s_history, vec2(texPos3.x, texPos3.y)).rgb * (w3.x * w3.y);
    // Catmull-Rom lobes are partly negative; clamp the ringing out of dark backgrounds.
    return max(result, vec3_splat(0.0));
}

void main()
{
    vec2 currentUv = v_texcoord0;

    vec4 current = texture2D(s_source, currentUv);
    if (u_temporalParams.y < 0.5) {
        gl_FragColor = current;
        return;
    }

    vec2 velocity = texture2D(s_velocity, currentUv).xy;
    vec2 historyUv = currentUv - velocity;
    if (historyUv.x < 0.0 || historyUv.x > 1.0 || historyUv.y < 0.0 || historyUv.y > 1.0) {
        gl_FragColor = current;
        return;
    }

    vec2 texel = u_temporalParams.zw;
    vec3 minColor = current.rgb;
    vec3 maxColor = current.rgb;
    vec3 sumColor = current.rgb;
    vec3 sumColorSq = current.rgb * current.rgb;
    vec3 c00 = texture2D(s_source, currentUv + vec2(-1.0, -1.0) * texel).rgb;
    vec3 c10 = texture2D(s_source, currentUv + vec2( 0.0, -1.0) * texel).rgb;
    vec3 c20 = texture2D(s_source, currentUv + vec2( 1.0, -1.0) * texel).rgb;
    vec3 c01 = texture2D(s_source, currentUv + vec2(-1.0,  0.0) * texel).rgb;
    vec3 c21 = texture2D(s_source, currentUv + vec2( 1.0,  0.0) * texel).rgb;
    vec3 c02 = texture2D(s_source, currentUv + vec2(-1.0,  1.0) * texel).rgb;
    vec3 c12 = texture2D(s_source, currentUv + vec2( 0.0,  1.0) * texel).rgb;
    vec3 c22 = texture2D(s_source, currentUv + vec2( 1.0,  1.0) * texel).rgb;
    sumColor += c00 + c10 + c20 + c01 + c21 + c02 + c12 + c22;
    sumColorSq += (c00 * c00) + (c10 * c10) + (c20 * c20) + (c01 * c01) + (c21 * c21) + (c02 * c02) + (c12 * c12) + (c22 * c22);
    minColor = min(minColor, min(min(c00, c10), min(c20, c01)));
    minColor = min(minColor, min(min(c21, c02), min(c12, c22)));
    maxColor = max(maxColor, max(max(c00, c10), max(c20, c01)));
    maxColor = max(maxColor, max(max(c21, c02), max(c12, c22)));
    vec3 meanColor = sumColor * (1.0 / 9.0);
    vec3 variance = max((sumColorSq * (1.0 / 9.0)) - (meanColor * meanColor), vec3_splat(0.0));
    vec3 sigma = sqrt(variance) * 1.25;
    minColor = max(minColor, meanColor - sigma);
    maxColor = min(maxColor, meanColor + sigma);

    vec3 rawHistory = SampleHistoryCatmullRom(historyUv, texel);
    vec3 historyRgb = clamp(rawHistory, minColor, maxColor);
    float blend = saturate(u_temporalParams.x);
    float pixelVelocity = length(velocity / max(texel, vec2_splat(0.000001)));
    // Motion vectors are jitter-free (fs_post_motion_vectors.sc reprojects with unjittered
    // matrices on both ends), so a static pixel carries exactly zero velocity and never erodes
    // history. The low edge only absorbs depth/interpolation noise; real motion beyond it fades
    // history out to limit ghosting the neighborhood clamp cannot hide.
    float velocityRejection = smoothstep(0.75, 2.5, pixelVelocity);
    blend *= 1.0 - velocityRejection;
    // Clamp-distance rejection: when the reprojected history lands far outside the current
    // neighborhood box, the motion vector was wrong for this pixel — depthless overlays (editor
    // grid) reproject at the far plane with no parallax, and disoccluded pixels have no valid
    // history at all. Velocity rejection cannot catch these (the predicted velocity is small),
    // so distrust history by how far the clamp had to move it. A history inside the box (the
    // converged static case) is untouched.
    float boxExtent = length(maxColor - minColor);
    float clampError = length(rawHistory - historyRgb);
    float clampRejection = smoothstep(0.25, 1.0, clampError / max(boxExtent, 0.0001));
    blend *= 1.0 - clampRejection;
    // No luminance-delta rejection here: at a high-contrast edge the jittered current sample
    // legitimately alternates between the two sides every frame, so any luma-delta test fires
    // permanently on exactly the pixels TAA must converge, and the edge never stops shimmering.
    // Ghosting is already bounded by the variance clamp above and the Karis weighting below.
    float currentLum = dot(current.rgb, vec3(0.2126, 0.7152, 0.0722));
    float historyLum = dot(historyRgb, vec3(0.2126, 0.7152, 0.0722));

    // Karis inverse-luminance weighting (UE4 "High Quality Temporal Anti-Aliasing", 2014):
    // weighting samples by 1/(1+luma) before averaging suppresses flicker from high-variance
    // neighborhoods without the aggressive blur a naive lerp needs to hide the same flicker.
    float currentWeight = (1.0 - blend) / (1.0 + currentLum);
    float historyWeight = blend / (1.0 + historyLum);
    vec3 resolved = (current.rgb * currentWeight + historyRgb * historyWeight) / max(currentWeight + historyWeight, 0.00001);
    gl_FragColor = vec4(resolved, current.a);
}
