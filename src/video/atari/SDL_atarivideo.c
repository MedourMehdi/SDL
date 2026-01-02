/* ============================================
   FILE: src/video/atari/SDL_atarivideo.c
   Atari video driver (delegates to GEM)
   ============================================ */

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "SDL_atarivideo.h"
#include <mint/cookie.h>
#include <mint/osbind.h>

#ifdef SDL_VIDEO_DRIVER_ATARI

#ifdef SDL_VIDEO_DRIVER_GEM
extern SDL_VideoDevice *GEM_CreateDevice(int devindex);
#endif

static int ATARI_Available(void)
{
    long mint_cookie;
    
    /* SDL2 requires FreeMiNT for pthread support */
    if (Getcookie(C_MiNT, &mint_cookie) != C_FOUND) {
        return 0;
    }
    
    return 1;
}

static void ATARI_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *ATARI_CreateDevice(int devindex)
{
#ifdef SDL_VIDEO_DRIVER_GEM
    /* Detect hardware for logging */
    Atari_DetectHW();
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                "Atari hardware: %s, CPU: %d", 
                Atari_GetMachineName(), hw_info.cpu);
    
    /* Delegate to GEM driver */
    return GEM_CreateDevice(devindex);
#else
    SDL_SetError("GEM driver not compiled in");
    return NULL;
#endif
}

VideoBootStrap ATARI_bootstrap = {
    "atari", "SDL Atari video driver",
    ATARI_CreateDevice
};

#endif /* SDL_VIDEO_DRIVER_ATARI */