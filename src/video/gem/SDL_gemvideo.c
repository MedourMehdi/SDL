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
#include <mint/falcon.h>

#ifdef SDL_VIDEO_DRIVER_GEM

static unsigned char vdi_index[256] = {
    0,  2,  3,  6,  4,  7,  5,  8,
    9, 10, 11, 14, 12, 15, 13,  1
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

static void InitPaletteLUT(SDL_VideoData *data, const int16_t *screen_info)
{
    int i, p;
    int num_colors;
    int r3, g3, b2;
    int r4, g4, b4;
    int pr, pg, pb;
    int dr, dg, db, dist;
    int best, min_dist;
    short rgb[3];

    num_colors = 1 << data->planes;
    if (num_colors > 256) num_colors = 256;

    fprintf(stderr, "InitPaletteLUT: called, planes=%d, clut_type=%d\r\n",
            data->planes, (int)screen_info[1]);

    /* ============================================================
       STEP 1: Read the actual hardware palette
       ============================================================ */
    if (screen_info[1] == VDI_CLUT_HARDWARE) {
        /* vq_color reads hardware slots directly */
        for (i = 0; i < num_colors; i++) {
            vq_color(data->vdi_handle, (short)i, 1, rgb);

            r4 = ((int)rgb[0] * 15 + 500) / 1000;
            g4 = ((int)rgb[1] * 15 + 500) / 1000;
            b4 = ((int)rgb[2] * 15 + 500) / 1000;
            if (r4 < 0) r4 = 0; else if (r4 > 15) r4 = 15;
            if (g4 < 0) g4 = 0; else if (g4 > 15) g4 = 15;
            if (b4 < 0) b4 = 0; else if (b4 > 15) b4 = 15;

            data->hw_palette[i] = (Uint16)((r4 << 8) | (g4 << 4) | b4);
        }
    } else {
        /* VDI_CLUT_SOFTWARE (NVDI/fVDI) or VDI_CLUT_NONE:
           vq_color is useless. Read hardware registers directly. */
        fprintf(stderr, "Hw Type: %s, using direct register reads for palette\r\n",
                Atari_GetMachineName());
        switch (hw_info.hw_type) {
            case ATARI_HW_ST:
            case ATARI_HW_STE:
            case ATARI_HW_MILAN: {
                /* ST/STE/Milan: $FFFF8240 palette registers.
                   ST only has 16 registers; STE/Milan may have more
                   but Setcolor is safe for the first 16. For 8-bit
                   Milan modes you may need direct register reads. */
                int max_regs = (hw_info.hw_type == ATARI_HW_ST) ? 16 : num_colors;
                if (max_regs > num_colors) max_regs = num_colors;

                for (i = 0; i < max_regs; i++) {
                    short reg = Setcolor(i, -1);

                    if (hw_info.hw_type == ATARI_HW_STE ||
                        hw_info.hw_type == ATARI_HW_MILAN) {
                        /* STE format: xxxx R0 R3 R2 R1  G0 G3 G2 G1  B0 B3 B2 B1
                           Reassemble nibbles: R3 R2 R1 R0 */
                        r4 = (((reg >> 8) & 7) << 1) | ((reg >> 11) & 1);
                        g4 = (((reg >> 4) & 7) << 1) | ((reg >> 7) & 1);
                        b4 = (( reg       & 7) << 1) | ((reg >> 3) & 1);
                    } else {
                        /* ST format: xxxx x R2 R1 R0 x G2 G1 G0 x B2 B1 B0
                           3-bit to 4-bit: multiply by 2 */
                        r4 = ((reg >> 8) & 7) * 2;
                        g4 = ((reg >> 4) & 7) * 2;
                        b4 = ( reg       & 7) * 2;
                    }
                    data->hw_palette[i] = (Uint16)((r4 << 8) | (g4 << 4) | b4);
                }
                /* If num_colors > max_regs (e.g. ST with num_colors=16 is fine,
                   but if somehow num_colors > 16 on ST), fill remainder with 0 */
                for (; i < num_colors; i++) {
                    data->hw_palette[i] = 0;
                }
                break;
            }

            case ATARI_HW_TT: {
                /* TT: $FFFF8400 palette, 256 words of 0x0RGB */
                short tt_pal[256];
                int count = (num_colors < 256) ? num_colors : 256;

                EgetPalette(0, count, tt_pal);
                for (i = 0; i < count; i++) {
                    r4 = (tt_pal[i] >> 8) & 0x0F;
                    g4 = (tt_pal[i] >> 4) & 0x0F;
                    b4 =  tt_pal[i]       & 0x0F;
                    data->hw_palette[i] = (Uint16)((r4 << 8) | (g4 << 4) | b4);
                }
                for (; i < num_colors; i++) {
                    data->hw_palette[i] = 0;
                }
                break;
            }

            case ATARI_HW_HADES:
            case ATARI_HW_F30: {
                /* Falcon: $FFFF9800 palette, 256 longs of 0x00RRGGBB */
                long falcon_pal[256];
                int count = (num_colors < 256) ? num_colors : 256;

                VgetRGB(0, count, falcon_pal);
                for (i = 0; i < count; i++) {
                    int r8 = (int)((falcon_pal[i] >> 16) & 0xFF);
                    int g8 = (int)((falcon_pal[i] >>  8) & 0xFF);
                    int b8 = (int)( falcon_pal[i]        & 0xFF);

                    r4 = (r8 * 15 + 127) / 255;  /* round, don't truncate */
                    g4 = (g8 * 15 + 127) / 255;
                    b4 = (b8 * 15 + 127) / 255;
                    data->hw_palette[i] = (Uint16)((r4 << 8) | (g4 << 4) | b4);
                }
                for (; i < num_colors; i++) {
                    data->hw_palette[i] = 0;
                }
                break;
            }

            default: {
                /* Hades, Nova, Imagine, or unknown expansion cards.
                   No standard Atari palette registers. We cannot read
                   the hardware CLT. Force non-identity and build a
                   best-effort grayscale ramp so the app at least runs. */
                fprintf(stderr,
                    "GEM: Unknown hardware palette for %s, using grayscale fallback\r\n",
                    Atari_GetMachineName());

                for (i = 0; i < num_colors; i++) {
                    int v = (i * 15) / (num_colors - 1);
                    data->hw_palette[i] = (Uint16)((v << 8) | (v << 4) | v);
                }
                break;
            }
        }
    }

    /* ============================================================
       STEP 2: Identity check against RGB332 ideal
       ============================================================ */
    data->use_identity_palette = 1;
    for (i = 0; i < num_colors; i++) {
        r3 = (i >> 5) & 0x07;
        g3 = (i >> 2) & 0x07;
        b2 =  i       & 0x03;

        r4 = (r3 * 15 + 3) / 7;
        g4 = (g3 * 15 + 3) / 7;
        b4 = (b2 * 15 + 1) / 3;

        if (data->hw_palette[i] != (Uint16)((r4 << 8) | (g4 << 4) | b4)) {
            data->use_identity_palette = 0;
            fprintf(stderr,
                "GEM: Palette mismatch at slot %d (hw=0x%04X expected=0x%04X)\r\n",
                i, data->hw_palette[i],
                (Uint16)((r4 << 8) | (g4 << 4) | b4));
            break;
        }
    }

    fprintf(stderr, "hw_palette[0]=0x%04X [1]=0x%04X [15]=0x%04X\r\n",
        data->hw_palette[0], data->hw_palette[1], data->hw_palette[15]);

    /* ============================================================
       STEP 3: Build LUT
       ============================================================ */
    if (data->use_identity_palette) {
        fprintf(stderr, "GEM: Using IDENTITY palette\r\n");
        for (i = 0; i < 256; i++) {
            data->rgb332_to_hw[i] = (Uint8)i;
        }
    } else {
        fprintf(stderr, "GEM: Using distance-matched LUT\r\n");
        for (i = 0; i < 256; i++) {
            r3 = (i >> 5) & 0x07;
            g3 = (i >> 2) & 0x07;
            b2 =  i       & 0x03;

            r4 = (r3 * 15 + 3) / 7;
            g4 = (g3 * 15 + 3) / 7;
            b4 = (b2 * 15 + 1) / 3;

            best = 0;
            min_dist = 0x7FFF;

            for (p = 0; p < num_colors; p++) {
                pr = (int)((data->hw_palette[p] >> 8) & 0x0F);
                pg = (int)((data->hw_palette[p] >> 4) & 0x0F);
                pb = (int)( data->hw_palette[p]        & 0x0F);

                dr = r4 - pr;  dg = g4 - pg;  db = b4 - pb;
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

    fprintf(stderr,
        "GEM: Black(0x00)->%d White(0xFF)->%d Red(0xE0)->%d "
        "Green(0x1C)->%d Blue(0x03)->%d\r\n",
        data->rgb332_to_hw[0x00], data->rgb332_to_hw[0xFF],
        data->rgb332_to_hw[0xE0], data->rgb332_to_hw[0x1C],
        data->rgb332_to_hw[0x03]);

    data->palette_initialized = 1;
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
    if (gl_apid < 0) return SDL_SetError("AES not initialized");

    Atari_DetectHW();
    // fprintf(stderr, "GEM: Hardware: %s (CPU %d)\r\n",
    //             Atari_GetMachineName(), hw_info.cpu);

    mt_wind_get_grect(DESK, WF_WORKXYWH, (GRECT *)&data->work_x, sdl_global_aes);
    mt_wind_get_grect(DESK, WF_CURRXYWH, (GRECT *)&data->desk_x, sdl_global_aes);

    data->vdi_handle = mt_graf_handle(NULL, NULL, NULL, NULL, sdl_global_aes);
    if (data->vdi_handle < 1) return SDL_SetError("Can't get VDI handle");

    for (i = 0; i < 10; i++) work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &data->vdi_handle, work_out);
    if (data->vdi_handle == 0) return SDL_SetError("Can't open VDI workstation");

    vq_extnd(data->vdi_handle, 1, work_out);
    data->planes = work_out[4];

    /* Complete vdi_index[] for slots 16-255 */
    for (i = 16; i < 255; i++) vdi_index[i] = (unsigned char)i;
    vdi_index[255] = (hw_info.vdi_type == ATARI_VDI_FVDI) ? 255 : 1;
    // vdi_index[255] = 1;
    // fprintf(stderr, "GEM: VDI type=%s  vdi_index[255]=%d\r\n",
    //             (hw_info.vdi_type == ATARI_VDI_FVDI) ? "fVDI" :
    //             (hw_info.vdi_type == ATARI_VDI_NVDI) ? "NVDI" :
    //             (hw_info.vdi_type == ATARI_VDI_GDOS) ? "GDOS" : "ROM",
    //             (int)vdi_index[255]);
    // fprintf(stderr, "GEM_VideoInit: planes=%d\r\n", data->planes);

    vq_scrninfo(data->vdi_handle, screen_info);
    data->vdi_pixel_format   = screen_info[0];
    data->vdi_nb_of_colors = screen_info[4];

    /* Rebuild vdi_index[] from the driver's authoritative CLUT table,
     * exactly as SDL 1.2 does. Overrides all static defaults. */
    if (screen_info[1] == VDI_CLUT_HARDWARE) {
        Uint16 *tmp_p = (Uint16 *)&screen_info[16];
        for (i = 0; i < 256; i++) {
            vdi_index[*tmp_p++] = (unsigned char)i;
        }
        // fprintf(stderr,
        //             "GEM: vdi_index[] loaded from vq_scrninfo CLUT table");
    }

    // fprintf(stderr, "vq_scrninfo: screen_info[1]=%d\r\n", (int)screen_info[1]);
    // fprintf(stderr, "GEM: VDI format=%d bpp=%d planes=%d",
    //             data->vdi_pixel_format, data->vdi_nb_of_colors, data->planes);

    if (data->planes <= 8) {
        InitPaletteLUT(data, screen_info);   /* pass screen_info */
    }
    /* Display mode */
    SDL_zero(mode);

    switch (data->planes) {
        case 1: case 2: case 4: case 8:
            mode.format = SDL_PIXELFORMAT_RGB332; break;
        case 16: mode.format = SDL_PIXELFORMAT_RGB565; break;
        case 24: mode.format = SDL_PIXELFORMAT_BGR24;  break;
        default: mode.format = SDL_PIXELFORMAT_ARGB8888; break;
    }    
    // printf("GEM: Display mode format=%s, SDL_BYTEORDER == %s\r\n", SDL_GetPixelFormatName(mode.format), SDL_BYTEORDER == SDL_LIL_ENDIAN ? "LITTLE_ENDIAN" : "BIG_ENDIAN");
    mode.w = data->work_w; mode.h = data->work_h; mode.refresh_rate = 50;

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