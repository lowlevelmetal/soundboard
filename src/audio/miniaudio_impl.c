/*
 * Compiles the miniaudio implementation. stb_vorbis is included the way
 * miniaudio documents it so that Ogg Vorbis files can be decoded as well as
 * WAV, FLAC and MP3 (which miniaudio supports natively).
 */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
