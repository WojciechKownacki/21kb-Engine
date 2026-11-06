#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

/* The bundled decoder retains its original license and attribution. */
#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

/* Keep implementation-local decoder macros out of platform SDK headers. */
#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>

/* Keep spatial-DSP distance gain identical to the bundled spatializer. */
float kb_miniaudio_distance_attenuation(ma_attenuation_model model,float distance,float minimum,float maximum,float rolloff)
{
    switch(model) {
        case ma_attenuation_model_inverse: return ma_attenuation_inverse(distance,minimum,maximum,rolloff);
        case ma_attenuation_model_linear: return ma_attenuation_linear(distance,minimum,maximum,rolloff);
        case ma_attenuation_model_exponential: return ma_attenuation_exponential(distance,minimum,maximum,rolloff);
        case ma_attenuation_model_none: default: return 1.0f;
    }
}
