/* ============================================
   FILE: src/video/gem/SDL_gemvideo.h
   GEM video driver header - FINAL PRODUCTION VERSION
   ============================================ */
#ifndef SDL_gemvideo_h_
#define SDL_gemvideo_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "../../events/SDL_events_c.h"
#include "../atari/SDL_atarivideo.h"

#include <mt_gem.h>

/* Video driver data */
typedef struct SDL_VideoData {
    short vdi_handle;
    short desk_x, desk_y, desk_w, desk_h;
    short work_x, work_y, work_w, work_h;
    short planes;
} SDL_VideoData;

/* Window data */
typedef struct SDL_WindowData {
    short handle;
    short win_type;
    /* Size tracking for buffer management */
    short win_x, win_y, win_w, win_h;
    short work_x, work_y, work_w, work_h;
    short last_w, last_h;
    SDL_bool is_maximized;
    GRECT restore_rect;
    
    /* Redraw state - eliminates global variables and logic duplication */
    SDL_bool in_gem_redraw;
    GRECT gem_clip_rect;
    
    void *buffer;
    MFDB planar_mfdb;
    void *planar_buffer;
    int buffer_pitch;
} SDL_WindowData;

/* Function prototypes */
extern SDL_VideoDevice *GEM_CreateDevice(void);
extern int GEM_VideoInit(_THIS);
extern void GEM_VideoQuit(_THIS);
extern int GEM_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);

/* Window functions */
extern int GEM_CreateWindow(_THIS, SDL_Window *window);
extern void GEM_DestroyWindow(_THIS, SDL_Window *window);
extern int GEM_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch);
extern int GEM_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects);
extern void GEM_DestroyWindowFramebuffer(_THIS, SDL_Window *window);

extern void GEM_SetWindowPosition(_THIS, SDL_Window *window);
extern void GEM_ShowWindow(_THIS, SDL_Window *window);
extern void GEM_HideWindow(_THIS, SDL_Window *window);
extern void GEM_RaiseWindow(_THIS, SDL_Window *window);
extern void GEM_MaximizeWindow(_THIS, SDL_Window *window);
extern void GEM_MinimizeWindow(_THIS, SDL_Window *window);
extern void GEM_RestoreWindow(_THIS, SDL_Window *window);
extern void GEM_SetWindowBordered(_THIS, SDL_Window *window, SDL_bool bordered);
extern void GEM_SetWindowResizable(_THIS, SDL_Window *window, SDL_bool resizable);
extern void GEM_SetWindowSize(_THIS, SDL_Window *window);
extern void GEM_SetWindowMinimumSize(_THIS, SDL_Window *window);
extern void GEM_SetWindowMaximumSize(_THIS, SDL_Window *window);

/* Event functions */
extern void GEM_PumpEvents(_THIS);
extern void GEM_InitEvents(_THIS);
extern void GEM_QuitEvents(_THIS);

/* Desktop workspace boundaries */
extern int16_t wdesk, hdesk;

#endif /* SDL_gemvideo_h_ */