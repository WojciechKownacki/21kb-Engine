$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_color0, v_texcoord0, v_shadowPos, v_shadowFlags

#include <bgfx_shader.sh>

uniform vec4 u_particleCameraBasis[3];
uniform vec4 u_particleEmitterParams;
uniform vec4 u_particleLocalBasis;

vec3 RotateByQuaternion(vec3 value, vec4 quaternion)
{
    return value + 2.0 * cross(quaternion.xyz, cross(quaternion.xyz, value) + quaternion.w * value);
}

void main()
{
    vec3 cameraRight = normalize(u_particleCameraBasis[0].xyz);
    vec3 cameraUp = normalize(u_particleCameraBasis[1].xyz);
    vec3 cameraForward = normalize(u_particleCameraBasis[2].xyz);
    vec3 right = cameraRight;
    vec3 up = cameraUp;
    float alignment = u_particleEmitterParams.z;
    if (alignment > 0.5 && alignment < 1.5) {
        vec3 velocity = i_data2.xyz;
        if (dot(velocity, velocity) > 0.0000000001) {
            up = normalize(velocity);
            right = cross(cameraForward, up);
            if (dot(right, right) <= 0.0000000001) right = cameraRight;
            right = normalize(right);
            up = normalize(cross(right, cameraForward));
        }
    } else if (alignment > 1.5 && alignment < 2.5) {
        up = vec3(0.0, 1.0, 0.0);
        right = cross(cameraForward, up);
        if (dot(right, right) <= 0.0000000001) right = cameraRight;
        right = normalize(right);
        up = normalize(cross(right, cameraForward));
    } else if (alignment > 2.5) {
        float quaternionLength = dot(u_particleLocalBasis, u_particleLocalBasis);
        if (quaternionLength > 0.999 && quaternionLength < 1.001) {
            vec4 localBasis = normalize(u_particleLocalBasis);
            right = normalize(RotateByQuaternion(vec3(1.0, 0.0, 0.0), localBasis));
            up = normalize(RotateByQuaternion(vec3(0.0, 1.0, 0.0), localBasis));
        }
    }

    float outputScale = u_particleEmitterParams.w;
    float stretch = max(1.0, i_data2.w);
    float rotationCos = cos(i_data1.w);
    float rotationSin = sin(i_data1.w);
    vec2 corner = vec2(
        a_position.x * rotationCos - a_position.y * rotationSin,
        a_position.x * rotationSin + a_position.y * rotationCos);
    vec3 world = i_data0.xyz + right * corner.x * i_data0.w * outputScale +
        up * corner.y * i_data0.w * outputScale * stretch;
    vec4 clip = mul(u_viewProj, vec4(world, 1.0));
    gl_Position = clip;
    float columns = max(u_particleEmitterParams.x, 1.0);
    float rows = max(u_particleEmitterParams.y, 1.0);
    float frame = i_data4.x;
    vec2 cell = vec2(mod(frame, columns), floor(frame / columns));
    v_texcoord0 = (a_texcoord0 + cell) / vec2(columns, rows);
    v_color0 = i_data3;
    v_shadowPos = clip;
    v_shadowFlags = vec4(abs(mul(u_view, vec4(world, 1.0)).z), a_position.xy * 2.0,
        i_data0.w * outputScale);
}
