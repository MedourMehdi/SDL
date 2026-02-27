/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* ============================================================================
   SDL_gemvideo.c – GEM video driver core for SDL2
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Responsibilities: device creation, VideoInit/VideoQuit, display mode setup,
   one-time palette LUT initialisation, mouse driver registration, and
   AES message box.

   All expensive queries (VDI format, hardware palette) are performed once
   in VideoInit and cached in SDL_VideoData for zero per-frame overhead.

   C90 compliant.
   ============================================================================ */

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
    
    /* STEP 1: Read hardware palette via vq_color().
     *
     * Why NOT Setcolor(): ST/STe BIOS call, covers only hardware slots 0..15.
     * Why NOT VgetRGB(): Falcon XBIOS only, returns raw hardware values before
     *   the VDI index remapping is applied — wrong colour for slots 0..15.
     *
     * vq_color(handle, vdi_pen, set_flag, rgb[3]) reads by VDI pen index,
     * but our planar buffer (fd_stand=0, device-specific) stores HARDWARE
     * slot indices — vro_cpyfm feeds pixel values directly to the palette
     * hardware, bypassing the VDI pen remapping.
     *
     * The VDI remaps hardware slots 0..15 to different VDI pen numbers.
     * (Slots 16..254 are identity; slot 255 → VDI pen 1.)
     * This is the same mapping used by vdi_index[] in Patrice Mandin's
     * vdi_com.c reference implementation.
     *
     * To get the colour actually displayed at hardware slot hw:
     *   vdi_pen = vdi_index[hw]
     *   vq_color(handle, vdi_pen, 1, rgb)
     *
     * We store colours indexed by hardware slot so the distance-matcher
     * produces hardware slot indices directly into rgb332_to_hw[]. */
    {
        /* Hardware slot → VDI pen mapping (from Atari VDI documentation).
         * Indices 16..254 are identity; 255 → VDI pen 1. */
        static const Uint8 vdi_index[16] = {
            0, 2, 3, 6, 4, 7, 5, 8, 9, 10, 11, 14, 12, 15, 13, 255
        };
        short rgb[3];
        int r4, g4, b4, vdi_pen;
        for (i = 0; i < num_colors; i++) {
            vdi_pen = (i < 16) ? (int)vdi_index[i] : (i == 255 ? 1 : i);
            vq_color(data->vdi_handle, vdi_pen, 1, rgb);
            /* VDI 0..1000 → 4-bit 0..15 */
            r4 = ((int)rgb[0] * 15 + 500) / 1000;
            g4 = ((int)rgb[1] * 15 + 500) / 1000;
            b4 = ((int)rgb[2] * 15 + 500) / 1000;
            if (r4 < 0) r4 = 0; else if (r4 > 15) r4 = 15;
            if (g4 < 0) g4 = 0; else if (g4 > 15) g4 = 15;
            if (b4 < 0) b4 = 0; else if (b4 > 15) b4 = 15;
            /* Store indexed by hardware slot */
            data->hw_palette[i] = (Uint16)((r4 << 8) | (g4 << 4) | b4);
        }
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
    char alert_str[512];
    char safe_msg[200];
    char buttons_str[128];
    short result;
    int num_buttons;
    int icon;
    int i;
    int pos;
    const char *btn_text;
    size_t btn_len;
    size_t remaining;
    
    if (!messageboxdata || !messageboxdata->message) {
        return -1;
    }
    
    /* GEM supports max 3 buttons, clamp to valid range */
    num_buttons = messageboxdata->numbuttons;
    if (num_buttons > 3) {
        num_buttons = 3;
    } else if (num_buttons < 1) {
        num_buttons = 1;
    }
    
    /* Build button string with | separators */
    buttons_str[0] = '\0';
    pos = 0;
    remaining = sizeof(buttons_str) - 1;
    
    for (i = 0; i < num_buttons && remaining > 0; i++) {
        if (i > 0) {
            if (remaining > 1) {
                buttons_str[pos++] = '|';
                buttons_str[pos] = '\0';
                remaining--;
            }
        }
        
        btn_text = messageboxdata->buttons[i].text;
        if (!btn_text) {
            btn_text = "?";
        }
        
        /* Truncate individual button text if needed (max ~20 chars for GEM) */
        btn_len = SDL_strlen(btn_text);
        if (btn_len > 20) {
            btn_len = 20;
        }
        
        if (btn_len > remaining) {
            btn_len = remaining;
        }
        
        if (btn_len > 0) {
            SDL_memcpy(buttons_str + pos, btn_text, btn_len);
            pos += (int)btn_len;
            buttons_str[pos] = '\0';
            remaining -= btn_len;
        }
    }
    
    /* Map SDL message box flags to GEM icon */
    /* 0=none, 1=warning(!), 2=question(?), 3=stop - use 1 as default */
    if (messageboxdata->flags & SDL_MESSAGEBOX_ERROR) {
        icon = 3;  /* stop sign */
    } else if (messageboxdata->flags & SDL_MESSAGEBOX_WARNING) {
        icon = 1;  /* exclamation */
    } else {
        icon = 1;  /* default to warning (info icon=4 is AES 4.10+) */
    }
    
    /* Copy and sanitize message: truncate and replace newlines with | */
    SDL_strlcpy(safe_msg, messageboxdata->message, sizeof(safe_msg));
    
    for (i = 0; safe_msg[i] != '\0'; i++) {
        if (safe_msg[i] == '\n' || safe_msg[i] == '\r') {
            safe_msg[i] = '|';
        }
    }
    
    /* Build final alert string */
    SDL_snprintf(alert_str, sizeof(alert_str), "[%d][%s][%s]", 
                 icon, safe_msg, buttons_str);
    
    /* Default button is 1 (first button) */
    result = mt_form_alert(1, alert_str, sdl_global_aes);
    
    /* Map GEM result (1, 2, 3) back to SDL buttonid */
    if (buttonid) {
        if (result >= 1 && result <= num_buttons) {
            *buttonid = messageboxdata->buttons[result - 1].buttonid;
        } else {
            *buttonid = -1;  /* Error or unexpected result */
        }
    }
    
    return 0;
}

VideoBootStrap GEM_bootstrap = {
    "gem", "GEM video driver", GEM_CreateDevice, GEM_ShowMessageBox
};

#endif /* SDL_VIDEO_DRIVER_GEM */