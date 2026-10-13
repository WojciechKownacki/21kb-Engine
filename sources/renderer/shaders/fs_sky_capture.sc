$input v_texcoord0

// Sky light capture (Unreal's SkyLight, real-time capture): one face of a cube map of the world
// backdrop, which SceneSkyReflection then prefilters for the reflections of every roughness.

#include <bgfx_shader.sh>
#include "sky_cube.sh"

SAMPLER2D(s_deferredBackdropEnvironment, 0);
uniform vec4 u_skyCubeFace;

#include "sky_backdrop.sh"

void main()
{
    vec3 direction = KbCubeFaceDirection(u_skyCubeFace.x, KbCubeFaceUv(v_texcoord0, u_skyCubeFace.y));
    gl_FragColor = vec4(KbBackdropRadiance(direction), 1.0);
}
