#include "SDL_gemwindow.h"
#include "SDL_gemvideo.h"
#include <mint/sysbind.h>
#include <gem.h>

#include "SDL_render.h"
#include "../../render/software/SDL_render_sw_c.h"

int GEM_CreateWindow(_THIS, SDL_Window *window)
{
    struct SDL_VideoData *video = (struct SDL_VideoData *)_this->driverdata;
    SDL_WindowData *data;
    short wx, wy, ww, wh;
    int window_attr;

    /* Allocate window data */
    data = (SDL_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }

    window->driverdata = data;

    /* Set up window attributes */
    window_attr = NAME | MOVER;  /* Always have title bar and mover */
    
    if (!(window->flags & SDL_WINDOW_BORDERLESS)) {
        window_attr |= CLOSER | FULLER | SIZER;
    }

    /* Calculate window position and size */
    wx = (window->x < 0) ? video->work_x : window->x;
    wy = (window->y < 0) ? video->work_y : window->y;
    ww = window->w;
    wh = window->h;

    /* Create GEM window */
    data->handle = wind_create(window_attr, wx, wy, ww, wh);
    if (data->handle < 0) {
        SDL_free(data);
        return SDL_SetError("Could not create GEM window");
    }

    /* Get work area */
    wind_get_grect(data->handle, WF_WORKXYWH, (GRECT *)&data->work_x);

    /* Handle fullscreen */
    if (window->flags & SDL_WINDOW_FULLSCREEN) {
        data->fullscreen = SDL_TRUE;
        wind_set(data->handle, WF_FULLSCREEN, 1, 0, 0, 0);
    }

    /* Open the window */
    wind_open(data->handle, wx, wy, ww, wh);

    /* Create window buffer */
    if (!data->fullscreen) {
        data->buffer = SDL_malloc(data->work_w * data->work_h * 2); /* Assuming 16-bit color */
        if (!data->buffer) {
            wind_close(data->handle);
            wind_delete(data->handle);
            SDL_free(data);
            return SDL_OutOfMemory();
        }
    } else {
        /* In fullscreen, use screen buffer directly */
        data->buffer = Physbase();
    }

    return 0;
}

// int GEM_CreateWindowFramebuffer(_THIS, SDL_Window *window, 
//                               Uint32 *format, void **pixels, int *pitch)
// {
//     SDL_Surface *surface;
//     int w, h;
//     const Uint32 surface_format = SDL_PIXELFORMAT_RGB565;
//     SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
//     *format = SDL_PIXELFORMAT_RGB565;  /* Most common format for Atari */
//     SDL_GetWindowSize(window, &w, &h);
//     *format = surface_format;
//     *pitch = (((w * SDL_BYTESPERPIXEL(surface_format)) + 3) & ~3);    
//     surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 16, surface_format);
//     if (!surface) {
//         return -1;
//     }
//     *pixels = surface->pixels;
//     /* Save the surface as window data */
//     SDL_SetWindowData(window, "SDL_WindowFramebuffer", surface);
//     return 0;
// }

int GEM_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    int bpp;
    Uint32 Rmask, Gmask, Bmask, Amask;

    /* Set up the format - typically RGB565 for Atari */
    *format = SDL_PIXELFORMAT_RGB565;
    SDL_PixelFormatEnumToMasks(*format, &bpp, &Rmask, &Gmask, &Bmask, &Amask);

    /* Calculate pitch */
    *pitch = (((window->w * bpp) + 31) & ~31) / 8;

    /* Allocate buffer */
    *pixels = SDL_malloc(window->h * (*pitch));
    if (*pixels == NULL) {
        return SDL_OutOfMemory();
    }

    /* Clear the buffer */
    SDL_memset(*pixels, 0, window->h * (*pitch));

    return 0;
}

int GEM_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    MFDB src, dst;
    short pxy[8];
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    void *pixels;
    int pitch;

    pixels = SDL_GetWindowData(window, "SDL_WindowFramebuffer");
    if (!pixels) {
        return SDL_SetError("No framebuffer to update");
    }

    /* Copy to screen using VDI functions */
    
    src.fd_addr = pixels;
    src.fd_w = window->w;
    src.fd_h = window->h;
    src.fd_wdwidth = (window->w + 15) / 16;
    src.fd_stand = 0;
    src.fd_nplanes = 16;  /* For RGB565 */

    dst.fd_addr = 0;  /* Physical screen */

    for (int i = 0; i < numrects; ++i) {
        pxy[0] = rects[i].x;
        pxy[1] = rects[i].y;
        pxy[2] = rects[i].x + rects[i].w - 1;
        pxy[3] = rects[i].y + rects[i].h - 1;
        pxy[4] = pxy[0];
        pxy[5] = pxy[1];
        pxy[6] = pxy[2];
        pxy[7] = pxy[3];

        vro_cpyfm(data->vdi_handle, S_ONLY, pxy, &src, &dst);
    }

    return 0;
}

// int GEM_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
//                               const SDL_Rect *rects, int numrects)
// {
//     SDL_Surface *surface;
//     SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
//     struct SDL_VideoData *video = (struct SDL_VideoData *)_this->driverdata;
//     int i;
//     surface = (SDL_Surface *)SDL_GetWindowData(window, "SDL_WindowFramebuffer");
//     if (!surface) {
//         return SDL_SetError("Couldn't find framebuffer surface");
//     }
//     if (!data->fullscreen) {
//         /* Update window content */
//         for (i = 0; i < numrects; i++) {
//             const SDL_Rect *rect = &rects[i];
//             GRECT gem_rect;
//             gem_rect.g_x = data->work_x + rect->x;
//             gem_rect.g_y = data->work_y + rect->y;
//             gem_rect.g_w = rect->w;
//             gem_rect.g_h = rect->h;
//             wind_update(BEG_UPDATE);
//             graf_mouse(M_OFF, NULL);
//             /* Copy buffer to screen */
//             vro_cpyfm(video->vdi_handle, S_ONLY,
//                      (short *)&gem_rect,
//                      (MFDB *)data->buffer,
//                      (MFDB *)Physbase());
//             graf_mouse(M_ON, NULL);
//             wind_update(END_UPDATE);
//         }
//     }
//     return 0;
// }

// void GEM_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
// {
//     SDL_Surface *surface;
//     SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
//     surface = (SDL_Surface *)SDL_GetWindowData(window, "SDL_WindowFramebuffer");
//     SDL_FreeSurface(surface);
//     SDL_SetWindowData(window, "SDL_WindowFramebuffer", NULL);
//     if (!data->fullscreen) {
//         SDL_free(data->buffer);
//         data->buffer = NULL;
//     }
// }

void GEM_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    void *pixels = SDL_GetWindowData(window, "SDL_WindowFramebuffer");
    if (pixels) {
        SDL_free(pixels);
        SDL_SetWindowData(window, "SDL_WindowFramebuffer", NULL);
    }
}
void GEM_DestroyWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;

    if (!data) {
        return;
    }

    /* Close and delete GEM window */
    wind_close(data->handle);
    wind_delete(data->handle);

    /* Free window buffer if we created one */
    if (!data->fullscreen) {
        SDL_free(data->buffer);
    }

    SDL_free(data);
    window->driverdata = NULL;
}
