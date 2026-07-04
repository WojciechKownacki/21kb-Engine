$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
SAMPLER2D(s_history, 1);
SAMPLER2D(s_velocity, 2);
uniform vec4 u_temporalParams;
uniform vec4 u_postParams;

void main()
{
    vec2 currentUv = v_texcoord0 + u_postParams.zw;
    if (currentUv.x < 0.0 || currentUv.x > 1.0 || currentUv.y < 0.0 || currentUv.y > 1.0) {
        currentUv = v_texcoord0;
    }

    vec4 current = texture2D(s_source, currentUv);
    if (u_temporalParams.y < 0.5) {
        gl_FragColor = current;
        return;
    }

    vec2 velocity = texture2D(s_velocity, currentUv).xy;
    vec2 historyUv = currentUv - velocity + u_postParams.xy;
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

    vec4 history = texture2D(s_history, historyUv);
    history.rgb = clamp(history.rgb, minColor, maxColor);
    float blend = saturate(u_temporalParams.x);
    float pixelVelocity = length(velocity / max(texel, vec2_splat(0.000001)));
    blend *= 1.0 - saturate(pixelVelocity * 0.02);
    float currentLum = dot(current.rgb, vec3(0.2126, 0.7152, 0.0722));
    float historyLum = dot(history.rgb, vec3(0.2126, 0.7152, 0.0722));
    float lumDelta = abs(currentLum - historyLum) / max(max(currentLum, historyLum), 0.0001);
    blend *= 1.0 - (saturate(lumDelta) * 0.08);
    gl_FragColor = vec4(mix(current.rgb, history.rgb, blend), current.a);
}
