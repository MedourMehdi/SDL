/* ============================================
   SDL_gemvideo.c - GEM video driver for SDL2
   ============================================ */

#include "SDL_gemvideo.h"
#include "SDL_gemmodel.h"
#include "SDL_gemkeys.h"
#include <mint/osbind.h>
#include <mt_gemx.h>  /* For vq_scrninfo */

#ifdef SDL_VIDEO_DRIVER_GEM

static int GEM_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid);

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

static void GEM_WarpMouse(SDL_Window *window, int x, int y) { (void)window; (void)x; (void)y; }

static Uint32 GEM_GetGlobalMouseState(int *x, int *y)
{
    short mx, my, mb, ks;
    mt_graf_mkstate(&mx, &my, &mb, &ks, sdl_global_aes);
    *x = mx;
    *y = my;
    return (Uint32)mb;
}

static int GEM_ShowCursor(SDL_Cursor *cursor)
{
    mt_graf_mouse(cursor ? M_ON : M_OFF, 0L, sdl_global_aes);
    return 0;
}

static int GEM_SetRelativeMouseMode(SDL_bool enabled)
{
    mt_graf_mouse(enabled ? M_OFF : M_ON, 0L, sdl_global_aes);
    return 0;
}

static int GEM_CaptureMouse(SDL_Window *window) { (void)window; return 0; }

static void GEM_RegisterMouseDriver(void)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    if (mouse) {
        mouse->WarpMouse = GEM_WarpMouse;
        mouse->GetGlobalMouseState = GEM_GetGlobalMouseState;
        mouse->ShowCursor = GEM_ShowCursor;
        mouse->SetRelativeMouseMode = GEM_SetRelativeMouseMode;
        mouse->CaptureMouse = GEM_CaptureMouse;
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

    GEM_RegisterMouseDriver();

    return device;
}

/* ============================================
   ONE-TIME PALETTE INITIALIZATION (m68000 optimized)
   Called once in VideoInit - saves ~10,000 cycles per window
   ============================================ */

static void InitPaletteLUT(SDL_VideoData *data)
{
    int i, p;
    int num_colors;
    int r, g, b;
    int pr, pg, pb;
    int dr, dg, db, dist;
    int best, min_dist;
    Uint16 expected;
    
    num_colors = 1 << data->planes;
    if (num_colors > 256) num_colors = 256;
    
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                "GEM: Building palette LUT for %d colors (ONE-TIME)", num_colors);
    
    /* STEP 1: Read hardware palette via BIOS Setcolor() - 256 trap calls */
    for (i = 0; i < num_colors; i++) {
        data->hw_palette[i] = (Uint16)Setcolor(i, -1);
    }
    
    /* STEP 2: Check for identity palette (RGB332 matches hardware) */
    data->use_identity_palette = 1;
    for (i = 0; i < num_colors; i++) {
        /* Expected RGB332 in 4-bit format */
        r = ((i >> 5) & 0x07) << 1;  /* 3-bit -> 4-bit (0-14) */
        g = ((i >> 2) & 0x07) << 1;  /* 3-bit -> 4-bit (0-14) */
        b = (i & 0x03) * 5;          /* 2-bit -> 4-bit (0,5,10,15) */
        
        expected = (Uint16)((r << 8) | (g << 4) | b);
        
        if (data->hw_palette[i] != expected) {
            data->use_identity_palette = 0;
            SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                        "GEM: Palette mismatch at %d (hw=0x%04X, expected=0x%04X)",
                        i, data->hw_palette[i], expected);
            break;
        }
    }
    
    /* STEP 3: Build LUT (identity or distance-matched) */
    if (data->use_identity_palette) {
        SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "GEM: Using IDENTITY palette (fast path)");
        for (i = 0; i < 256; i++) {
            data->rgb332_to_hw[i] = (Uint8)i;
        }
    } else {
        SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "GEM: Using distance-matched LUT");
        for (i = 0; i < 256; i++) {
            /* SDL RGB332 color components */
            r = ((i >> 5) & 0x07) << 1;
            g = ((i >> 2) & 0x07) << 1;
            b = (i & 0x03) * 5;
            
            best = 0;
            min_dist = 0x7FFF;
            
            /* Find closest hardware color */
            for (p = 0; p < num_colors; p++) {
                pr = (int)((data->hw_palette[p] >> 8) & 0x0F);
                pg = (int)((data->hw_palette[p] >> 4) & 0x0F);
                pb = (int)(data->hw_palette[p] & 0x0F);
                
                dr = r - pr;
                dg = g - pg;
                db = b - pb;
                dist = dr*dr + dg*dg + db*db;
                
                if (dist < min_dist) {
                    min_dist = dist;
                    best = p;
                    if (dist == 0) break;  /* Perfect match */
                }
            }
            
            data->rgb332_to_hw[i] = (Uint8)best;
        }
    }
    
    data->palette_initialized = 1;
    
    /* Debug: Verify key colors */
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                 "GEM: Black(0x00)->%d White(0xFF)->%d Red(0xE0)->%d",
                 data->rgb332_to_hw[0x00],
                 data->rgb332_to_hw[0xFF],
                 data->rgb332_to_hw[0xE0]);
}

int GEM_VideoInit(SDL_VideoDevice *this)
{
    SDL_DisplayMode mode;
    SDL_VideoDisplay display;
    SDL_VideoData *data;
    short work_in[11];
    short work_out[57];
    int16_t screen_info[272];
    int i;
    
    data = (SDL_VideoData *)this->driverdata;
    
    if (gl_apid < 0) {
        return SDL_SetError("AES not initialized");
    }
    
    /* Detect hardware */
    Atari_DetectHW();
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                "GEM: Hardware: %s (CPU %d)", 
                Atari_GetMachineName(), hw_info.cpu);
    
    /* Get desktop dimensions */
    mt_wind_get_grect(DESK, WF_WORKXYWH, (GRECT *)&data->work_x, sdl_global_aes);
    mt_wind_get_grect(DESK, WF_CURRXYWH, (GRECT *)&data->desk_x, sdl_global_aes);
    
    /* Open VDI workstation */
    data->vdi_handle = mt_graf_handle(NULL, NULL, NULL, NULL, sdl_global_aes);
    if (data->vdi_handle < 1) {
        return SDL_SetError("Can't get VDI handle");
    }
    
    for (i = 0; i < 10; i++) work_in[i] = 1;
    work_in[10] = 2;
    
    v_opnvwk(work_in, &data->vdi_handle, work_out);
    if (data->vdi_handle == 0) {
        return SDL_SetError("Can't open VDI workstation");
    }
    
    vq_extnd(data->vdi_handle, 1, work_out);
    data->planes = work_out[4];
    
    /* ============================================
       VDI FORMAT QUERY (vq_scrninfo)
       ============================================ */
    vq_scrninfo(data->vdi_handle, screen_info);
    data->vdi_pixel_format = screen_info[0];      /* 0, 1, or 2 */
    data->vdi_bits_per_pixel = screen_info[4];    /* Actual bpp */
    
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO,
                "GEM: VDI format=%d bpp=%d planes=%d",
                data->vdi_pixel_format,
                data->vdi_bits_per_pixel,
                data->planes);
    
    /* ============================================
       PALETTE INITIALIZATION
       ============================================ */
    if (data->planes <= 8) {
        InitPaletteLUT(data);
    }
    
    /* Setup display mode */
    SDL_zero(mode);
    
    switch (data->planes) {
        case 1:
        case 2:
        case 4:
        case 8:
            mode.format = SDL_PIXELFORMAT_RGB332;
            break;
        case 16:
            mode.format = SDL_PIXELFORMAT_RGB565;
            break;
        case 24:
            mode.format = SDL_PIXELFORMAT_RGB888;
            break;
        case 32:
            mode.format = SDL_PIXELFORMAT_ARGB8888;
            break;
        default:
            mode.format = SDL_PIXELFORMAT_RGB565;
            break;
    }
    
    mode.w = data->desk_w;
    mode.h = data->desk_h;
    mode.refresh_rate = 60;
    
    SDL_zero(display);
    display.desktop_mode = mode;
    display.current_mode = mode;
    
    if (SDL_AddVideoDisplay(&display, 0) < 0) {
        v_clsvwk(data->vdi_handle);
        return -1;
    }
    
    GEM_InitEvents(this);
    
    return 0;
}

void GEM_VideoQuit(SDL_VideoDevice *this)
{
    SDL_VideoData *data;
    
    data = (SDL_VideoData *)this->driverdata;
    if (!data) return;
    
    GEM_QuitEvents(this);
    
    if (data->vdi_handle) {
        v_clsvwk(data->vdi_handle);
        data->vdi_handle = 0;
    }
}

int GEM_SetDisplayMode(SDL_VideoDevice *this, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    (void)this;
    (void)display;
    (void)mode;
    return 0;
}

static int GEM_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid)
{
    char alert_str[256];
    char safe_msg[180];
    short result;
    
    if (!messageboxdata || !messageboxdata->message) return -1;
    
    SDL_strlcpy(safe_msg, messageboxdata->message, sizeof(safe_msg));
    SDL_snprintf(alert_str, sizeof(alert_str), "[1][%s][OK]", safe_msg);
    
    result = mt_form_alert(1, alert_str, sdl_global_aes);
    if (buttonid) *buttonid = (result == 1) ? 0 : -1;
    
    return 0;
}

VideoBootStrap GEM_bootstrap = {
    "gem", "GEM video driver", GEM_CreateDevice, GEM_ShowMessageBox
};

#endif /* SDL_VIDEO_DRIVER_GEM */