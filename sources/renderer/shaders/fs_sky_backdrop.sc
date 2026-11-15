$input v_texcoord0

// The world backdrop of the forward path, where no opaque surface was drawn: the same radiance
// (sky_backdrop.sh) and view ray as the background pixels of the deferred lighting.

#include <bgfx_shader.sh>
#include "gbuffer_position.sh"

SAMPLER2D(s_deferredBackdropEnvironment, 0);

#include "sky_backdrop.sh"

void main()
{
    // The cleared depth of a background pixel, as the deferred lighting reads it there.
    KB_RECONSTRUCTION_PRECISE vec3 direction =
        normalize(ReconstructWorldPosition(v_texcoord0, 0.0) - u_deferredCameraPosition.xyz);
    gl_FragColor = vec4(KbBackdropRadiance(direction), 1.0);
}
