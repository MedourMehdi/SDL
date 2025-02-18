#ifndef SDL_gemvideo_h_
#define SDL_gemvideo_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "SDL_render.h"
#include <gem.h>

/* Hidden "this" pointer for the video functions */
#define _THIS SDL_VideoDevice *_this

typedef struct SDL_VideoData {
    short desk_x;        /* Desktop resolution */
    short desk_y;
    short desk_w;
    short desk_h;
    short work_x;        /* Work area resolution */
    short work_y;
    short work_w;
    short work_h;
    short vdi_handle;    /* VDI handle */
    SDL_bool use_vdi;   /* Use VDI or direct hardware */
    short planes;
} SDL_VideoData;

/* GEM driver bootstrap functions */
int GEM_Available(void);
// SDL_VideoDevice *GEM_CreateDevice(int devindex);

/* GEM video functions */
int GEM_VideoInit(_THIS);
void GEM_VideoQuit(_THIS);
int GEM_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
int GEM_CreateWindow(_THIS, SDL_Window *window);
int GEM_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch);
int GEM_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects);
void GEM_DestroyWindowFramebuffer(_THIS, SDL_Window *window);
void GEM_DestroyWindow(_THIS, SDL_Window *window);
void GEM_SetWindowPosition(_THIS, SDL_Window *window);
void GEM_ShowWindow(_THIS, SDL_Window *window);
void GEM_HideWindow(_THIS, SDL_Window *window);
void GEM_RaiseWindow(_THIS, SDL_Window *window);
void GEM_MaximizeWindow(_THIS, SDL_Window *window);
void GEM_MinimizeWindow(_THIS, SDL_Window *window);
void GEM_RestoreWindow(_THIS, SDL_Window *window);
void GEM_SetWindowBordered(_THIS, SDL_Window *window, SDL_bool bordered);
void GEM_SetWindowResizable(_THIS, SDL_Window *window, SDL_bool resizable);
void GEM_SetWindowSize(_THIS, SDL_Window *window);
void GEM_SetWindowMinimumSize(_THIS, SDL_Window *window);
void GEM_SetWindowMaximumSize(_THIS, SDL_Window *window);
#endif /* SDL_gemvideo_h_ */
