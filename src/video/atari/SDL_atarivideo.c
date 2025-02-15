#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_ATARI

#include "../SDL_sysvideo.h"
#include "SDL_atarivideo.h"
#include "SDL_atarihw.h"

static int ATARI_VideoInit(_THIS);
static void ATARI_VideoQuit(_THIS);
static void ATARI_DeleteDevice(SDL_VideoDevice * device);

static int ATARI_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format, void ** pixels, int *pitch);
static void ATARI_UpdateWindowFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects);
static void ATARI_DestroyWindowFramebuffer(_THIS, SDL_Window * window);

/* ATARI video driver bootstrap functions */
static int ATARI_Available(void)
{
    return 1;
}

static void ATARI_DeleteDevice(SDL_VideoDevice * device)
{
    SDL_free(device->driverdata);
    SDL_free(device);
}

static SDL_VideoDevice *ATARI_CreateDevice(void)
{
    SDL_VideoDevice *device;
    SDL_VideoData *data;

    /* Initialize device structure */
    device = (SDL_VideoDevice *) SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Initialize internal data */
    data = (SDL_VideoData *) SDL_calloc(1, sizeof(SDL_VideoData));
    if (!data) {
        SDL_OutOfMemory();
        SDL_free(device);
        return NULL;
    }

    device->driverdata = data;

    /* Setup the function pointers */
    device->VideoInit = ATARI_VideoInit;
    device->VideoQuit = ATARI_VideoQuit;
    device->CreateWindowFramebuffer = ATARI_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = ATARI_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = ATARI_DestroyWindowFramebuffer;
    device->free = ATARI_DeleteDevice;

    return device;
}

static int ATARI_VideoInit(_THIS)
{
    /* Initialize internal data */
    SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;
    if (!data) {
        return SDL_SetError("No driver data available");
    }

    /* Initialize video modes */
    ATARI_InitModes(_this);

    return 0;
}

static void ATARI_VideoQuit(_THIS)
{
    /* Nothing to do here */
}

static int ATARI_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format, void ** pixels, int *pitch)
{
    SDL_VideoData *data = (SDL_VideoData *) _this->driverdata;
    
    *format = SDL_PIXELFORMAT_RGB565;
    *pixels = data->screen_base;
    *pitch = data->screen_pitch;
    
    return 0;
}

static void ATARI_UpdateWindowFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects)
{
    /* Hardware framebuffer - no update needed */
}

static void ATARI_DestroyWindowFramebuffer(_THIS, SDL_Window * window)
{
    /* Hardware framebuffer - nothing to destroy */
}

VideoBootStrap ATARI_bootstrap = {
    "atari", "Atari video driver",
    ATARI_CreateDevice
};

#endif /* SDL_VIDEO_DRIVER_ATARI */
