/*
 * SDL_atariaudio.c - Version 1: Callback INSIDE Interrupt
 * * WARNING: The audio callback must be FAST. Do not use printf or complex 
 * OS calls inside the callback, or the system will crash/freeze.
 */

#include "../../SDL_internal.h"
#include "SDL.h"
#include "SDL_audio.h"
#include "../SDL_sysaudio.h"
#include "../SDL_audio_c.h"

#include <mint/osbind.h>
#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/mintbind.h>
#include <unistd.h>

/* --- Definitions & Structs --- */
#define MCH_MASK    0xFFFF0000
#define SND_16BIT   0x04
#define SND_8BIT    0x02

typedef struct {
    int freq;
    int prescale;
} FalconFreq;

static const FalconFreq falcon_freq_table[] = {
    { 49170, 1 }, { 33880, 2 }, { 24585, 3 }, { 20770, 4 },
    { 16490, 5 }, { 12292, 6 }, { 9849, 7 },  { 8195, 8 }, { 0, 0 }
};

struct SDL_PrivateAudioData {
    Uint8 *rawbuf;         /* Single allocation for both buffers */
    Uint8 *buffer[2];      /* Pointers to the two halves (A and B) */
    int    mixlen;         /* Length of one buffer */
    int    play_idx;       /* The buffer currently being played by hardware */
    int    write_idx;      /* The buffer we are currently filling */
};

/* Global pointer for ISR visibility */
static SDL_AudioDevice *volatile isr_audio_device = NULL;
static volatile int in_isr = 0;
static volatile int load_sample = 0;
static volatile int device_active = 0;
static SDL_Thread *audio_thread = NULL;

/* --- Assembly Helper (Optimized Swap) --- */
static inline void swap_audio_bytes_asm(Uint8 *ptr, int count)
{
    __asm__ volatile (
        "   move.l  %0,a5\n"        /* Use a5 for pointer */
        "   move.l  %1,d0\n"
        "   lsr.l   #4,d0\n"
        "   beq.s   2f\n"
        "   subq.l  #1,d0\n"
        "1:\n"
        /* Process 16 bytes (8 words) per iteration using d5 exclusively */
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   dbra    d0,1b\n"
        "2:\n"
        "   move.l  %1,d0\n"
        "   and.l   #14,d0\n"
        "   beq.s   4f\n"
        "   lsr.l   #1,d0\n"
        "   subq.l  #1,d0\n"
        "3:\n"
        "   move.w  (a5),d5\n"  "   ror.w   #8,d5\n"  "   move.w  d5,(a5)+\n"
        "   dbra    d0,3b\n"
        "4:\n"
        : : "r"(ptr), "r"(count) : "a5", "d0", "d5", "cc", "memory"
    );
}

/* --- Minimal ISR (only sets flag) --- */
void __attribute__((interrupt)) mint_audio_callback(void)
{
    load_sample = 1;
    *((volatile unsigned char*)0xFFFFFA0FL) &= ~(1<<5);
}

/* --- Processing Function (now runs in thread) --- */
static void mint_audio_process(void)
{
    struct SDL_PrivateAudioData *hidden;
    Uint8 *write_buf;
    
    if (!isr_audio_device || !isr_audio_device->hidden) return;
    hidden = isr_audio_device->hidden;

    Setbuffer(SR_PLAY, hidden->buffer[hidden->play_idx], 
              hidden->buffer[hidden->play_idx] + hidden->mixlen);
    
    write_buf = hidden->buffer[hidden->play_idx];
    
    if (isr_audio_device->callbackspec.callback) {
        isr_audio_device->callbackspec.callback(
            isr_audio_device->callbackspec.userdata,
            write_buf, 
            hidden->mixlen
        );
    } else {
        SDL_memset(write_buf, isr_audio_device->spec.silence, hidden->mixlen);
    }

    if (isr_audio_device->spec.format == AUDIO_S16LSB) {
        swap_audio_bytes_asm(write_buf, hidden->mixlen);
    }

    hidden->play_idx ^= 1; 
}

/* --- Thread that waits and processes --- */
static int AudioProcessingThread(void *data)
{
    while (device_active) {
        /* Wait for ISR signal (polite yield for MiNT) */
        while (load_sample == 0 && device_active) {
            #ifdef __MINT__
                Fselect(1, NULL, NULL, NULL);  /* Sleep 1ms, yield CPU */
            #else
                /* TOS single-tasking: minimal wait */
                __asm__ volatile("nop");
            #endif
        }
        
        if (!device_active) break;
        
        /* Process one buffer */
        mint_audio_process();
        
        /* Clear flag for next interrupt */
        load_sample = 0;
    }
    return 0;
}

/* --- Device Functions --- */
static void ATARI_CloseDevice(SDL_AudioDevice *device)
{
    if (device->hidden) {
        device_active = 0;  // Signal thread to exit
        
        if (audio_thread) {
            SDL_WaitThread(audio_thread, NULL);
            audio_thread = NULL;
        }
        
        Buffoper(0);
        Jdisint(MFP_TIMERA);
        isr_audio_device = NULL;
        
        if (device->hidden->rawbuf) Mfree(device->hidden->rawbuf);
        Unlocksnd();
        SDL_free(device->hidden);
    }
}

static int ATARI_OpenDevice(SDL_AudioDevice *device, const char *devname)
{
    struct SDL_PrivateAudioData *hidden;
    int i, best_idx = 0, min_diff = 1000000, diff;

    hidden = (struct SDL_PrivateAudioData *)SDL_malloc(sizeof(*hidden));
    if (!hidden) return SDL_OutOfMemory();
    SDL_memset(hidden, 0, sizeof(*hidden));
    device->hidden = hidden;

    /* Freq Init */
    for (i = 0; falcon_freq_table[i].freq != 0; i++) {
        diff = abs(device->spec.freq - falcon_freq_table[i].freq);
        if (diff < min_diff) { min_diff = diff; best_idx = i; }
    }
    device->spec.freq = falcon_freq_table[best_idx].freq;

    /* Format Init */
    if (SDL_FirstAudioFormat(device->spec.format) == AUDIO_S16) {
        device->spec.format = AUDIO_S16LSB;
        device->spec.channels = 2;
    } else {
        device->spec.format = AUDIO_S8;
        device->spec.channels = (device->spec.channels > 1) ? 2 : 1;
    }

    SDL_CalculateAudioSpec(&device->spec);
    hidden->mixlen = (device->spec.size + 15) & ~15; /* Align 16 */
    // For 16-bit stereo, ensure it's a multiple of 4
    if (device->spec.format == AUDIO_S16LSB && hidden->mixlen % 4) {
        hidden->mixlen = (hidden->mixlen + 3) & ~3;
    }
    /* Alloc ST-RAM */
    hidden->rawbuf = (Uint8 *)Mxalloc(hidden->mixlen * 2, MX_STRAM);
    if (!hidden->rawbuf) return SDL_SetError("No ST-RAM");
    // Ensure pointer is WORD-aligned (must be even)
    if ((uintptr_t)hidden->rawbuf & 1) {
        /* This should never happen with Mxalloc, but check anyway */
        Mfree(hidden->rawbuf);
        return SDL_SetError("Buffer misalignment");
    }    
    SDL_memset(hidden->rawbuf, device->spec.silence, hidden->mixlen * 2);

    hidden->buffer[0] = hidden->rawbuf;
    hidden->buffer[1] = hidden->rawbuf + hidden->mixlen;
    hidden->play_idx = 0; /* Hardware starts here */

    /* Hardware Init */
    Locksnd();
    Sndstatus(SND_RESET);
    Devconnect(DMAPLAY, DAC, CLK25M, falcon_freq_table[best_idx].prescale, NO_SHAKE);
    Setmode((device->spec.channels == 2) ? 
           (device->spec.format == AUDIO_S16LSB ? MODE_STEREO16 : MODE_STEREO8) : MODE_MONO);
    Settracks(0, 0);
    Setmontracks(0);

    /* Setup ISR */
    isr_audio_device = device;
    Setinterrupt(SI_TIMERA, SI_PLAY);
    Xbtimer(XB_TIMERA, 1<<3, 1, mint_audio_callback);

    /* STARTUP: Prime the pipeline */
    /* 1. Set Buffer A as Active */
    Setbuffer(SR_PLAY, hidden->buffer[0], hidden->buffer[0] + hidden->mixlen);
    Jenabint(MFP_TIMERA);
    Buffoper(SB_PLA_ENA | SB_PLA_RPT);
    Setbuffer(SR_PLAY, hidden->buffer[1], hidden->buffer[1] + hidden->mixlen);
    hidden->play_idx = 1;
    
    // Start processing thread
    device_active = 1;
    audio_thread = SDL_CreateThread(AudioProcessingThread, "AtariAudio", NULL);
    if (!audio_thread) {
        ATARI_CloseDevice(device);
        return SDL_SetError("Failed to create audio thread");
    }
    
    return 0;
}

static SDL_bool ATARI_Init(SDL_AudioDriverImpl *impl)
{
    long cookie;
    if (Getcookie(C__SND, &cookie) != C_FOUND) return SDL_FALSE;
    if (!(cookie & (SND_16BIT | SND_8BIT))) return SDL_FALSE;

    impl->OpenDevice = ATARI_OpenDevice;
    impl->CloseDevice = ATARI_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    
    /* V1 Specific: We drive the bus */
    impl->ProvidesOwnCallbackThread = SDL_TRUE; 

    return SDL_TRUE;
}

AudioBootStrap ATARIAUDIO_bootstrap = {
    "mint_xbios", "Atari XBIOS DMA Audio (ISR Mode)", ATARI_Init, SDL_FALSE
};