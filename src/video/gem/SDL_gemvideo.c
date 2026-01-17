/* ============================================
   FILE: src/video/gem/SDL_gemvideo.c
   GEM video driver implementation
   ============================================ */

#include "../../SDL_internal.h"
#include "SDL_gemvideo.h"
#include "../ataricommon/SDL_atarimodel.h"
#include "../ataricommon/SDL_atarikeys.h"
#include <mint/osbind.h>

#ifdef SDL_VIDEO_DRIVER_GEM

/* Atari desktop dimensions */
int16_t wdesk, hdesk;

static int GEM_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid);

int GEM_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    /* GEM manages display modes */
    return 0;
}

static void GEM_DeleteDevice(SDL_VideoDevice *device)
{
    if (device) {
        GEM_VideoQuit(device);
        if (device->driverdata) {
            SDL_free(device->driverdata);
        }
        SDL_free(device);
    }
}

SDL_VideoDevice *GEM_CreateDevice(void)
{
    SDL_VideoDevice *device;
    SDL_VideoData *data;

    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }

    data = (SDL_VideoData *)SDL_calloc(1, sizeof(SDL_VideoData));
    if (!data) {
        SDL_OutOfMemory();
        SDL_free(device);
        return NULL;
    }

    device->driverdata = data;
    device->num_displays = 0;

    device->VideoInit = GEM_VideoInit;
    device->VideoQuit = GEM_VideoQuit;
    device->SetDisplayMode = GEM_SetDisplayMode;
    device->CreateSDLWindow = GEM_CreateWindow;
    device->GetWindowDisplayIndex = GEM_GetWindowDisplayIndex;
    device->SetWindowTitle = GEM_SetWindowTitle;
    device->CreateWindowFramebuffer = GEM_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = GEM_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = GEM_DestroyWindowFramebuffer;
    device->DestroyWindow = GEM_DestroyWindow;
    device->free = GEM_DeleteDevice;

    device->SetWindowPosition = GEM_SetWindowPosition;
    device->ShowWindow = GEM_ShowWindow;
    device->HideWindow = GEM_HideWindow;
    device->RaiseWindow = GEM_RaiseWindow;
    device->MaximizeWindow = GEM_MaximizeWindow;
    device->MinimizeWindow = GEM_MinimizeWindow;
    device->RestoreWindow = GEM_RestoreWindow;
    device->SetWindowBordered = GEM_SetWindowBordered;
    device->SetWindowResizable = GEM_SetWindowResizable;
    device->SetWindowSize = GEM_SetWindowSize;
    device->SetWindowMinimumSize = GEM_SetWindowMinimumSize;
    device->SetWindowMaximumSize = GEM_SetWindowMaximumSize;

    device->PumpEvents = GEM_PumpEvents;
    
    return device;
}

int GEM_VideoInit(_THIS)
{
    SDL_DisplayMode mode;
    SDL_VideoDisplay display;
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    short work_in[11], work_out[57];
    int i;

    /* ============================================
       CRITICAL CHANGE: Don't call appl_init here!
       It's already done in SDL_main wrapper
       ============================================ */
    
    /* Verify AES is initialized */
    if (gl_apid < 0) {
        return SDL_SetError("AES not initialized - gl_apid is invalid");
    }

    /* Get desktop geometry */

    mt_wind_get_grect(DESK, WF_WORKXYWH, (GRECT *)&data->work_x, sdl_global_aes);
    mt_wind_get_grect(DESK, WF_CURRXYWH, (GRECT *)&data->desk_x, sdl_global_aes);

    data->desk_w = data->desk_w;
    data->desk_h = data->desk_h;
    /* Export for window position validation */
    wdesk = data->desk_w;
    hdesk = data->desk_h;

    /* Open VDI workstation */

    data->vdi_handle = mt_graf_handle(&work_out[0], &work_out[1], 
                                  &work_out[2], &work_out[3], sdl_global_aes);

    for (i = 0; i < 10; i++) {
        work_in[i] = 1;
    }
    work_in[10] = 2;
    
    v_opnvwk(work_in, &data->vdi_handle, work_out);
    if (data->vdi_handle == 0) {
        return SDL_SetError("Can't initialize VDI workstation");
    }
    
    /* Query extended workstation info */
    vq_extnd(data->vdi_handle, 1, work_out);
    data->planes = work_out[4];
    
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                "GEM: %dx%d desktop, %d planes", 
                data->desk_w, data->desk_h, data->planes);
    
    /* Setup display mode based on color depth */
    SDL_zero(mode);
    switch (data->planes) {
        case 1:
            mode.format = SDL_PIXELFORMAT_INDEX1MSB;  /* 2 colors */
            break;
        case 2:
            mode.format = SDL_PIXELFORMAT_INDEX4MSB;  /* 4 colors */
            break;
        case 4:
            mode.format = SDL_PIXELFORMAT_INDEX4MSB;  /* 16 colors */
            break;
        case 8:
            mode.format = SDL_PIXELFORMAT_INDEX8;     /* 256 colors */
            break;
        case 16:
            mode.format = SDL_PIXELFORMAT_RGB565;     /* HiColor */
            break;
        case 24:
            mode.format = SDL_PIXELFORMAT_RGB888;     /* TrueColor */
            break;
        case 32:
            mode.format = SDL_PIXELFORMAT_ARGB8888;   /* TrueColor + Alpha */
            break;
        default:
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, 
                       "Unknown plane count %d, defaulting to RGB565", data->planes);
            mode.format = SDL_PIXELFORMAT_RGB565;
            break;
    }
    mode.w = data->desk_w;
    mode.h = data->desk_h;
    mode.refresh_rate = 60;

    /* Register display */
    SDL_zero(display);
    display.desktop_mode = mode;
    display.current_mode = mode;

    if (SDL_AddVideoDisplay(&display, SDL_FALSE) < 0) {
        v_clsvwk(data->vdi_handle);
        return -1;
    }

    return 0;
}

void GEM_VideoQuit(_THIS)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;

    if (!data) {
        return;
    }

    /* Close VDI workstation */
    if (data->vdi_handle) {
        v_clsvwk(data->vdi_handle);
        data->vdi_handle = 0;
    }

    /* ============================================
       CRITICAL CHANGE: Don't call appl_exit here!
       It's done in SDL_main wrapper
       ============================================ */
}

static int GEM_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid)
{
    char alert_str[256];
    char safe_msg[180];
    short result;
    
    if (!messageboxdata || !messageboxdata->message) {
        return SDL_SetError("Invalid message box data");
    }
    
    SDL_strlcpy(safe_msg, messageboxdata->message, sizeof(safe_msg));
    SDL_snprintf(alert_str, sizeof(alert_str), "[1][%s][OK]", safe_msg);
    
    result = mt_form_alert(1, alert_str, sdl_global_aes);

    if (buttonid) {
        *buttonid = (result == 1) ? 0 : -1;
    }
    
    return 0;
}

VideoBootStrap GEM_bootstrap = {
    "gem",
    "GEM video driver",
    GEM_CreateDevice,
    GEM_ShowMessageBox
};

#endif /* SDL_VIDEO_DRIVER_GEM */