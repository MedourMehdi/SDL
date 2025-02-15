/* Keep only necessary includes */
#include "SDL_gemvideo.h"
#include "SDL_gemevents.h"
#include <gem.h>
#include <gemx.h>
#include <mint/osbind.h>
#include <mint/cookie.h>

/* Defines from old GEM/GEMX headers */
#ifndef APP_FIRST
#define APP_FIRST 0x0001
#endif

static SDL_VideoDevice *GEM_Create(void);
static int GEM_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid);

/* Global variables for AES */
extern short gl_apid;    /* Application ID */
extern short gl_ap_version;  /* AES version */
extern short aes_global[];   /* Global array */

int GEM_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    struct SDL_VideoData *data = (struct SDL_VideoData *)_this->driverdata;

    /* For now, just verify the mode is supported */
    if (mode->w != data->desk_x || mode->h != data->desk_y) {
        return SDL_SetError("Requested display mode not supported");
    }

    /* Mode is supported, nothing to do since GEM handles the screen */
    return 0;
}

int ATARI_InitModes(_THIS)
{
    struct SDL_VideoData *data = (struct SDL_VideoData *)_this->driverdata;
    SDL_DisplayMode mode;

    /* Add the desktop mode */
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = data->desk_x;
    mode.h = data->desk_y;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;
    
    if (SDL_AddDisplayMode(SDL_GetDisplay(0), &mode) < 0) {
        return -1;
    }

    return 0;
}


int GEM_Available(void)
{
    short work_in[11], work_out[57];
    short dummy;
    
    /* Initialize AES */
    printf("GEM: Trying to initialize AES...\n");
    gl_apid = appl_init();
    if (gl_apid == -1) {
        printf("GEM: AES initialization failed (appl_init returned -1)\n");
        return 0;
    }
    printf("GEM: AES initialized, gl_apid = %d\n", gl_apid);

    /* Check VDI workstation */
    printf("GEM: Getting VDI handle...\n");
    dummy = graf_handle(&work_out[0], &work_out[1], 
                       &work_out[2], &work_out[3]);
    printf("GEM: graf_handle returned %d\n", dummy);
    
    for (int i = 0; i < 10; i++) {
        work_in[i] = 1;
    }
    work_in[10] = 2;
    
    printf("GEM: Opening VDI workstation...\n");
    v_opnvwk(work_in, &dummy, work_out);
    
    if (dummy == 0) {
        printf("GEM: Failed to open VDI workstation\n");
        appl_exit();
        return 0;
    }
    printf("GEM: VDI workstation opened successfully\n");
    
    v_clsvwk(dummy);
    appl_exit();
    
    printf("GEM: Driver available\n");
    return 1;
}

static void GEM_DeleteDevice(SDL_VideoDevice * device)
{
    if (device->driverdata) {
        SDL_free(device->driverdata);
    }
    SDL_free(device);
}

static SDL_VideoDevice *GEM_CreateDevice(int devindex)
{
    SDL_VideoDevice *device;
    struct SDL_VideoData *data;

    /* Initialize device structure */
    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Initialize internal data */
    data = (struct SDL_VideoData *)SDL_calloc(1, sizeof(struct SDL_VideoData));
    if (!data) {
        SDL_OutOfMemory();
        SDL_free(device);
        return NULL;
    }

    device->driverdata = data;

    /* Set function pointers */
    device->VideoInit = GEM_VideoInit;
    device->VideoQuit = GEM_VideoQuit;
    device->SetDisplayMode = GEM_SetDisplayMode;
    device->CreateSDLWindow = GEM_CreateWindow;
    device->CreateWindowFramebuffer = GEM_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = GEM_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = GEM_DestroyWindowFramebuffer;
    device->DestroyWindow = GEM_DestroyWindow;
    device->free = GEM_DeleteDevice;

    /* Event handling */
    device->PumpEvents = GEM_PumpEvents;

    return device;
}

int GEM_VideoInit(_THIS)
{
    SDL_DisplayMode mode;
    SDL_VideoDisplay display;
    struct SDL_VideoData *data = (struct SDL_VideoData *)_this->driverdata;
    short work_in[11], work_out[57];
    int i;

    /* Initialize AES */
    gl_apid = appl_init();
    if (gl_apid == -1) {
        return SDL_SetError("Can't initialize AES");
    }

    /* Get desktop size */
    wind_get_grect(DESK, WF_WORKXYWH, (GRECT *)&data->work_x);
    wind_get_grect(DESK, WF_CURRXYWH, (GRECT *)&data->desk_x);

    /* Initialize VDI */
    data->vdi_handle = graf_handle(&work_out[0], &work_out[1], 
                                  &work_out[2], &work_out[3]);
    
    for (i = 0; i < 10; i++) {
        work_in[i] = 1;
    }
    work_in[10] = 2;
    
    v_opnvwk(work_in, &data->vdi_handle, work_out);
    if (data->vdi_handle == 0) {
        appl_exit();
        return SDL_SetError("Can't initialize VDI");
    }

    /* Add display mode */
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = data->desk_x;
    mode.h = data->desk_y;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;

    /* Create and add the display */
    SDL_zero(display);
    display.desktop_mode = mode;
    display.current_mode = mode;
    display.driverdata = NULL;

    if (SDL_AddVideoDisplay(&display, SDL_FALSE) < 0) {
        v_clsvwk(data->vdi_handle);
        appl_exit();
        return -1;
    }

    return 0;
}

void GEM_VideoQuit(_THIS)
{
    struct SDL_VideoData *data = (struct SDL_VideoData *)_this->driverdata;

    /* Close VDI workstation */
    if (data->vdi_handle) {
        v_clsvwk(data->vdi_handle);
        data->vdi_handle = 0;
    }

    /* Close AES */
    appl_exit();
}

/* Implementation of the create function that matches the signature */
static SDL_VideoDevice *GEM_Create(void)
{
    return GEM_CreateDevice(0);  /* We always use device index 0 */
}

/* Basic message box implementation */
static int GEM_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid)
{
    /* For now, just return -1 to indicate we don't support message boxes */
    return -1;
}

/* Add this new function for the bootstrap */
static SDL_VideoDevice *GEM_CreateDriver(void) 
{
    if (!GEM_Available()) {
        return NULL;
    }
    return GEM_CreateDevice(0);
}

VideoBootStrap GEM_bootstrap = {
    "gem",
    "GEM video driver",
    GEM_CreateDriver,    /* Now using the correct function type */
    GEM_ShowMessageBox
};

/* Implement the renderer functions */
// static SDL_Renderer *GEM_CreateRenderer(SDL_Window *window, Uint32 flags)
// {
//     SDL_Renderer *renderer;
//     GEM_RenderData *data;

//     renderer = (SDL_Renderer *)SDL_calloc(1, sizeof(*renderer));
//     if (!renderer) {
//         SDL_OutOfMemory();
//         return NULL;
//     }

//     data = (GEM_RenderData *)SDL_calloc(1, sizeof(*data));
//     if (!data) {
//         SDL_free(renderer);
//         SDL_OutOfMemory();
//         return NULL;
//     }

//     renderer->WindowEvent = GEM_WindowEvent;
//     renderer->CreateTexture = GEM_CreateTexture;
//     renderer->UpdateTexture = GEM_UpdateTexture;
//     renderer->LockTexture = GEM_LockTexture;
//     renderer->UnlockTexture = GEM_UnlockTexture;
//     renderer->SetRenderTarget = GEM_SetRenderTarget;
//     renderer->UpdateViewport = GEM_UpdateViewport;
//     renderer->RenderClear = GEM_RenderClear;
//     renderer->RenderDrawPoints = GEM_RenderDrawPoints;
//     renderer->RenderDrawLines = GEM_RenderDrawLines;
//     renderer->RenderFillRects = GEM_RenderFillRects;
//     renderer->RenderCopy = GEM_RenderCopy;
//     renderer->RenderCopyEx = GEM_RenderCopyEx;
//     renderer->RenderReadPixels = GEM_RenderReadPixels;
//     renderer->RenderPresent = GEM_RenderPresent;
//     renderer->DestroyTexture = GEM_DestroyTexture;
//     renderer->DestroyRenderer = GEM_DestroyRenderer;
//     renderer->SetRenderDrawColor = GEM_SetRenderDrawColor;

//     renderer->info.name = "gem";
//     renderer->info.flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;

//     renderer->window = window;
//     renderer->driverdata = data;

//     return renderer;
// }

// static void GEM_DestroyRenderer(SDL_Renderer *renderer)
// {
//     GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;
//     SDL_free(data);
//     SDL_free(renderer);
// }

// static int GEM_RenderClear(SDL_Renderer *renderer)
// {
//     GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;
//     /* Clear the screen using VDI functions */
//     vsf_color(data->vdi_handle, data->fill_color);
//     v_bar(data->vdi_handle, 0, 0, renderer->window->w, renderer->window->h);
//     return 0;
// }

// static int GEM_RenderPresent(SDL_Renderer *renderer)
// {
//     /* GEM updates immediately, so nothing to do here */
//     return 0;
// }

// static int GEM_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
// {
//     GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;
//     /* Convert RGB to GEM color index */
//     data->fill_color = rgb_to_vdi_color(r, g, b);
//     return 0;
// }