#include "SDL_atariaudio.h"
#include "../../SDL_internal.h"
#include "../SDL_audio_c.h"

/* The DMA interrupt handler */
void __attribute__((interrupt)) mint_audio_callback(void)
{
    SDL_AudioDevice *audio = SDL_GetCurrentAudioDevice();
    struct SDL_PrivateAudioData *hidden;
    
    if (!audio || !audio->hidden) {
        return;
    }
    
    hidden = audio->hidden;
    
    if (!hidden->playing) {
        return;
    }

    /* Fill the DMA buffer with audio data */
    SDL_LockMutex(audio->mixer_lock);
    if (!hidden->paused) {
        if (audio->callbackspec.callback) {
            audio->callbackspec.callback(audio->callbackspec.userdata,
                                       hidden->mixbuf,
                                       hidden->mixlen);
        }
    } else {
        SDL_memset(hidden->mixbuf, audio->spec.silence, hidden->mixlen);
    }
    SDL_UnlockMutex(audio->mixer_lock);
}

int mint_audio_open(_THIS, SDL_AudioSpec *spec)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;
    Uint32 freq = spec->freq;
    Uint8 format;
    
    /* Set audio format */
    switch (spec->format & ~SDL_AUDIO_MASK_ENDIAN) {
        case AUDIO_S8:
        case AUDIO_U8:
            spec->format = AUDIO_U8;
            format = FALCON_8BIT;
            break;
        case AUDIO_S16MSB:
        case AUDIO_S16LSB:
            spec->format = AUDIO_S16MSB;
            format = FALCON_16BIT;
            break;
        default:
            SDL_SetError("Unsupported audio format");
            return -1;
    }
    
    /* Set channels */
    format |= (spec->channels > 1) ? FALCON_STEREO : FALCON_MONO;
    
    /* Set frequency */
    switch(freq) {
        case 8000:
            format |= FALCON_FREQ_8K;
            break;
        case 11025:
            format |= FALCON_FREQ_11K;
            break;
        case 16000:
            format |= FALCON_FREQ_16K;
            break;
        case 22050:
            format |= FALCON_FREQ_22K;
            break;
        case 32000:
            format |= FALCON_FREQ_32K;
            break;
        case 44100:
            format |= FALCON_FREQ_44K;
            break;
        case 48000:
            format |= FALCON_FREQ_48K;
            break;
        default:
            SDL_SetError("Unsupported frequency");
            return -1;
    }
    
    /* Setup DMA */
    if (Soundcmd(SNDLOCKED, 0) == SNDLOCKED) {
        SDL_SetError("Audio hardware already in use");
        return -1;
    }
    
    /* Set hardware parameters */
    Devconnect(DMAPLAY, DAC, CLKEXT, format, 1);
    
    /* Setup interrupt handler */
    Xbtimer(XB_TIMERA, 8, 1, mint_audio_callback);
    
    hidden->playing = 0;
    hidden->paused = 0;
    
    return 0;
}

void mint_audio_close(_THIS)
{
    /* Stop DMA */
    Soundcmd(SNDLOCKED, 0);
    
    /* Remove interrupt handler */
    Xbtimer(XB_TIMERA, 0, 1, NULL);
}

void mint_audio_start(_THIS)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;
    
    if (!hidden->playing) {
        hidden->playing = 1;
        Setmode(MODE_STEREO16);
        Settracks(0, 1);
        Setmontracks(0);
        Setinterrupt(SI_TIMERA, SI_PLAY);
        Buffoper(1);
    }
}

void mint_audio_stop(_THIS)
{
    struct SDL_PrivateAudioData *hidden = _this->hidden;
    
    if (hidden->playing) {
        hidden->playing = 0;
        Buffoper(0);
        Setinterrupt(SI_TIMERA, SI_NONE);
    }
}
