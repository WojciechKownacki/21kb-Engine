// The deferred lighting shader reading the shadow maps through comparison samplers (hardware filtering); the frames that
// need the stored depth (PCSS) run fs_deferred_lighting.sc. Its inputs come with the included file.
#define KB_SHADOW_COMPARE 1
#include "fs_deferred_lighting.sc"
