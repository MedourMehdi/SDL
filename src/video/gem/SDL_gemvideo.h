/* ============================================
   SDL_gemvideo.h - Step 1 Optimized (FIXED)
   m68000: Precompute at VideoInit
   C90 Compliant
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
#define MERGE_THRESHOLD 16

/* Video driver data - PRECOMPUTED at VideoInit */
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
    Uint8 rgb332_to_hw[256];       /* SDL RGB332 -> hardware index LUT */
    int use_identity_palette;      /* 1=fast path, 0=LUT remapping */
    int palette_initialized;       /* 1=LUT is valid */
    
} SDL_VideoData;

/* Window data */
typedef struct SDL_WindowData {
    short handle;
    short win_type;
    short win_x, win_y, win_w, win_h;
    short work_x, work_y, work_w, work_h;
    short last_w, last_h;
    SDL_bool is_maximized;
    GRECT restore_rect;
    
    /* Framebuffer */
    void *buffer;                  /* SDL chunky buffer (aligned) */
    void *raw_buffer;              /* Original malloc ptr for free */
    void *final_buffer;            /* VDI planar/packed buffer (aligned) */
    void *raw_final_buffer;        /* Original malloc ptr for free */
    MFDB final_mfdb;
    unsigned short buffer_pitch;
    size_t final_buffer_size;
    
    /* Remap buffer (only allocated if !use_identity_palette) */
    void *remap_buffer;
    void *raw_remap_buffer;
    size_t remap_buffer_size;
    
    /* PRECOMPUTED C2P constants (calculated once in CreateWindowFramebuffer) */
    int aligned_w;                 /* Width rounded to 16 pixels */
    int plane_line_bytes;          /* Bytes per plane per line */
    int total_line_bytes;          /* Bytes for all planes per line */
    int block_size;                /* Bytes per 16-pixel block */
    #ifdef SDL_GEM_DIRTY_RECT
    /* === STEP 4: FAST CHANGE DETECTION (68000-optimized) === */
    /* One byte checksum per row. Aligned to 16 bytes for 68000 movem */
    Uint8 *row_checksums;        /* Current frame checksums */
    void *raw_checksum_buffer;   /* Original malloc ptr for free */
    Uint16 last_frame_counter;   /* For periodic forced updates */
    SDL_Rect prev_dirty_rects[MAX_MERGED_RECTS];   /* rectangles updated in previous frame */
    int      num_prev_dirty_rects;                 /* number of valid rects in prev_dirty_rects */    
    #endif 
} SDL_WindowData;

/* Driver functions */
extern SDL_VideoDevice *GEM_CreateDevice(void);
extern int GEM_VideoInit(SDL_VideoDevice *this);
extern void GEM_VideoQuit(SDL_VideoDevice *this);
extern int GEM_SetDisplayMode(SDL_VideoDevice *this, SDL_VideoDisplay *display, SDL_DisplayMode *mode);

/* Window functions */
extern int GEM_CreateWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_DestroyWindow(SDL_VideoDevice *this, SDL_Window *window);
extern int GEM_CreateWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window, Uint32 *format, void **pixels, int *pitch);
extern int GEM_UpdateWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window, const SDL_Rect *rects, int numrects);
extern void GEM_DestroyWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window);
extern int GEM_GetWindowDisplayIndex(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_SetWindowTitle(SDL_VideoDevice *this, SDL_Window *window);

extern void GEM_SetWindowPosition(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_ShowWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_HideWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_RaiseWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_MaximizeWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_MinimizeWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_RestoreWindow(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_SetWindowBordered(SDL_VideoDevice *this, SDL_Window *window, SDL_bool bordered);
extern void GEM_SetWindowResizable(SDL_VideoDevice *this, SDL_Window *window, SDL_bool resizable);
extern void GEM_SetWindowSize(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_SetWindowMinimumSize(SDL_VideoDevice *this, SDL_Window *window);
extern void GEM_SetWindowMaximumSize(SDL_VideoDevice *this, SDL_Window *window);

/* Event functions */
extern void GEM_PumpEvents(SDL_VideoDevice *this);
extern void GEM_InitEvents(SDL_VideoDevice *this);
extern void GEM_QuitEvents(SDL_VideoDevice *this);

extern void Atari_C2P_Planar(void *src, void *dst, int width, int height,
                             int src_pitch, int dst_pitch, int planes);

#ifdef SDL_GEM_C2P_ASM

   extern void Atari_BlitFast_asm(void*, const void*, int, int, int);
    #define BlitFast(a, b, c, d, e) Atari_BlitFast_asm(a, b, c, d, e)
   extern void Atari_C2P_Planar_LUT(void *src, void *dst, int width, int height,
                          int src_pitch, int dst_pitch, int planes, Uint8 *lut);

#else

   extern void Atari_BlitFast_c(void *dst, const void *src, int row_bytes,
                           int rows, int pitch);
    #define BlitFast(a, b, c, d, e) Atari_BlitFast_c(a, b, c, d, e)
    
#endif

#ifdef SDL_GEM_DIRTY_RECT_ASM
extern Uint8 Atari_CalculateRowChecksum(const Uint8 *row, int len);
extern Uint8 Atari_CalculateRowChecksum_320(const Uint8 *row);
#define CALCULATE_ROW_CHECKSUM(r, l) \
    ((l) == 320 ? Atari_CalculateRowChecksum_320(r) : Atari_CalculateRowChecksum(r, l))
#endif /* SDL_GEM_DIRTY_RECT_ASM */

/* Globals - sdl_global_aes and gl_apid already declared in SDL_sysvideo.h */

#endif /* SDL_gemvideo_h_ */