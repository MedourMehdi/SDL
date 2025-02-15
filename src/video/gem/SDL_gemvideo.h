#ifndef SDL_gemvideo_h_
#define SDL_gemvideo_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "SDL_render.h"
#include <gem.h>

/* Hidden "this" pointer for the video functions */
#define _THIS SDL_VideoDevice *_this

/* Private display data */
struct SDL_VideoData {
    short desk_x;        /* Desktop resolution */
    short desk_y;
    short desk_w;
    short desk_h;
    short work_x;        /* Work area resolution */
    short work_y;
    short work_w;
    short work_h;
    short vdi_handle;    /* VDI handle */
    short win_handle;    /* GEM window handle */
    void *screen;        /* Screen buffer */
    int pitch;          /* Screen pitch */
    SDL_bool use_vdi;   /* Use VDI or direct hardware */    
    short handle;          /* GEM window handle */
    void *buffer;         /* Window buffer */
    SDL_bool fullscreen;  /* Fullscreen flag */
};

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

#endif /* SDL_gemvideo_h_ */
