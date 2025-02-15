#include "SDL_atariwindow.h"
#include "SDL_atarivideo.h"
#include <mint/osbind.h>

int ATARI_CreateWindow(_THIS, SDL_Window *window)
{
    SDL_VideoData *video = (SDL_VideoData *)_this->driverdata;
    SDL_WindowData *data;

    /* Allocate window data */
    data = (SDL_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }

    window->driverdata = data;

    /* In non-GEM mode, we always use fullscreen */
    data->fullscreen = SDL_TRUE;
    data->buffer = video->screen_base;

    return 0;
}

int ATARI_CreateWindowFramebuffer(_THIS, SDL_Window *window,
                                Uint32 *format, void **pixels, int *pitch)
{
    SDL_VideoData *video = (SDL_VideoData *)_this->driverdata;
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;

    *format = SDL_PIXELFORMAT_INDEX8;  // Default to 8-bit
    switch(video->screen_bpp) {
        case 4:
            *format = SDL_PIXELFORMAT_INDEX4LSB;
            break;
        case 8:
            *format = SDL_PIXELFORMAT_INDEX8;
            break;
        case 16:
            *format = SDL_PIXELFORMAT_RGB565;
            break;
    }

    *pixels = data->buffer;
    *pitch = video->screen_pitch;

    return 0;
}

int ATARI_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
                                const SDL_Rect *rects, int numrects)
{
    /* In direct hardware mode, updates are immediate */
    return 0;
}

void ATARI_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    /* Nothing to do, we use direct hardware access */
}

void ATARI_DestroyWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;

    if (data) {
        SDL_free(data);
        window->driverdata = NULL;
    }
}
