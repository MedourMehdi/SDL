#ifndef SDL_atariaudio_h_
#define SDL_atariaudio_h_

#include "../../SDL_internal.h"
#include "SDL_audio.h"
#include "../SDL_sysaudio.h"

#include <mint/osbind.h>
#include <mint/cookie.h>
#include <mint/falcon.h>

struct SDL_PrivateAudioData {
    Uint8 *buffer_a;          /* First DMA buffer */
    Uint8 *buffer_b;          /* Second DMA buffer */
    Uint8 *current_buffer;    /* Currently active buffer */
    int mixlen;               /* Buffer length */
    unsigned long cookie_snd; /* Sound capabilities */
    unsigned long cookie_mch; /* Machine type */
    volatile Uint8 playing;   /* Playback status */
    volatile Uint8 paused;    /* Pause status */
    int buffer_ready;         /* Buffer status */
};

/* Machine types */
#define MCH_ST      0x00000000
#define MCH_STE     0x00010000
#define MCH_TT      0x00020000
#define MCH_F30     0x00030000
#define MCH_ARA     0x00050000
#define MCH_MASK    0xFFFF0000

/* Sound capabilities */
#define SND_PSG     0x01    /* Yamaha PSG */
#define SND_8BIT    0x02    /* 8 bit DMA */
#define SND_16BIT   0x04    /* 16 bit DMA */
#define SND_DSP     0x08    /* DSP */
#define SND_MATRIX  0x10    /* Matrix mixer */
#define SND_EXT     0x20    /* External clock */
#define SND_CLOCK25 0x40    /* Clock 25.175 MHz */
#define SND_CLOCK32 0x80    /* Clock 32 MHz */

int mint_audio_open(SDL_AudioDevice *device, SDL_AudioSpec *spec);
void mint_audio_close(SDL_AudioDevice *device);
void mint_audio_start(SDL_AudioDevice *device);
void mint_audio_stop(SDL_AudioDevice *device);

#endif /* SDL_atariaudio_h_ */
