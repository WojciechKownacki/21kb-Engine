#ifndef KB_SKINNING_SH
#define KB_SKINNING_SH

// Skinned vertices carry their palette indices as normalized 16-bit integers: an integer vertex
// attribute read into a float shader input is reinterpreted bits on Direct3D and Vulkan and an
// integer attribute on OpenGL, so the layout declares them normalized (defined on every API)
// and this turns them back into indices.
vec4 KbSkinJointIndices(vec4 encoded)
{
    return floor(encoded * 65535.0 + 0.5);
}

#endif // KB_SKINNING_SH
