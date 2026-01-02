/* ============================================
   FILE: src/video/atari/SDL_atarivideo.h
   Atari video driver header
   ============================================ */
#ifndef SDL_atarivideo_h_
#define SDL_atarivideo_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "../ataricommon/SDL_atarimodel.h"

/* External assembly functions (defined in SDL_ataric2p.S) */
extern void Atari_C2P_8to4(void *src, void *dst, int width, int height,
                           int src_pitch, int dst_pitch);
extern void Atari_C2P_8to8(void *src, void *dst, int width, int height,
                           int src_pitch, int dst_pitch);
extern void Atari_BlitFast(void *dst, const void *src, int row_bytes,
                           int rows, int pitch);

#endif /* SDL_atarivideo_h_ */