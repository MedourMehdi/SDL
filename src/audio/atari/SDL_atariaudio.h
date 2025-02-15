#ifndef SDL_atariaudio_h_
#define SDL_atariaudio_h_

#include "../../SDL_internal.h"

#include "SDL_audio.h"
#include "../SDL_sysaudio.h"

#define _THIS SDL_AudioDevice *_this

/* Include MiNT headers */
#include <mint/osbind.h>
#include <mint/cookie.h>
#include <mint/falcon.h>

/* MiNT audio driver structure */
struct SDL_PrivateAudioData {
    Uint8 *mixbuf;          /* The mixing buffer */
    int mixlen;             /* Length of mixing buffer */
    SDL_AudioFormat format; /* Audio format */
    Uint8 channels;         /* Number of channels */
    Uint8 isDma;           /* Using DMA or not */
    
    /* Hardware configuration */
    unsigned long cookie_snd;
    unsigned long cookie_mch;
    volatile Uint8 playing;  /* Audio is playing */
    volatile Uint8 paused;   /* Audio is paused */
};

/* Cookie _MCH definitions */
#define MCH_ST      0x00000000
#define MCH_STE     0x00010000
#define MCH_TT      0x00020000
#define MCH_F30     0x00030000
#define MCH_MASK    0xFFFF0000

/* Cookie _SND definitions */
#define SND_PSG         0x01    /* Yamaha PSG */
#define SND_8BIT        0x02    /* 8 bit DMA */
#define SND_16BIT       0x04    /* 16 bit DMA */
#define SND_DSP         0x08    /* DSP */
#define SND_MATRIX      0x10    /* Matrix mixer */
#define SND_EXT         0x20    /* External clock */
#define SND_CLOCK25    0x40    /* Clock 25.175 MHz */
#define SND_CLOCK32    0x80    /* Clock 32 MHz */

/* Falcon audio constants */
#define FALCON_8BIT       0x00
#define FALCON_16BIT      0x01
#define FALCON_MONO       0x00
#define FALCON_STEREO     0x02

/* Falcon frequency constants */
#define FALCON_FREQ_8K    0x00
#define FALCON_FREQ_11K   0x01
#define FALCON_FREQ_16K   0x02
#define FALCON_FREQ_22K   0x03
#define FALCON_FREQ_32K   0x04
#define FALCON_FREQ_44K   0x05
#define FALCON_FREQ_48K   0x06

// /* Function prototypes */
int mint_audio_open(SDL_AudioDevice *device, SDL_AudioSpec *spec);
void mint_audio_close(SDL_AudioDevice *device);
void mint_audio_start(SDL_AudioDevice *device);
void mint_audio_stop(SDL_AudioDevice *device);

// extern int mint_audio_open(_THIS, SDL_AudioSpec *spec);
// extern void mint_audio_close(_THIS);
// extern void mint_audio_start(_THIS);
// extern void mint_audio_stop(_THIS);

#endif /* SDL_atariaudio_h_ */
