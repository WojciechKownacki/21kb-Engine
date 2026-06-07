$input v_normal, v_color0, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    vec3 normal = normalize(v_normal);
    vec3 lightDir = normalize(vec3(-0.25, 0.55, -0.79));
    float diffuse = dot(normal, lightDir) * 0.5 + 0.5;
    float rim = pow(1.0 - abs(normal.z), 2.0) * 0.05;
    float shade = min(0.72 + diffuse * 0.22 + rim, 1.0);
    float viewFacing = abs(normal.z);
    float edgeWidth = max(fwidth(viewFacing) * 1.65, 0.018);
    float silhouette = smoothstep(0.0, edgeWidth, viewFacing);
    float tintAlpha = v_texcoord0.x;
    float basePass = step(0.95, tintAlpha);
    float alpha = tintAlpha * mix(1.0, silhouette, basePass);
    gl_FragColor = vec4(v_color0.rgb * shade, alpha);
}
