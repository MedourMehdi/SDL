#ifndef SDL_atariwindow_h_
#define SDL_atariwindow_h_

#include "../SDL_sysvideo.h"

typedef struct SDL_WindowData {
    void *buffer;         /* Window buffer */
    SDL_bool fullscreen;  /* Fullscreen flag */
} SDL_WindowData;

extern int ATARI_CreateWindow(_THIS, SDL_Window *window);
extern int ATARI_CreateWindowFramebuffer(_THIS, SDL_Window *window,
                                       Uint32 *format, void **pixels, int *pitch);
extern int ATARI_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
                                       const SDL_Rect *rects, int numrects);
extern void ATARI_DestroyWindowFramebuffer(_THIS, SDL_Window *window);
extern void ATARI_DestroyWindow(_THIS, SDL_Window *window);

#endif /* SDL_atariwindow_h_ */
