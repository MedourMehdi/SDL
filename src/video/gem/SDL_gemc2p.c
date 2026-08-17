/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* ============================================================================
   SDL_gemc2p.c – Chunky-to-Planar and pixel-format converters
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Strategy: LUT-first everywhere. The shift-based C2P variants
             (Atari_C2P_8to4, Atari_C2P_8to8) are removed; in pure-C mode
             every planar path goes through c2p_lut_core(). When
             SDL_GEM_C2P_ASM is defined the hand-written LUT+C2P assembly
             routines are used instead.

   C90 compliant.
   ============================================================================ */

#include "SDL_gemvideo.h"

/* ============================================================================
   Section 1 – RGB332 → TrueColor LUT tables
   Memory: 512 + 1024 + 1024 = 2560 bytes total
   ============================================================================ */

Uint16 rgb332_to_rgb565_lut[256];
Uint32 rgb332_to_rgb888_lut[256];    /* 0x00RRGGBB */
Uint32 rgb332_to_argb8888_lut[256];  /* 0xFFRRGGBB */

static int rgb332_tc_lut_initialized = 0;

static void Atari_InitRGB332toTrueColorLUTs(void)
{
    int i;
    Uint8  r3, g3, b2;
    Uint8  r8, g8, b8;
    Uint16 r5, g6, b5;

    if (rgb332_tc_lut_initialized) return;

    for (i = 0; i < 256; i++) {
        r3 = (Uint8)((i >> 5) & 0x07);
        g3 = (Uint8)((i >> 2) & 0x07);
        b2 = (Uint8)(i & 0x03);

        /* Expand to 8-bit via replication */
        r8 = (Uint8)((r3 << 5) | (r3 << 2) | (r3 >> 1));
        g8 = (Uint8)((g3 << 5) | (g3 << 2) | (g3 >> 1));
        b8 = (Uint8)((b2 << 6) | (b2 << 4) | (b2 << 2) | b2);

        r5 = (Uint16)((r3 * 31) / 7);
        g6 = (Uint16)((g3 * 63) / 7);
        b5 = (Uint16)((b2 * 31) / 3);

        rgb332_to_rgb565_lut[i]   = (Uint16)((r5 << 11) | (g6 << 5) | b5);
        rgb332_to_rgb888_lut[i]   = ((Uint32)r8 << 16) | ((Uint32)g8 << 8) | b8;
        rgb332_to_argb8888_lut[i] = 0xFF000000UL
                                  | ((Uint32)r8 << 16)
                                  | ((Uint32)g8 << 8)
                                  | b8;
    }

    rgb332_tc_lut_initialized = 1;
}

/* ============================================================================
   Section 2 – RGB332 → TrueColor converters
   ASM versions (SDL_GEM_C2P_ASM) are thin wrappers that ensure the LUT is
   initialised before handing off to the hand-written inner loop.
   ============================================================================ */

#ifdef SDL_GEM_C2P_ASM

void Atari_ConvertRGB332toRGB565(const Uint8 *src, Uint16 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    if (!rgb332_tc_lut_initialized) Atari_InitRGB332toTrueColorLUTs();
    Atari_ConvertRGB332toRGB565_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertRGB332toARGB8888(const Uint8 *src, Uint32 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    if (!rgb332_tc_lut_initialized) Atari_InitRGB332toTrueColorLUTs();
    Atari_ConvertRGB332toARGB8888_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertRGB332toBGRA8888(const Uint8 *src, Uint32 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    if (!rgb332_tc_lut_initialized) Atari_InitRGB332toTrueColorLUTs();
    Atari_ConvertRGB332toBGRA8888_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertBGRA8888toRGB332(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertBGRA8888toRGB332_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertARGB8888toRGB332(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertARGB8888toRGB332_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertRGB888toRGB332(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertRGB888toRGB332_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertBGR888toRGB332(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertBGR888toRGB332_asm(src, dst, width, height, src_pitch, dst_pitch);
}

/* ---- 24-bit (true 3-byte) ASM wrappers ---- */

void Atari_ConvertRGB332toRGB888(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertRGB332toRGB888_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertRGB332toBGR888(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertRGB332toBGR888_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertBGRA8888toBGR888(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertBGRA8888toBGR888_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertARGB8888toRGB888(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertARGB8888toRGB888_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertBGRA8888toRGB888(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertBGRA8888toRGB888_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertARGB8888toBGR888(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertARGB8888toBGR888_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertRGBA8888toRGB332(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertRGBA8888toRGB332_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertRGB565toRGB332(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertRGB565toRGB332_asm(src, dst, width, height, src_pitch, dst_pitch);
}

void Atari_ConvertBGR565toRGB332(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    if (!src || !dst || width <= 0 || height <= 0) return;
    Atari_ConvertBGR565toRGB332_asm(src, dst, width, height, src_pitch, dst_pitch);
}

#else /* pure-C implementations */

void Atari_ConvertRGB332toRGB565(const Uint8 *src, Uint16 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint16      *dst_row;

    if (!src || !dst || width <= 0 || height <= 0) return;
    if (!rgb332_tc_lut_initialized) Atari_InitRGB332toTrueColorLUTs();

    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = (Uint16 *)((Uint8 *)dst + (y * dst_pitch));
        for (x = 0; x < width - 3; x += 4) {
            dst_row[x]   = rgb332_to_rgb565_lut[src_row[x]];
            dst_row[x+1] = rgb332_to_rgb565_lut[src_row[x+1]];
            dst_row[x+2] = rgb332_to_rgb565_lut[src_row[x+2]];
            dst_row[x+3] = rgb332_to_rgb565_lut[src_row[x+3]];
        }
        for (; x < width; x++)
            dst_row[x] = rgb332_to_rgb565_lut[src_row[x]];
    }
}

void Atari_ConvertRGB332toARGB8888(const Uint8 *src, Uint32 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint32      *dst_row;

    if (!src || !dst || width <= 0 || height <= 0) return;
    if (!rgb332_tc_lut_initialized) Atari_InitRGB332toTrueColorLUTs();

    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = (Uint32 *)((Uint8 *)dst + (y * dst_pitch));
        for (x = 0; x < width - 3; x += 4) {
            dst_row[x]   = rgb332_to_argb8888_lut[src_row[x]];
            dst_row[x+1] = rgb332_to_argb8888_lut[src_row[x+1]];
            dst_row[x+2] = rgb332_to_argb8888_lut[src_row[x+2]];
            dst_row[x+3] = rgb332_to_argb8888_lut[src_row[x+3]];
        }
        for (; x < width; x++)
            dst_row[x] = rgb332_to_argb8888_lut[src_row[x]];
    }
}

void Atari_ConvertRGB332toBGRA8888(const Uint8 *src, Uint32 *dst,
                                  int width, int height,
                                  int src_pitch, int dst_pitch)
{
    int y;
    int n = width;

    for (y = 0; y < height; y++) {
        const Uint8 *src_ptr = src + (y * src_pitch);
        Uint8 *dst_ptr = (Uint8 *)dst + (y * dst_pitch);
        
        while (n--) {
            Uint8 pixel = *src_ptr++;
            Uint8 r3 = (pixel >> 5) & 0x07;
            Uint8 g3 = (pixel >> 2) & 0x07;
            Uint8 b2 = pixel & 0x03;
            
            *dst_ptr++ = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;  /* B */
            *dst_ptr++ = (g3 << 5) | (g3 << 2) | (g3 >> 1);        /* G */
            *dst_ptr++ = (r3 << 5) | (r3 << 2) | (r3 >> 1);        /* R */
            *dst_ptr++ = 0xFF;                                      /* A */
        }
    }
}

void Atari_ConvertARGB8888toRGB332(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const int src_skip = src_pitch - (width * 4);
    const int dst_skip = dst_pitch - width;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            /* Memory layout [A][R][G][B] */
            Uint8 r = src[1];
            Uint8 g = src[2];
            Uint8 b = src[3];
            /* src[0] = A, ignored */
            *dst++ = (r & 0xE0) | ((g >> 5) << 2) | (b >> 6);
            src += 4;
        }
        src += src_skip;
        dst += dst_skip;
    }
}

void Atari_ConvertBGRA8888toRGB332(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const int src_skip = src_pitch - (width * 4);
    const int dst_skip = dst_pitch - width;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            Uint8 b = src[0];
            Uint8 g = src[1];
            Uint8 r = src[2];
            /* src[3] = A, ignored */
            *dst++ = ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
            src += 4;
        }
        src += src_skip;
        dst += dst_skip;
    }
}

void Atari_ConvertRGB888toRGB332(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const int src_skip = src_pitch - (width * 3);
    const int dst_skip = dst_pitch - width;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            Uint8 r = src[0];
            Uint8 g = src[1];
            Uint8 b = src[2];
            *dst++ = (r & 0xE0) | ((g >> 5) << 2) | (b >> 6);
            src += 3;
        }
        src += src_skip;
        dst += dst_skip;
    }
}

void Atari_ConvertBGR888toRGB332(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const int src_skip = src_pitch - (width * 3);
    const int dst_skip = dst_pitch - width;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            Uint8 b = src[0];
            Uint8 g = src[1];
            Uint8 r = src[2];
            *dst++ = (r & 0xE0) | ((g >> 5) << 2) | (b >> 6);
            src += 3;
        }
        src += src_skip;
        dst += dst_skip;
    }
}

/* ---- 24-bit (true 3-byte) pure-C implementations ---- */

void Atari_ConvertRGB332toRGB888(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint8       *dst_row;
    Uint32       packed;

    if (!src || !dst || width <= 0 || height <= 0) return;
    if (!rgb332_tc_lut_initialized) Atari_InitRGB332toTrueColorLUTs();

    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = dst + (y * dst_pitch);
        for (x = 0; x < width; x++) {
            packed = rgb332_to_rgb888_lut[src_row[x]];
            dst_row[x*3 + 0] = (Uint8)((packed >> 16) & 0xFF); /* R */
            dst_row[x*3 + 1] = (Uint8)((packed >>  8) & 0xFF); /* G */
            dst_row[x*3 + 2] = (Uint8)( packed        & 0xFF); /* B */
        }
    }
}

void Atari_ConvertRGB332toBGR888(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint8       *dst_row;
    Uint32       packed;

    if (!src || !dst || width <= 0 || height <= 0) return;
    if (!rgb332_tc_lut_initialized) Atari_InitRGB332toTrueColorLUTs();

    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = dst + (y * dst_pitch);
        for (x = 0; x < width; x++) {
            packed = rgb332_to_rgb888_lut[src_row[x]];
            dst_row[x*3 + 0] = (Uint8)( packed        & 0xFF); /* B */
            dst_row[x*3 + 1] = (Uint8)((packed >>  8) & 0xFF); /* G */
            dst_row[x*3 + 2] = (Uint8)((packed >> 16) & 0xFF); /* R */
        }
    }
}

void Atari_ConvertBGRA8888toBGR888(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint8       *dst_row;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = dst + (y * dst_pitch);
        for (x = 0; x < width; x++) {
            dst_row[x*3 + 0] = src_row[x*4 + 0]; /* B */
            dst_row[x*3 + 1] = src_row[x*4 + 1]; /* G */
            dst_row[x*3 + 2] = src_row[x*4 + 2]; /* R */
        }
    }
}

void Atari_ConvertARGB8888toRGB888(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint8       *dst_row;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = dst + (y * dst_pitch);
        for (x = 0; x < width; x++) {
            dst_row[x*3 + 0] = src_row[x*4 + 1]; /* R */
            dst_row[x*3 + 1] = src_row[x*4 + 2]; /* G */
            dst_row[x*3 + 2] = src_row[x*4 + 3]; /* B */
        }
    }
}

void Atari_ConvertBGRA8888toRGB888(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint8       *dst_row;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = dst + (y * dst_pitch);
        for (x = 0; x < width; x++) {
            dst_row[x*3 + 0] = src_row[x*4 + 2]; /* R */
            dst_row[x*3 + 1] = src_row[x*4 + 1]; /* G */
            dst_row[x*3 + 2] = src_row[x*4 + 0]; /* B */
        }
    }
}

void Atari_ConvertARGB8888toBGR888(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint8       *dst_row;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = dst + (y * dst_pitch);
        for (x = 0; x < width; x++) {
            dst_row[x*3 + 0] = src_row[x*4 + 3]; /* B */
            dst_row[x*3 + 1] = src_row[x*4 + 2]; /* G */
            dst_row[x*3 + 2] = src_row[x*4 + 1]; /* R */
        }
    }
}

void Atari_ConvertRGBA8888toRGB332(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const int src_skip = src_pitch - (width * 4);
    const int dst_skip = dst_pitch - width;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            Uint8 r = src[0];
            Uint8 g = src[1];
            Uint8 b = src[2];
            /* src[3] = A, ignored */
            *dst++ = (r & 0xE0) | ((g >> 5) << 2) | (b >> 6);
            src += 4;
        }
        src += src_skip;
        dst += dst_skip;
    }
}

void Atari_ConvertRGB565toRGB332(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint16 *src_row;
    Uint8 *dst_row;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        src_row = (const Uint16 *)(src + (y * src_pitch));
        dst_row = dst + (y * dst_pitch);
        for (x = 0; x < width; x++) {
            Uint16 pixel = src_row[x];
            Uint8 r = (pixel >> 11) & 0x1F;
            Uint8 g = (pixel >> 5) & 0x3F;
            Uint8 b = pixel & 0x1F;
            dst_row[x] = ((r >> 2) << 5) | ((g >> 3) << 2) | (b >> 3);
        }
    }
}

void Atari_ConvertBGR565toRGB332(const Uint8 *src, Uint8 *dst,
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint16 *src_row;
    Uint8 *dst_row;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        src_row = (const Uint16 *)(src + (y * src_pitch));
        dst_row = dst + (y * dst_pitch);
        for (x = 0; x < width; x++) {
            Uint16 pixel = src_row[x];
            Uint8 b = (pixel >> 11) & 0x1F;
            Uint8 g = (pixel >> 5) & 0x3F;
            Uint8 r = pixel & 0x1F;
            dst_row[x] = ((r >> 2) << 5) | ((g >> 3) << 2) | (b >> 3);
        }
    }
}

#endif /* SDL_GEM_C2P_ASM */

/* ============================================================================
   Section 3 – C2P lookup tables (pure-C path only)

   c2p_plane_lut[pixel] stores the bitmask contribution of that pixel for
   each of the 8 bitplanes, pre-indexed by bit position within a 16-pixel
   block.  Built once, used by every LUT-based C2P function below.

   NOTE: the per-block bit-position mask (c2p_bit_mask) is still referenced
   by the ASM routines through the global symbol; keep the declaration but
   there is no longer a separate static copy in C — we use a local constant
   array instead to avoid the duplicate symbol when both C and ASM are linked.
   ============================================================================ */

#ifndef SDL_GEM_C2P_ASM

/* Local bit-position table (not exported – ASM has its own copy) */
static const Uint16 c2p_bitmask[16] = {
    0x8000, 0x4000, 0x2000, 0x1000,
    0x0800, 0x0400, 0x0200, 0x0100,
    0x0080, 0x0040, 0x0020, 0x0010,
    0x0008, 0x0004, 0x0002, 0x0001
};

/* ============================================================================
   LUT C2P core – shared by all plane-count variants.

   For each 16-pixel block:
     1. Apply optional palette remap via `lut` (pass NULL to skip).
     2. Scatter bits into `planes` words using bitmask table.
     3. Write `num_planes` words to dst.

   This single function replaces the four separate Atari_C2P_8toN_LUT
   functions from the original code.
   ============================================================================ */
static void c2p_lut_core(const Uint8 *src, Uint8 *dst,
                          int width, int height,
                          int src_pitch, int dst_pitch,
                          int num_planes, const Uint8 *lut)
{
    Uint16 planes[8];
    int y, x, px, p;
    int src_skip        = src_pitch - width;
    int dst_plane_bytes = (width >> 4) * (num_planes * 2);
    int dst_skip        = dst_pitch - dst_plane_bytes;
    const Uint8  *sp    = src;
    Uint16       *dp    = (Uint16 *)dst;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x += 16) {
            /* Zero plane accumulators */
            for (p = 0; p < num_planes; p++) planes[p] = 0;

            for (px = 0; px < 16; px++) {
                Uint8  raw   = *sp++;
                Uint8  pixel = lut ? lut[raw] : raw;
                Uint16 mask  = c2p_bitmask[px];

                /* Unrolled per plane – compiler will trim unused arms */
                if (num_planes >= 1 && (pixel & 0x01)) planes[0] |= mask;
                if (num_planes >= 2 && (pixel & 0x02)) planes[1] |= mask;
                if (num_planes >= 4 && (pixel & 0x04)) planes[2] |= mask;
                if (num_planes >= 4 && (pixel & 0x08)) planes[3] |= mask;
                if (num_planes >= 8 && (pixel & 0x10)) planes[4] |= mask;
                if (num_planes >= 8 && (pixel & 0x20)) planes[5] |= mask;
                if (num_planes >= 8 && (pixel & 0x40)) planes[6] |= mask;
                if (num_planes >= 8 && (pixel & 0x80)) planes[7] |= mask;
            }

            for (p = 0; p < num_planes; p++) *dp++ = planes[p];
        }
        sp += src_skip;
        dp  = (Uint16 *)((Uint8 *)dp + dst_skip);
    }
}

/* Public single-function C2P with integrated LUT remap */
void Atari_C2P_Planar_LUT(void *src, void *dst, int width, int height,
                           int src_pitch, int dst_pitch,
                           int planes, Uint8 *lut)
{
    switch (planes) {
        case 1: c2p_lut_core(src, dst, width, height, src_pitch, dst_pitch, 1, lut); break;
        case 2: c2p_lut_core(src, dst, width, height, src_pitch, dst_pitch, 2, lut); break;
        case 4: c2p_lut_core(src, dst, width, height, src_pitch, dst_pitch, 4, lut); break;
        case 8: c2p_lut_core(src, dst, width, height, src_pitch, dst_pitch, 8, lut); break;
        default: break;
    }
}

/* Atari_C2P_Planar – always goes through the LUT path in pure-C mode.
   Passing lut=NULL skips remapping (identity). */
void Atari_C2P_Planar(void *src, void *dst, int width, int height,
                      int src_pitch, int dst_pitch, int planes)
{
    Atari_C2P_Planar_LUT(src, dst, width, height,
                         src_pitch, dst_pitch, planes, NULL);
}

/* ============================================================================
   BlitFast – aligned 32-bit row copy for TrueColor modes
   ============================================================================ */
void Atari_BlitFast_c(void *dst, const void *src, int row_bytes,
                      int rows, int pitch)
{
    Uint8       *dp = (Uint8 *)dst;
    const Uint8 *sp = (const Uint8 *)src;
    int row;

    for (row = 0; row < rows; row++) {
        Uint8       *d = dp + row * pitch;
        const Uint8 *s = sp + row * pitch;
        int bytes_left = row_bytes;
        int i;

        if (((uintptr_t)s & 3) == 0 && ((uintptr_t)d & 3) == 0 && bytes_left >= 16) {
            Uint32       *d32 = (Uint32 *)d;
            const Uint32 *s32 = (const Uint32 *)s;
            int longs = bytes_left >> 2;

            for (i = 0; i <= longs - 4; i += 4) {
                d32[i]   = s32[i];
                d32[i+1] = s32[i+1];
                d32[i+2] = s32[i+2];
                d32[i+3] = s32[i+3];
            }
            for (; i < longs; i++) d32[i] = s32[i];

            d = (Uint8 *)(d32 + longs);
            s = (const Uint8 *)(s32 + longs);
            bytes_left &= 3;
        }
        while (bytes_left--) *d++ = *s++;
    }
}

#else /* SDL_GEM_C2P_ASM */

/* When using ASM C2P, Atari_C2P_Planar dispatches to the LUT ASM variants
   unconditionally – no cpu-level branch needed here, the caller (SDL_gemvideo)
   should always supply the palette LUT. */
void Atari_C2P_Planar(void *src, void *dst, int width, int height,
                      int src_pitch, int dst_pitch, int planes)
{
    switch (planes) {
        case 1: Atari_C2P_8to1_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, NULL); break;
        case 2: Atari_C2P_8to2_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, NULL); break;
        case 4: Atari_C2P_8to4_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, NULL); break;
        case 8: Atari_C2P_8to8_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, NULL); break;
        default: break;
    }
}

void Atari_C2P_Planar_LUT(void *src, void *dst, int width, int height,
                           int src_pitch, int dst_pitch,
                           int planes, Uint8 *lut)
{
    switch (planes) {
        case 1: Atari_C2P_8to1_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, lut); break;
        case 2: Atari_C2P_8to2_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, lut); break;
        case 4: Atari_C2P_8to4_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, lut); break;
        case 8: Atari_C2P_8to8_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, lut); break;
        default: break;
    }
}

#endif /* SDL_GEM_C2P_ASM */