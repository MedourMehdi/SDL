#ifndef SDL_gemwindow_h_
#define SDL_gemwindow_h_

#include "../SDL_sysvideo.h"

// typedef struct SDL_WindowData {
//     short desk_x;        /* Desktop resolution */
//     short desk_y;
//     short desk_w;
//     short desk_h;
//     short work_x;        /* Work area resolution */
//     short work_y;
//     short work_w;
//     short work_h;
//     short vdi_handle;    /* VDI handle */
//     short win_handle;    /* GEM window handle */
//     void *screen;        /* Screen buffer */
//     int pitch;          /* Screen pitch */
//     SDL_bool use_vdi;   /* Use VDI or direct hardware */    
//     short handle;          /* GEM window handle */
//     void *buffer;         /* Window buffer */
//     SDL_bool fullscreen;  /* Fullscreen flag */
// } SDL_WindowData;

typedef struct SDL_WindowData {
    short handle;    /* GEM window handle */
    void *screen;        /* Screen buffer */
    int pitch;          /* Screen pitch */
    void *buffer;       /* Window buffer */
    short work_x;        /* Work area resolution */
    short work_y;
    short work_w;
    short work_h;    
    SDL_bool fullscreen; /* Fullscreen flag */
} SDL_WindowData;

extern int GEM_CreateWindow(_THIS, SDL_Window *window);
extern int GEM_CreateWindowFramebuffer(_THIS, SDL_Window *window,
                                     Uint32 *format, void **pixels, int *pitch);
extern int GEM_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
                                     const SDL_Rect *rects, int numrects);
extern void GEM_DestroyWindowFramebuffer(_THIS, SDL_Window *window);
extern void GEM_DestroyWindow(_THIS, SDL_Window *window);
extern int GEM_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
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
#endif /* SDL_gemwindow_h_ */
