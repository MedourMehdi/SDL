#include "../../SDL_internal.h"

#if SDL_AUDIO_DRIVER_ATARI

#include "SDL.h"
#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_atariaudio.h"

static int get_cookie(long cookie, unsigned long *val) {
    long tmp;
    int ret = Getcookie(cookie, &tmp);
    if (ret == C_FOUND) {
        *val = (unsigned long)tmp;
    }
    return ret;
}

static int ATARI_OpenDevice(SDL_AudioDevice *device, const char *devname)
{
    struct SDL_PrivateAudioData *hidden;
    int result;

    device->hidden = (struct SDL_PrivateAudioData *)SDL_malloc(sizeof(*device->hidden));
    if (!device->hidden) {
        return SDL_OutOfMemory();
    }
    hidden = device->hidden;
    SDL_memset(hidden, 0, sizeof(*hidden));

    if (get_cookie(C__SND, &hidden->cookie_snd) != C_FOUND) {
        hidden->cookie_snd = SND_PSG;
    }
    
    if (get_cookie(C__MCH, &hidden->cookie_mch) != C_FOUND) {
        hidden->cookie_mch = MCH_ST;
    }

    switch (hidden->cookie_mch & MCH_MASK) {
        case MCH_F30:
        case MCH_ARA:
            result = mint_audio_open(device, &device->spec);
            if (result == 0) {
                mint_audio_start(device);
            }
            return result;
        case MCH_STE:
        case MCH_TT:
            if (hidden->cookie_snd & SND_8BIT) {
                result = mint_audio_open(device, &device->spec);
                if (result == 0) {
                    mint_audio_start(device);
                }
                return result;
            }
            return SDL_SetError("Sound hardware not available");
        default:
            return SDL_SetError("Unsupported audio hardware");
    }
}

static void ATARI_CloseDevice(SDL_AudioDevice *device)
{
    if (device->hidden) {
        mint_audio_stop(device);
        mint_audio_close(device);
        SDL_free(device->hidden);
        device->hidden = NULL;
    }
}

static SDL_bool ATARI_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = ATARI_OpenDevice;
    impl->CloseDevice = ATARI_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->HasCaptureSupport = SDL_FALSE;
    impl->ProvidesOwnCallbackThread = SDL_TRUE;
    
    return SDL_TRUE;
}

AudioBootStrap ATARIAUDIO_bootstrap = {
    "mint_xbios",
    "Atari XBIOS audio driver",
    ATARI_Init,
    SDL_TRUE
};

#endif /* SDL_AUDIO_DRIVER_ATARI */
