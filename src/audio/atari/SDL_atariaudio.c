#include "../../SDL_internal.h"

#if SDL_AUDIO_DRIVER_MINT

#include "SDL.h"
#include "SDL_audio.h"
#include "SDL_error.h"
#include "SDL_timer.h"
#include "../SDL_audio_c.h"
#include "SDL_atariaudio.h"

/* Fix Getcookie signedness warning */
static int get_cookie(long cookie, unsigned long *val) {
    long tmp;
    int ret = Getcookie(cookie, &tmp);
    if (ret == C_FOUND) {
        *val = (unsigned long)tmp;
    }
    return ret;
}

static void MINT_DetectDevices(void)
{
    SDL_AddAudioDevice(SDL_FALSE, "Atari Audio", NULL, (void *)((intptr_t)0));
}

static int MINT_OpenDevice(SDL_AudioDevice *device, const char *devname)
{
    SDL_AudioSpec *spec = &device->spec;
    struct SDL_PrivateAudioData *hidden;

    /* Initialize private data */
    device->hidden = (struct SDL_PrivateAudioData *)SDL_malloc(sizeof(*device->hidden));
    if (device->hidden == NULL) {
        return SDL_OutOfMemory();
    }
    hidden = device->hidden;
    SDL_memset(hidden, 0, sizeof(*hidden));

    /* Get audio hardware capabilities */
    if (get_cookie(C__SND, &hidden->cookie_snd) != C_FOUND) {
        hidden->cookie_snd = SND_PSG;
    }
    
    if (get_cookie(C__MCH, &hidden->cookie_mch) != C_FOUND) {
        hidden->cookie_mch = MCH_ST;
    }

    /* Setup hardware depending on machine type */
    switch (hidden->cookie_mch & MCH_MASK) {
        case MCH_F30:
            /* Falcon audio */
            if (mint_audio_open(device, spec) < 0) {
                return -1;
            }
            break;
        case MCH_STE:
        case MCH_TT:
            /* STE/TT audio */
            if ((hidden->cookie_snd & SND_8BIT) == 0) {
                return SDL_SetError("Sound hardware not available");
            }
            if (mint_audio_open(device, spec) < 0) {
                return -1;
            }
            break;
        default:
            return SDL_SetError("Unsupported audio hardware");
    }

    /* Allocate mixing buffer */
    hidden->mixlen = spec->size;
    hidden->mixbuf = (Uint8 *) SDL_malloc(hidden->mixlen);
    if (hidden->mixbuf == NULL) {
        return SDL_OutOfMemory();
    }
    SDL_memset(hidden->mixbuf, spec->silence, hidden->mixlen);

    return 0;
}

static void MINT_CloseDevice(SDL_AudioDevice *device)
{
    if (device->hidden != NULL) {
        mint_audio_stop(device);
        mint_audio_close(device);

        if (device->hidden->mixbuf != NULL) {
            SDL_free(device->hidden->mixbuf);
        }
        SDL_free(device->hidden);
        device->hidden = NULL;
    }
}

static void MINT_LockDevice(SDL_AudioDevice *device)
{
    /* No locking needed as MiNT handles this */
}

static void MINT_UnlockDevice(SDL_AudioDevice *device)
{
    /* No locking needed as MiNT handles this */
}

static SDL_bool MINT_Init(SDL_AudioDriverImpl * impl)
{
    /* Set the function pointers */
    impl->OpenDevice = MINT_OpenDevice;
    impl->CloseDevice = MINT_CloseDevice;
    impl->LockDevice = MINT_LockDevice;
    impl->UnlockDevice = MINT_UnlockDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->DetectDevices = MINT_DetectDevices;

    return SDL_TRUE;
}

// static SDL_bool MINT_Deinitialize(void)
// {
//     return SDL_TRUE;
// }

AudioBootStrap MINTAUDIO_bootstrap = {
    "atari",
    "Atari audio driver",
    MINT_Init,
    SDL_TRUE
};

#endif /* SDL_AUDIO_DRIVER_MINT */
