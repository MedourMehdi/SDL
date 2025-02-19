#include "SDL_atariaudio.h"
#include "../../SDL_internal.h"
#include "../SDL_audio_c.h"

static SDL_AudioDevice *volatile current_audio = NULL;
static volatile int loadNewSample = 0;

void __attribute__((interrupt)) mint_audio_callback(void)
{
    register SDL_AudioDevice *audio = current_audio;
    register struct SDL_PrivateAudioData *hidden;
    
    *((volatile unsigned char*)0xFFFFFA0FL) &= ~(1<<5);
    
    if (!audio || !audio->hidden) {
        return;
    }
    
    hidden = audio->hidden;
    if (!hidden->playing) {
        return;
    }

    if (!hidden->paused && audio->callbackspec.callback) {
        // Fill next buffer while current one is playing
        Uint8 *next_buffer = (hidden->current_buffer == hidden->buffer_a) ? 
                             hidden->buffer_b : hidden->buffer_a;
        
        audio->callbackspec.callback(audio->callbackspec.userdata,
                                   next_buffer,
                                   hidden->mixlen);
        
        Setbuffer(SR_PLAY, next_buffer, next_buffer + hidden->mixlen);
        hidden->current_buffer = next_buffer;
    }
    loadNewSample = 1;
}

static void enableTimerA(void)
{
    *((volatile unsigned char*)0xFFFFFA17L) |= (1<<3);
}

int mint_audio_open(SDL_AudioDevice *device, SDL_AudioSpec *spec)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    int prescale;
    
    current_audio = device;
    
    prescale = ((25175000 >> 8) / spec->freq - 1);
    
    hidden->mixlen = spec->size;
    hidden->buffer_a = (Uint8 *)Mxalloc(hidden->mixlen, MX_STRAM);
    hidden->buffer_b = (Uint8 *)Mxalloc(hidden->mixlen, MX_STRAM);
    
    if (!hidden->buffer_a || !hidden->buffer_b) {
        return SDL_SetError("Failed to allocate ST RAM buffers");
    }
    
    SDL_memset(hidden->buffer_a, 0, hidden->mixlen);
    SDL_memset(hidden->buffer_b, 0, hidden->mixlen);
    hidden->current_buffer = hidden->buffer_a;
    
    // Stop any ongoing DMA
    Buffoper(0x00);
    Jdisint(MFP_TIMERA);
    
    // Setup DMA
    Devconnect(DMAPLAY, DAC, CLK25M, prescale, NO_SHAKE);
    Setmode(MODE_STEREO16);
    Settracks(0, 1);
    Setmontracks(0);
    
    // Setup timer
    Setinterrupt(SI_TIMERA, SI_PLAY);
    Xbtimer(XB_TIMERA, 1<<3, 1, mint_audio_callback);
    Supexec(enableTimerA);
    
    hidden->playing = 0;
    hidden->paused = 0;
    
    return 0;
}

void mint_audio_start(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    
    if (!hidden->playing) {
        hidden->playing = 1;
        
        Setbuffer(SR_PLAY, hidden->current_buffer, 
                 hidden->current_buffer + hidden->mixlen);
        
        Jenabint(MFP_TIMERA);
        Buffoper(SB_PLA_ENA | SB_PLA_RPT);
        loadNewSample = 1;
    }
}

void mint_audio_stop(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    
    if (hidden->playing) {
        hidden->playing = 0;
        Buffoper(0x00);
        Jdisint(MFP_TIMERA);
        Setinterrupt(SI_TIMERA, SI_NONE);
    }
}

void mint_audio_close(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    
    Buffoper(0x00);
    Jdisint(MFP_TIMERA);
    
    if (hidden->buffer_a) {
        Mfree(hidden->buffer_a);
    }
    if (hidden->buffer_b) {
        Mfree(hidden->buffer_b);
    }
    
    Soundcmd(SNDLOCKED, 0);
    Xbtimer(XB_TIMERA, 0, 1, NULL);
}
