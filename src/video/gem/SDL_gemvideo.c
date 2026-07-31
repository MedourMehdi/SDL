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
   SDL_gemvideo.c - GEM video driver core for SDL2
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Responsibilities: device creation, VideoInit/VideoQuit, display mode setup,
   one-time palette LUT initialisation, mouse driver registration, and
   AES message box.

   All expensive queries (VDI format, hardware palette) are performed once
   in VideoInit and cached in SDL_VideoData for zero per-frame overhead.

   Palette approach (identical to SDL 1.2 / Patrice Mandin):
     vdi_index[hw_slot] = VDI pen number for that hardware slot.
     vs_color(handle, vdi_index[i], rgb) writes hardware slot i.
     vq_color(handle, vdi_index[i], 1, rgb) reads hardware slot i.
     No Setcolor(), no VsetRGB(), no VgetRGB() needed.

   C90 compliant.
   ============================================================================ */

#include "SDL_gemvideo.h"
#include "SDL_gemmodel.h"
#include "SDL_gemkeys.h"
#include <mint/osbind.h>
#include <mt_gemx.h>

#ifdef SDL_VIDEO_DRIVER_GEM

/* ============================================================
   Global hardware slot -> VDI pen mapping.
   Slots  0-15 : fixed GEM mapping (from Atari VDI documentation,
                 same table used by Patrice Mandin in SDL 1.2).
   Slots 16-254: identity (pen == slot).
   Slot    255 : VDI pen 1 (GEM always maps pen 1 to hw slot 255).
   Filled completely in GEM_VideoInit before any palette call.
   ============================================================ */
static unsigned char vdi_index[256] = {
    0,  2,  3,  6,  4,  7,  5,  8,
    9, 10, 11, 14, 12, 15, 13, 255
    /* slots 16-255 filled at runtime in GEM_VideoInit */
};

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

static void GEM_WarpMouse(SDL_Window *window, int x, int y)
{
    (void)window; (void)x; (void)y;
}

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
        mouse->WarpMouse          = GEM_WarpMouse;
        mouse->GetGlobalMouseState = GEM_GetGlobalMouseState;
        mouse->ShowCursor         = GEM_ShowCursor;
        mouse->SetRelativeMouseMode = GEM_SetRelativeMouseMode;
        mouse->CaptureMouse       = GEM_CaptureMouse;
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

    device->driverdata    = data;
    device->num_displays  = 0;

    device->VideoInit               = GEM_VideoInit;
    device->VideoQuit               = GEM_VideoQuit;
    device->SetDisplayMode          = GEM_SetDisplayMode;
    device->CreateSDLWindow         = GEM_CreateWindow;
    device->GetWindowDisplayIndex   = GEM_GetWindowDisplayIndex;
    device->SetWindowTitle          = GEM_SetWindowTitle;
    device->CreateWindowFramebuffer = GEM_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = GEM_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer= GEM_DestroyWindowFramebuffer;
    device->DestroyWindow           = GEM_DestroyWindow;
    device->free                    = GEM_DeleteDevice;

    device->SetWindowPosition       = GEM_SetWindowPosition;
    device->ShowWindow              = GEM_ShowWindow;
    device->HideWindow              = GEM_HideWindow;
    device->RaiseWindow             = GEM_RaiseWindow;
    device->MaximizeWindow          = GEM_MaximizeWindow;
    device->MinimizeWindow          = GEM_MinimizeWindow;
    device->RestoreWindow           = GEM_RestoreWindow;
    device->SetWindowBordered       = GEM_SetWindowBordered;
    device->SetWindowResizable      = GEM_SetWindowResizable;
    device->SetWindowSize           = GEM_SetWindowSize;
    device->SetWindowMinimumSize    = GEM_SetWindowMinimumSize;
    device->SetWindowMaximumSize    = GEM_SetWindowMaximumSize;
    device->SetWindowFullscreen     = GEM_SetWindowFullscreen;

    device->PumpEvents = GEM_PumpEvents;

    GEM_RegisterMouseDriver();

    return device;
}

/* ============================================================
   ONE-TIME PALETTE LUT INITIALISATION
   Called once in VideoInit after the palette has been programmed.
   Reads the hardware palette via vdi_index[] + vq_color() and
   builds rgb332_to_hw[]: maps each RGB332 value to the nearest
   hardware palette slot index.

   Uses the same vdi_index[] approach as SDL 1.2 (Patrice Mandin):
     vq_color(handle, vdi_index[hw_slot], 1, rgb)
   This gives the colour actually stored in hardware slot hw_slot,
   bypassing the VDI pen remapping that would occur if we passed
   hw_slot directly.
   ============================================================ */
static void InitPaletteLUT(SDL_VideoData *data)
{
    int i, p;
    int num_colors;
    int r, g, b;
    int pr, pg, pb;
    int dr, dg, db, dist;
    int best, min_dist;
    Uint16 expected;
    short rgb[3];
    int r4, g4, b4;

    num_colors = 1 << data->planes;
    if (num_colors > 256) num_colors = 256;

    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO,
                "GEM: Building palette LUT for %d colors (ONE-TIME)", num_colors);

    /* -------------------------------------------------------
       STEP 1: Read hardware palette.
       vq_color(handle, vdi_index[i], 1, rgb) reads the colour
       at hardware slot i by going through the correct VDI pen.
       Store as 4-bit per channel in hw_palette[i].
       ------------------------------------------------------- */
    for (i = 0; i < num_colors; i++) {
        vq_color(data->vdi_handle, (short)vdi_index[i], 1, rgb);

        r4 = ((int)rgb[0] * 15 + 500) / 1000;
        g4 = ((int)rgb[1] * 15 + 500) / 1000;
        b4 = ((int)rgb[2] * 15 + 500) / 1000;
        if (r4 < 0) r4 = 0; else if (r4 > 15) r4 = 15;
        if (g4 < 0) g4 = 0; else if (g4 > 15) g4 = 15;
        if (b4 < 0) b4 = 0; else if (b4 > 15) b4 = 15;

        data->hw_palette[i] = (Uint16)((r4 << 8) | (g4 << 4) | b4);
    }

    /* -------------------------------------------------------
       STEP 2: Check for identity palette.
       If we just programmed the palette to RGB332 layout, every
       hw_palette[i] should exactly match the RGB332 colour for
       index i.  If so we use the fast identity LUT path.

       Expected encoding per slot i:
         R3 = (i>>5)&7  -> 4-bit: R3<<1      (0,2,4,6,8,A,C,E)
         G3 = (i>>2)&7  -> 4-bit: G3<<1
         B2 = i&3       -> 4-bit: B2*5       (0,5,A,F)
       ------------------------------------------------------- */
    data->use_identity_palette = 1;
    for (i = 0; i < num_colors; i++) {
        r = ((i >> 5) & 0x07) << 1;
        g = ((i >> 2) & 0x07) << 1;
        b =  (i & 0x03) * 5;

        expected = (Uint16)((r << 8) | (g << 4) | b);

        if (data->hw_palette[i] != expected) {
            data->use_identity_palette = 0;
            SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO,
                        "GEM: Palette mismatch at %d (hw=0x%04X, expected=0x%04X)",
                        i, data->hw_palette[i], expected);
            break;
        }
    }

    /* -------------------------------------------------------
       STEP 3: Build rgb332_to_hw[] LUT.
       ------------------------------------------------------- */
    if (data->use_identity_palette) {
        SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "GEM: Using IDENTITY palette (fast path)");
        for (i = 0; i < 256; i++) {
            data->rgb332_to_hw[i] = (Uint8)i;
        }
    } else {
        SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "GEM: Using distance-matched LUT");
        for (i = 0; i < 256; i++) {
            r = ((i >> 5) & 0x07) << 1;
            g = ((i >> 2) & 0x07) << 1;
            b =  (i & 0x03) * 5;

            best     = 0;
            min_dist = 0x7FFF;

            for (p = 0; p < num_colors; p++) {
                pr = (int)((data->hw_palette[p] >> 8) & 0x0F);
                pg = (int)((data->hw_palette[p] >> 4) & 0x0F);
                pb = (int)( data->hw_palette[p]        & 0x0F);

                dr = r - pr;
                dg = g - pg;
                db = b - pb;
                dist = dr*dr + dg*dg + db*db;

                if (dist < min_dist) {
                    min_dist = dist;
                    best = p;
                    if (dist == 0) break;
                }
            }

            data->rgb332_to_hw[i] = (Uint8)best;
        }
    }

    data->palette_initialized = 1;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                 "GEM: Black(0x00)->%d White(0xFF)->%d Red(0xE0)->%d "
                 "Green(0x1C)->%d Blue(0x03)->%d",
                 (int)data->rgb332_to_hw[0x00],
                 (int)data->rgb332_to_hw[0xFF],
                 (int)data->rgb332_to_hw[0xE0],
                 (int)data->rgb332_to_hw[0x1C],
                 (int)data->rgb332_to_hw[0x03]);
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

    /* -------------------------------------------------------
       Complete the vdi_index[] table.
       Slots 0-15 are already set by the static initialiser.
       Slots 16-254: identity mapping (pen == slot).
       Slot    255 : depends on VDI implementation:
         - NVDI (and ROM VDI / GDOS): replicates TOS quirk where
           hardware slot 255 is tied to VDI pen 1 (foreground/black).
           Must use pen 1 to read or write slot 255.
         - fVDI: strict 1:1 identity for all extended palette slots,
           so pen 255 accesses hardware slot 255 directly.
       Must be done before ANY palette read or write call.
       ------------------------------------------------------- */
    for (i = 16; i < 255; i++) {
        vdi_index[i] = (unsigned char)i;
    }
    vdi_index[255] = (hw_info.vdi_type == ATARI_VDI_FVDI) ? 255 : 1;

    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO,
                "GEM: VDI type=%s  vdi_index[255]=%d",
                (hw_info.vdi_type == ATARI_VDI_FVDI) ? "fVDI" :
                (hw_info.vdi_type == ATARI_VDI_NVDI) ? "NVDI" :
                (hw_info.vdi_type == ATARI_VDI_GDOS) ? "GDOS" : "ROM",
                (int)vdi_index[255]);

    /* -------------------------------------------------------
       VDI FORMAT QUERY
       ------------------------------------------------------- */
    vq_scrninfo(data->vdi_handle, screen_info);
    data->vdi_pixel_format   = screen_info[0];
    data->vdi_bits_per_pixel = screen_info[4];

    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO,
                "GEM: VDI format=%d bpp=%d planes=%d",
                data->vdi_pixel_format,
                data->vdi_bits_per_pixel,
                data->planes);

    /* -------------------------------------------------------
       PALETTE INITIALISATION (8bpp and below only)

       Step A – Save current palette so VideoQuit can restore it.
                vq_color(handle, vdi_index[i], 1, rgb) reads the
                colour at hardware slot i correctly.

       Step B – Program palette to exact RGB332 layout so that
                hardware slot i displays the RGB332 colour for
                index i.  After this, InitPaletteLUT will find
                a perfect identity match.

                B2 VDI blue scaling uses exact boundary values
                so the readback rounds correctly:
                  B2=0 -> VDI 0    -> 4-bit 0  (= 0*5)
                  B2=1 -> VDI 333  -> 4-bit 5  (= 1*5) *
                  B2=2 -> VDI 667  -> 4-bit 10 (= 2*5) *
                  B2=3 -> VDI 1000 -> 4-bit 15 (= 3*5)
                (* 333*15/1000 = 4.995 -> rounds to 5 with +500 bias)

       Step C – Build the LUT (will always be identity after B).
       ------------------------------------------------------- */
    if (data->planes <= 8) {
        short rgb[3];
        int n_colors = 1 << data->planes;
        if (n_colors > 256) n_colors = 256;

        data->saved_palette_count = n_colors;

        /* --- Step A: Save current palette ---
         * Use set_flag=0 (read current value, not default) and raw
         * pen index i — exactly as SDL 1.2 GEM_CommonSavePalette does.
         * vdi_index[] is only needed for the program/restore steps. */
        for (i = 0; i < n_colors; i++) {
            int r4, g4, b4;
            vq_color(data->vdi_handle, (short)i, 0, rgb);
            r4 = ((int)rgb[0] * 15 + 500) / 1000;
            g4 = ((int)rgb[1] * 15 + 500) / 1000;
            b4 = ((int)rgb[2] * 15 + 500) / 1000;
            if (r4 < 0) r4 = 0; else if (r4 > 15) r4 = 15;
            if (g4 < 0) g4 = 0; else if (g4 > 15) g4 = 15;
            if (b4 < 0) b4 = 0; else if (b4 > 15) b4 = 15;
            data->saved_palette[i] = (Uint16)((r4 << 8) | (g4 << 4) | b4);
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Saved %d palette slots", n_colors);

        /* --- Step B: Program RGB332 layout --- */
        {
            /* Exact VDI 0-1000 values for each 2-bit blue component.
             * Using integer division alone (b2*1000/3) gives 333 for b2=1
             * which rounds correctly to 4-bit 5 with the +500 bias in
             * InitPaletteLUT, so the standard formula is fine here. */
            for (i = 0; i < n_colors; i++) {
                int r3 = (i >> 5) & 0x07;
                int g3 = (i >> 2) & 0x07;
                int b2 =  i       & 0x03;
                rgb[0] = (short)(r3 * 1000 / 7);
                rgb[1] = (short)(g3 * 1000 / 7);
                rgb[2] = (short)(b2 * 1000 / 3);
                vs_color(data->vdi_handle, (short)vdi_index[i], rgb);
            }
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Programmed %d palette slots to RGB332 layout", n_colors);

        /* --- Step C: Build LUT --- */
        InitPaletteLUT(data);
    }

    /* Setup display mode */
    SDL_zero(mode);

    switch (data->planes) {
        case 1:
        case 2:
        case 4:
        case 8:  mode.format = SDL_PIXELFORMAT_RGB332;  break;
        case 16: mode.format = SDL_PIXELFORMAT_RGB565;  break;
        case 24: mode.format = SDL_PIXELFORMAT_BGR24;  break;
        case 32: mode.format = SDL_PIXELFORMAT_ARGB8888; break;
        default: mode.format = SDL_PIXELFORMAT_RGB565;  break;
    }

    mode.w            = data->work_w;
    mode.h            = data->work_h;
    mode.refresh_rate = 50;

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
    int i;

    data = (SDL_VideoData *)this->driverdata;
    if (!data) return;

    GEM_QuitEvents(this);

    /* -------------------------------------------------------
       Restore the palette saved in VideoInit.
       Use raw pen index i with vs_color — exactly as SDL 1.2
       GEM_CommonRestorePalette does — because the save used
       vq_color(handle, i, 0, rgb) with the same raw pen index.
       ------------------------------------------------------- */
    if (data->vdi_handle && data->saved_palette_count > 0) {
        short rgb[3];
        for (i = 0; i < data->saved_palette_count; i++) {
            rgb[0] = (short)(((data->saved_palette[i] >> 8) & 0x0F) * 1000 / 15);
            rgb[1] = (short)(((data->saved_palette[i] >> 4) & 0x0F) * 1000 / 15);
            rgb[2] = (short)(( data->saved_palette[i]       & 0x0F) * 1000 / 15);
            vs_color(data->vdi_handle, (short)i, rgb);
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Restored %d palette slots", data->saved_palette_count);
        data->saved_palette_count = 0;
    }

    if (data->vdi_handle) {
        v_clsvwk(data->vdi_handle);
        data->vdi_handle = 0;
    }
}

int GEM_SetDisplayMode(SDL_VideoDevice *this, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    (void)this; (void)display; (void)mode;
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

    num_buttons = messageboxdata->numbuttons;
    if (num_buttons > 3) num_buttons = 3;
    else if (num_buttons < 1) num_buttons = 1;

    buttons_str[0] = '\0';
    pos = 0;
    remaining = sizeof(buttons_str) - 1;

    for (i = 0; i < num_buttons && remaining > 0; i++) {
        if (i > 0 && remaining > 1) {
            buttons_str[pos++] = '|';
            buttons_str[pos]   = '\0';
            remaining--;
        }

        btn_text = messageboxdata->buttons[i].text;
        if (!btn_text) btn_text = "?";

        btn_len = SDL_strlen(btn_text);
        if (btn_len > 20)        btn_len = 20;
        if (btn_len > remaining) btn_len = remaining;

        if (btn_len > 0) {
            SDL_memcpy(buttons_str + pos, btn_text, btn_len);
            pos += (int)btn_len;
            buttons_str[pos] = '\0';
            remaining -= btn_len;
        }
    }

    if (messageboxdata->flags & SDL_MESSAGEBOX_ERROR) {
        icon = 3;
    } else if (messageboxdata->flags & SDL_MESSAGEBOX_WARNING) {
        icon = 1;
    } else {
        icon = 1;
    }

    SDL_strlcpy(safe_msg, messageboxdata->message, sizeof(safe_msg));
    for (i = 0; safe_msg[i] != '\0'; i++) {
        if (safe_msg[i] == '\n' || safe_msg[i] == '\r') {
            safe_msg[i] = '|';
        }
    }

    SDL_snprintf(alert_str, sizeof(alert_str), "[%d][%s][%s]",
                 icon, safe_msg, buttons_str);

    result = mt_form_alert(1, alert_str, sdl_global_aes);

    if (buttonid) {
        if (result >= 1 && result <= num_buttons) {
            *buttonid = messageboxdata->buttons[result - 1].buttonid;
        } else {
            *buttonid = -1;
        }
    }

    return 0;
}

VideoBootStrap GEM_bootstrap = {
    "gem", "GEM video driver", GEM_CreateDevice, GEM_ShowMessageBox
};

#endif /* SDL_VIDEO_DRIVER_GEM */