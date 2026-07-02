$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal

#include <bgfx_shader.sh>

void main()
{
    gl_FragColor = vec4(1.0, 1.0, 1.0, max(abs(v_color0.a), 1.0));
}
