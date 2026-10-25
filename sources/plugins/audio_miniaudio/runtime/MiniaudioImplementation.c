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
