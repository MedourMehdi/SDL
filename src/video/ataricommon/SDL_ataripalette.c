#include "../atari/SDL_atarivideo.h"
#include "../atari/SDL_atarihw.h"
#include "SDL_atarimodel.h"
#include <mint/osbind.h>

void ATARI_SetPalette(_THIS, SDL_Color *colors, int firstcolor, int ncolors)
{
    int i;
    volatile Uint16 *palette_regs;
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;

    /* Get palette registers based on hardware */
    switch(hw_info.video) {
        case ATARI_VIDEO_F30:
            palette_regs = (Uint16 *)F30_VIDEL_REGS;
            break;
        case ATARI_VIDEO_TT:
            palette_regs = (Uint16 *)TT_SHIFTER_REGS;
            break;
        default:
            palette_regs = (Uint16 *)ST_SHIFTER_REGS;
            break;
    }

    /* Update palette entries */
    for (i = 0; i < ncolors; i++) {
        Uint16 color;
        
        if (hw_info.video == ATARI_VIDEO_F30) {
            /* Falcon uses full 8-bit RGB */
            color = ((colors[i].r & 0xFF) << 16) |
                   ((colors[i].g & 0xFF) << 8) |
                   (colors[i].b & 0xFF);
        } else {
            /* ST/TT use 3-bit RGB */
            color = ((colors[i].r & 0xE0) << 4) |
                   ((colors[i].g & 0xE0) << 0) |
                   ((colors[i].b & 0xE0) >> 4);
        }
        
        palette_regs[firstcolor + i] = color;
    }
}
