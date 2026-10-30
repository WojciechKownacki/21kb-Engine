#include "bgfx_compute.sh"
BUFFER_RO(effectInput, uint, 0);
BUFFER_RW(effectOutput, uint, 1);
IMAGE2D_WO(effectImage, rgba8, 2);
uniform vec4 u_fx_gradient_size;
NUM_THREADS(8,8,1)
void main()
{
    uvec2 p=gl_GlobalInvocationID.xy;
    if(p.x>=uint(u_fx_gradient_size.x)||p.y>=uint(u_fx_gradient_size.y)) return;
    if(p.y==0u&&p.x<4u) effectOutput[p.x]=effectInput[p.x]+42u;
    imageStore(effectImage,ivec2(p),vec4(float(p.x)/max(u_fx_gradient_size.x-1.0,1.0),0.5,0.25,1.0));
}
