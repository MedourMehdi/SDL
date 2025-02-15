#include "SDL_atarihw.h"
#include "SDL_atarivideo.h"
#include "../ataricommon/SDL_atarimodel.h"
#include <mint/osbind.h>
#include <mint/sysbind.h>
#include <mint/falcon.h>
#include <mint/sysvars.h>

#include "SDL_endian.h"

/* Falcon video mode constants */
#define CMD_SETMODE      4
#define MODE_DUALPACKED  0x0080
#define MODE_256COLOR    0x0010

// /* Example of handling 16-bit color conversion */
// static Uint16 ATARI_SwapColor(Uint16 x)
// {
// #if SDL_BYTEORDER == SDL_LIL_ENDIAN
//     return SDL_Swap16(x);
// #else
//     return x;
// #endif
// }

/* Original video mode storage */
static Uint16 original_resolution;
static Uint16 original_palette[256];
static void *original_screen;
static Uint16 original_pitch;
static Uint8 original_nplanes;
static SDL_bool mode_saved = SDL_FALSE;

int ATARI_InitHardware(_THIS)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    
    /* Save current video mode if not already done */
    if (!mode_saved) {
        ATARI_SaveVideoMode(_this);
    }

    /* Initialize video memory pointer */
    data->screen_base = Physbase();
    data->screen_pitch = Getrez() == 2 ? 80 : 160;
    
    return 0;
}

void ATARI_QuitHardware(_THIS)
{
    /* Restore original video mode */
    ATARI_RestoreVideoMode(_this);
}

void ATARI_SaveVideoMode(_THIS)
{
    int i;
    volatile Uint16 *palette_regs;

    /* Save current resolution */
    original_resolution = Getrez();
    original_screen = Physbase();
    original_pitch = original_resolution == 2 ? 80 : 160;
    original_nplanes = original_resolution == 0 ? 4 : (original_resolution == 1 ? 2 : 1);

    /* Save palette */
    switch(hw_info.video) {
        case ATARI_VIDEO_F30:
            palette_regs = (Uint16 *)F30_VIDEL_REGS;
            for (i = 0; i < 256; i++) {
                original_palette[i] = palette_regs[i];
            }
            break;
        case ATARI_VIDEO_TT:
            palette_regs = (Uint16 *)TT_SHIFTER_REGS;
            for (i = 0; i < 256; i++) {
                original_palette[i] = palette_regs[i];
            }
            break;
        default:
            palette_regs = (Uint16 *)ST_SHIFTER_REGS;
            for (i = 0; i < 16; i++) {
                original_palette[i] = palette_regs[i];
            }
            break;
    }

    mode_saved = SDL_TRUE;
}

void ATARI_RestoreVideoMode(_THIS)
{
    int i;
    volatile Uint16 *palette_regs;

    if (!mode_saved) {
        return;
    }

    /* Restore screen address */
    Setscreen(original_screen, original_screen, original_resolution);

    /* Restore palette */
    switch(hw_info.video) {
        case ATARI_VIDEO_F30:
            palette_regs = (Uint16 *)F30_VIDEL_REGS;
            for (i = 0; i < 256; i++) {
                palette_regs[i] = original_palette[i];
            }
            break;
        case ATARI_VIDEO_TT:
            palette_regs = (Uint16 *)TT_SHIFTER_REGS;
            for (i = 0; i < 256; i++) {
                palette_regs[i] = original_palette[i];
            }
            break;
        default:
            palette_regs = (Uint16 *)ST_SHIFTER_REGS;
            for (i = 0; i < 16; i++) {
                palette_regs[i] = original_palette[i];
            }
            break;
    }

    mode_saved = SDL_FALSE;
}

int ATARI_SetVideoMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    int width = mode->w;
    int height = mode->h;
    int bpp = SDL_BYTESPERPIXEL(mode->format);
    Uint16 new_mode = 0;

    /* Determine video mode based on resolution and hardware */
    switch(hw_info.video) {
        case ATARI_VIDEO_F30:
            /* Falcon can handle arbitrary resolutions */
            if (width > F30_MAX_WIDTH || height > F30_MAX_HEIGHT) {
                return SDL_SetError("Resolution too high for Falcon030");
            }
            new_mode = F30_VIDEL;
            if (bpp == 2) {
                /* 16-bit direct color mode */
                VsetScreen(-1, mode->format, BPS16 | COL80 | OVERSCAN, CMD_SETMODE);
            } else {
                /* 8-bit palette mode */
                VsetScreen(-1, mode->format, BPS8 | COL80 | OVERSCAN, CMD_SETMODE);
            }
            break;

        case ATARI_VIDEO_TT:
            if (width == TT_LOW_RES_WIDTH && height == TT_LOW_RES_HEIGHT) {
                new_mode = TT_LOW;
            } else {
                return SDL_SetError("Unsupported resolution for TT");
            }
            break;

        default: /* ST/STE */
            if (width == ST_LOW_RES_WIDTH && height == ST_LOW_RES_HEIGHT) {
                new_mode = ST_LOW;
            } else if (width == ST_MED_RES_WIDTH && height == ST_MED_RES_HEIGHT) {
                new_mode = ST_MEDIUM;
            } else if (width == ST_HIGH_RES_WIDTH && height == ST_HIGH_RES_HEIGHT) {
                new_mode = ST_HIGH;
            } else {
                return SDL_SetError("Unsupported resolution for ST/STE");
            }
            break;
    }

    /* Set new video mode */
    Setscreen(-1, -1, new_mode >> 8);
    
    /* Update screen info */
    data->screen_base = Physbase();
    data->screen_pitch = width * bpp;
    data->screen_width = width;
    data->screen_height = height;
    data->screen_bpp = bpp * 8;

    return 0;
}
