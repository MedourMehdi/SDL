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

/* ============================================
   SDL_gemvideo.h – GEM video driver public header
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   All driver data structures, window data, and function
   declarations for the GEM SDL2 video backend.
   Key values are precomputed once at VideoInit to minimise
   per-frame overhead on the 68000.

   C90 compliant.
   ============================================ */

#ifndef SDL_gemvideo_h_
#define SDL_gemvideo_h_

#include "../SDL_sysvideo.h"
#include "../../events/SDL_events_c.h"
#include "SDL_gemmodel.h"
#include <mt_gem.h>

#ifndef SDL_GEM_DIRTY_RECT
   #ifdef SDL_GEM_DIRTY_RECT_ASM
   #undef SDL_GEM_DIRTY_RECT_ASM
   #endif
#endif

#define MAX_MERGED_RECTS 16
#define MERGE_THRESHOLD  16

/* ============================================================
   Video driver data – PRECOMPUTED at VideoInit
   ============================================================ */
typedef struct SDL_VideoData {
    short vdi_handle;
    short desk_x, desk_y, desk_w, desk_h;
    short work_x, work_y, work_w, work_h;
    short planes;

    /* ONE-TIME: VDI format (vq_scrninfo called once in VideoInit) */
    int16_t vdi_pixel_format;      /* 0=interleaved, 1=whole, 2=packed */
    int16_t vdi_bits_per_pixel;    /* Actual bpp from VDI */

    /* ONE-TIME: Palette LUT (built once in VideoInit) */
    Uint16 hw_palette[256];        /* Hardware RGB values (0x0RGB) */
    Uint8  rgb332_to_hw[256];      /* SDL RGB332 -> hardware index LUT */
    int    use_identity_palette;   /* 1=fast path, 0=LUT remapping */
    int    palette_initialized;    /* 1=LUT is valid */

    /* Saved palette – restored in VideoQuit so the GEM desktop
     * returns to its original colours when the app exits.
     * Indexed by hardware slot (same layout as hw_palette[]). */
    Uint16 saved_palette[256];
    int    saved_palette_count;    /* number of valid entries (= 1<<planes) */

} SDL_VideoData;

/* ============================================================
   Window state flags – stored in SDL_WindowData.state_flags
   ============================================================ */
#define GEM_STATE_MAXIMIZED  0x01
#define GEM_STATE_ICONIFIED  0x02
#define GEM_STATE_FULLSCREEN 0x04
#define GEM_STATE_WAS_MAXIMIZED 0x08

/* ============================================================
   Window data
   ============================================================ */
typedef struct SDL_WindowData {
    /* Framebuffer */
    void *buffer;               /* SDL chunky buffer (aligned) */
    void *raw_buffer;           /* Original malloc ptr for free */
    void *final_buffer;         /* VDI planar/packed buffer (aligned) */
    void *raw_final_buffer;     /* Original malloc ptr for free */
    MFDB  final_mfdb;
    unsigned short buffer_pitch;
    size_t final_buffer_size;

    /* Remap buffer (only allocated if !use_identity_palette) */
    void  *remap_buffer;
    void  *raw_remap_buffer;
    size_t remap_buffer_size;

    short handle;
    short win_type;
    short win_x, win_y, win_w, win_h;
    short work_x, work_y, work_w, work_h;
    Uint8     state_flags;    /* GEM_STATE_* bitmask                       */
    GRECT     restore_rect;   /* border rect saved before maximize/iconify */

    /* PRECOMPUTED C2P constants (calculated once in CreateWindowFramebuffer) */
    int aligned_w;          /* Width rounded to 16 pixels */
    int plane_line_bytes;   /* Bytes per plane per line */
    int total_line_bytes;   /* Bytes for all planes per line */
    int block_size;         /* Bytes per 16-pixel block */

#ifdef SDL_GEM_DIRTY_RECT
    /* Fast change detection (68000-optimized) */
    Uint8   *row_checksums;                            /* Current frame checksums */
    void    *raw_checksum_buffer;                      /* Original malloc ptr for free */
    int      checksum_height;                          /* Height of allocated checksum buffer */
    Uint16   last_frame_counter;                       /* For periodic forced updates */
    SDL_Rect prev_dirty_rects[MAX_MERGED_RECTS];       /* Rects updated in previous frame */
    int      num_prev_dirty_rects;                     /* Number of valid rects in prev_dirty_rects */
#ifdef SDL_GEM_DIRTY_RECT_ASM
    Uint8  *batch_checksums;       /* Reusable temp buffer for batch calculation */
    size_t  batch_checksums_size;  /* Allocated size */
#endif
#endif

} SDL_WindowData;

/* ============================================================
   Driver entry points
   ============================================================ */
extern SDL_VideoDevice *GEM_CreateDevice(void);
extern int   GEM_VideoInit(SDL_VideoDevice *this);
extern void  GEM_VideoQuit(SDL_VideoDevice *this);
extern int   GEM_SetDisplayMode(SDL_VideoDevice *this, SDL_VideoDisplay *display, SDL_DisplayMode *mode);

/* Window */
extern int   GEM_CreateWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_DestroyWindow(SDL_VideoDevice *this, SDL_Window *window);
extern int   GEM_CreateWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window, Uint32 *format, void **pixels, int *pitch);
extern int   GEM_UpdateWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window, const SDL_Rect *rects, int numrects);
extern void  GEM_DestroyWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window);
extern int   GEM_GetWindowDisplayIndex(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_SetWindowTitle(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_SetWindowPosition(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_ShowWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_HideWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_RaiseWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_MaximizeWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_MinimizeWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_RestoreWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_SetWindowBordered(SDL_VideoDevice *this, SDL_Window *window, SDL_bool bordered);
extern void  GEM_SetWindowResizable(SDL_VideoDevice *this, SDL_Window *window, SDL_bool resizable);
extern void  GEM_SetWindowSize(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_SetWindowMinimumSize(SDL_VideoDevice *this, SDL_Window *window);
extern void  GEM_SetWindowMaximumSize(SDL_VideoDevice *this, SDL_Window *window);
extern void   GEM_SetWindowFullscreen(SDL_VideoDevice *this, SDL_Window *window, SDL_VideoDisplay *display, SDL_bool fullscreen);

/* Events */
extern void GEM_PumpEvents(SDL_VideoDevice *this);
extern void GEM_InitEvents(SDL_VideoDevice *this);
extern void GEM_QuitEvents(SDL_VideoDevice *this);

/* ============================================================
   C2P – unified LUT entry point (SDL_gemc2p.c)
   ============================================================ */

/* Primary planar converter – always LUT-based.
   Pass lut=NULL for identity (no palette remapping). */
extern void Atari_C2P_Planar_LUT(void *src, void *dst, int width, int height,
                                  int src_pitch, int dst_pitch,
                                  int planes, Uint8 *lut);

/* Convenience wrapper (lut=NULL path) */
extern void Atari_C2P_Planar(void *src, void *dst, int width, int height,
                              int src_pitch, int dst_pitch, int planes);

/* ============================================================
   RGB332 ↔ TrueColor converters
   ============================================================ */
extern void Atari_ConvertRGB332toRGB565(const Uint8 *src, Uint16 *dst,
                                        int width, int height,
                                        int src_pitch, int dst_pitch);
extern void Atari_ConvertRGB332toRGB888(const Uint8 *src, Uint8 *dst,
                                        int width, int height,
                                        int src_pitch, int dst_pitch);
extern void Atari_ConvertRGB332toBGR888(const Uint8 *src, Uint8 *dst,
                                        int width, int height,
                                        int src_pitch, int dst_pitch);
extern void Atari_ConvertRGB332toARGB8888(const Uint8 *src, Uint32 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
extern void Atari_ConvertRGB332toBGRA8888(const Uint8 *src, Uint32 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);

/* 32-bit → RGB332 */
extern void Atari_ConvertARGB8888toRGB332(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
extern void Atari_ConvertBGRA8888toRGB332(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);

/* 32-bit → 24-bit */
extern void Atari_ConvertBGRA8888toBGR888(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
extern void Atari_ConvertARGB8888toRGB888(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
extern void Atari_ConvertBGRA8888toRGB888(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
extern void Atari_ConvertARGB8888toBGR888(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);

/* 24-bit → RGB332 */
extern void Atari_ConvertRGB888toRGB332(const Uint8 *src, Uint8 *dst,
                                        int width, int height,
                                        int src_pitch, int dst_pitch);
extern void Atari_ConvertBGR888toRGB332(const Uint8 *src, Uint8 *dst,
                                        int width, int height,
                                        int src_pitch, int dst_pitch);

extern void Atari_ConvertRGBA8888toRGB332(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);

/* Shared LUT tables (initialised on first use) */
extern Uint16 rgb332_to_rgb565_lut[256];
extern Uint32 rgb332_to_rgb888_lut[256];
extern Uint32 rgb332_to_argb8888_lut[256];

/* ============================================================
   BlitFast – aligned row-copy for TrueColor modes
   ============================================================ */
#ifdef SDL_GEM_C2P_ASM
    extern void Atari_BlitFast_asm(void*, const void*, int, int, int);
    #define BlitFast(a,b,c,d,e) Atari_BlitFast_asm(a,b,c,d,e)
#else
    extern void Atari_BlitFast_c(void *dst, const void *src,
                                 int row_bytes, int rows, int pitch);
    #define BlitFast(a,b,c,d,e) Atari_BlitFast_c(a,b,c,d,e)
#endif

/* ============================================================
   ASM C2P declarations (SDL_GEM_C2P_ASM only)
   ============================================================ */
#ifdef SDL_GEM_C2P_ASM
   /* LUT + C2P – these are the ONLY ASM C2P variants called from C code */
   extern void Atari_C2P_8to1_LUT_asm(void*, void*, int, int, int, int, Uint8*);
   extern void Atari_C2P_8to2_LUT_asm(void*, void*, int, int, int, int, Uint8*);
   extern void Atari_C2P_8to4_LUT_asm(void*, void*, int, int, int, int, Uint8*);
   extern void Atari_C2P_8to8_LUT_asm(void*, void*, int, int, int, int, Uint8*);

   /* TrueColor converters */
   extern void Atari_ConvertRGB332toRGB565_asm(const Uint8 *src, Uint16 *dst,
                                             int width, int height,
                                             int src_pitch, int dst_pitch);
   extern void Atari_ConvertRGB332toARGB8888_asm(const Uint8 *src, Uint32 *dst,
                                                int width, int height,
                                                int src_pitch, int dst_pitch);
   extern void Atari_ConvertRGB332toBGRA8888_asm(const Uint8 *src, Uint32 *dst,
                                                int width, int height,
                                                int src_pitch, int dst_pitch);
   extern void Atari_ConvertARGB8888toRGB332_asm(const Uint8 *src, Uint8 *dst,
                                                int width, int height,
                                                int src_pitch, int dst_pitch);
   extern void Atari_ConvertBGRA8888toRGB332_asm(const Uint8 *src, Uint8 *dst,
                                                int width, int height,
                                                int src_pitch, int dst_pitch);
   /* 24-bit (true 3-byte) converters */
   extern void Atari_ConvertRGB332toRGB888_asm(const Uint8 *src, Uint8 *dst,
                                       int width, int height,
                                       int src_pitch, int dst_pitch);
   extern void Atari_ConvertRGB332toBGR888_asm(const Uint8 *src, Uint8 *dst,
                                       int width, int height,
                                       int src_pitch, int dst_pitch);

   extern void Atari_ConvertBGRA8888toBGR888_asm(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
   extern void Atari_ConvertARGB8888toRGB888_asm(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
   extern void Atari_ConvertBGRA8888toRGB888_asm(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
   extern void Atari_ConvertARGB8888toBGR888_asm(const Uint8 *src, Uint8 *dst,
                                          int width, int height,
                                          int src_pitch, int dst_pitch);
   extern void Atari_ConvertRGB888toRGB332_asm(const Uint8 *src, Uint8 *dst,
                                       int width, int height,
                                       int src_pitch, int dst_pitch);
   extern void Atari_ConvertBGR888toRGB332_asm(const Uint8 *src, Uint8 *dst,
                                       int width, int height,
                                       int src_pitch, int dst_pitch);
   extern void Atari_ConvertRGBA8888toRGB332_asm(const Uint8 *src, Uint8 *dst,
                                                int width, int height,
                                                int src_pitch, int dst_pitch);                                 
#endif /* SDL_GEM_C2P_ASM */

/* ============================================================
   Dirty-rect checksum helpers (SDL_gemchecksum.S)
   ============================================================ */
#ifdef SDL_GEM_DIRTY_RECT_ASM
    extern void  Atari_CalculateRowChecksumsBlock(const Uint8 *buffer,
                     Uint8 *checksums, int num_rows, int pitch, int bytes_per_row);
    extern Uint8 Atari_CalculateRowChecksum(const Uint8 *row, int len);
    extern Uint8 Atari_CalculateRowChecksum_320(const Uint8 *row);
    extern Uint8 Atari_CalculateRowChecksum_640(const Uint8 *row);
#define CALCULATE_ROW_CHECKSUM(r,l) \
    ((l) == 320 ? Atari_CalculateRowChecksum_320(r) : \
     (l) == 640 ? Atari_CalculateRowChecksum_640(r) : \
     Atari_CalculateRowChecksum(r,l))
#else
    #define CALCULATE_ROW_CHECKSUM(r,l) CalculateRowChecksum(r,l)
#endif

#ifndef MFDB_STRIDE
#define MFDB_STRIDE(w) (((w) + 15) & ~15)
#endif

#endif /* SDL_gemvideo_h_ */