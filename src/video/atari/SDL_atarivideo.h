#ifndef SDL_atarivideo_h_
#define SDL_atarivideo_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

/* Video driver data structure */
typedef struct SDL_VideoData {
    void *screen_base;        /* Base address of video memory */
    int screen_width;         /* Screen width */
    int screen_height;        /* Screen height */
    int screen_pitch;         /* Bytes per screen line */
    int screen_bpp;          /* Bits per pixel */
    SDL_bool use_gem;        /* Using GEM or direct hardware */
} SDL_VideoData;

/* Function prototypes are now private to the implementation */
int ATARI_InitModes(_THIS);

#endif /* SDL_atarivideo_h_ */
