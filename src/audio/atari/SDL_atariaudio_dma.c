/* 
 * SDL_atariaudio.c - Drop-Free Hybrid Polling
 * 
 * CRITICAL FIX: DMA distance check prevents underrun
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
#include <pthread.h>

/* --- Definitions --- */
#define SND_16BIT   0x04
#define SND_8BIT    0x02
#define _THIS SDL_AudioDevice *this
#define NUM_CHUNKS 10  /* Safe distance requires multiple chunks */

typedef struct { int freq; int prescale; } FalconFreq;
static const FalconFreq falcon_freq_table[] = {
    { 49170, 1 }, { 33880, 2 }, { 24585, 3 }, { 20770, 4 },
    { 16490, 5 }, { 12292, 6 }, { 9849, 7 },  { 8195, 8 }, { 0, 0 }
};

struct SDL_PrivateAudioData {
    Uint8 *current_fill_ptr, *buffer_base, *buffer_end;
    int chunk_size, playing, swap_needed, xor_needed, total_size;
};

/* --- Audio Conversions --- */
static void convert_u8_s8(Uint8 *ptr, int count)
{
    Uint32 *p32 = (Uint32 *)ptr;
    int c32 = count >> 2;
    int rem = count & 3;

    while (c32--) *p32++ ^= 0x80808080;
    ptr = (Uint8*)p32;
    while (rem--) *ptr++ ^= 0x80;
}

/* --- Optimized Byte Swap (Corrected) --- */
static inline void swap_audio_bytes_asm(Uint8 *ptr, int count)
{
    /* 
     * Process 16 bytes (8 words) per loop iteration.
     * Use proven safe logic to handle remainders correctly.
     */
    __asm__ volatile (
        "   move.l  %0,a0\n"
        "   move.l  %1,d0\n"
        "   lsr.l   #4,d0\n"      /* Divide by 16 */
        "   beq.s   2f\n"
        "   subq.l  #1,d0\n"
        "1:\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   dbra    d0,1b\n"
        "2:\n"
        "   move.l  %1,d0\n"
        "   and.l   #14,d0\n"     /* Remainder < 16 bytes */
        "   beq.s   4f\n"
        "   lsr.l   #1,d0\n"      /* Convert bytes to words */
        "   subq.l  #1,d0\n"
        "3:\n"
        "   move.w  (a0),d1\n"  "   ror.w   #8,d1\n"  "   move.w  d1,(a0)+\n"
        "   dbra    d0,3b\n"
        "4:\n"
        : : "r"(ptr), "r"(count) : "a0", "d0", "d1", "cc", "memory"
    );
}

/* --- DMA Hardware Registers --- */
#define DMA_PTR_HIGH  ((volatile Uint8 *)0xFFFF8909)
#define DMA_PTR_MID   ((volatile Uint8 *)0xFFFF890B)
#define DMA_PTR_LOW   ((volatile Uint8 *)0xFFFF890D)

/* --- Hardware Setup --- */
static void mint_audio_open_hw(SDL_AudioDevice *device, SDL_AudioSpec *spec)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    int i, best_idx = 0, min_diff = 1000000, diff, mode;

    for (i = 0; falcon_freq_table[i].freq != 0; i++) {
        diff = spec->freq - falcon_freq_table[i].freq;
        diff = (diff < 0) ? -diff : diff;
        if (diff < min_diff) {
            min_diff = diff;
            best_idx = i;
        }
    }

    hidden->swap_needed = hidden->xor_needed = 0;
    if (SDL_AUDIO_BITSIZE(spec->format) == 16) {
        if (spec->format == AUDIO_S16LSB) hidden->swap_needed = 1;
        else spec->format = AUDIO_S16MSB;
        mode = (spec->channels > 1) ? MODE_STEREO16 : MODE_MONO16;
        spec->channels = (spec->channels > 1) ? 2 : 1;
    } else {
        if (spec->format == AUDIO_U8) hidden->xor_needed = 1;
        else spec->format = AUDIO_S8;
        mode = (spec->channels > 1) ? MODE_STEREO8 : MODE_MONO;
    }

    SDL_CalculateAudioSpec(spec);
    hidden->chunk_size = spec->size;
    hidden->total_size = hidden->chunk_size * NUM_CHUNKS;
    hidden->buffer_base = (Uint8 *)Mxalloc(hidden->total_size, MX_STRAM);
    
    if (!hidden->buffer_base) {
        SDL_SetError("Out of ST-RAM");
        return;
    }

    hidden->buffer_end = hidden->buffer_base + hidden->total_size;
    SDL_memset(hidden->buffer_base, device->spec.silence, hidden->total_size);
    hidden->current_fill_ptr = hidden->buffer_base;
    hidden->playing = 0;

    Locksnd();
    Sndstatus(SND_RESET);
    Buffoper(0);
    Devconnect(DMAPLAY, DAC, CLK25M, falcon_freq_table[best_idx].prescale, NO_SHAKE);
    Setmode(mode);
    Settracks(0, 0);
    Setmontracks(0);
}

static void mint_audio_start_hw(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    int i;
    Uint8 *chunk_ptr;
    
    if (!hidden->playing) {
        for (i = 0; i < NUM_CHUNKS; i++) {
            chunk_ptr = hidden->buffer_base + (i * hidden->chunk_size);
            if (device->callbackspec.callback) {
                device->callbackspec.callback(device->callbackspec.userdata, 
                                             chunk_ptr, hidden->chunk_size);
                if (hidden->swap_needed) swap_audio_bytes_asm(chunk_ptr, hidden->chunk_size);
                else if (hidden->xor_needed) convert_u8_s8(chunk_ptr, hidden->chunk_size);
            }
        }

        Setbuffer(SR_PLAY, hidden->buffer_base, hidden->buffer_end);
        hidden->playing = 1;
        hidden->current_fill_ptr = hidden->buffer_base;
        Buffoper(SB_PLA_ENA | SB_PLA_RPT);
    }
}

static void mint_audio_stop_hw(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    if (hidden->playing) {
        hidden->playing = 0;
        Buffoper(0);
    }
}

static void mint_audio_close_hw(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    Buffoper(0);
    if (hidden->buffer_base) {
        Mfree(hidden->buffer_base);
        hidden->buffer_base = NULL;
    }
    Unlocksnd();
}

/* --- SDL Interface --- */
static void ATARI_CloseDevice(SDL_AudioDevice *device)
{
    if (device->hidden) {
        mint_audio_stop_hw(device);
        mint_audio_close_hw(device);
        SDL_free(device->hidden);
        device->hidden = NULL;
    }
}

/* 
 * WaitDevice - DROP PREVENTION: DMA must be 2+ chunks ahead
 */
static void ATARI_WaitDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;
    Uint8 *dma_pos;
    Uint32 dma_offset, fill_offset;
    int32_t distance;
    
    fill_offset = hidden->current_fill_ptr - hidden->buffer_base;
    
    while (hidden->playing) {
        dma_pos = (Uint8 *)(((Uint32)*DMA_PTR_HIGH << 16) | 
                         ((Uint32)*DMA_PTR_MID  << 8)  | 
                          (Uint32)*DMA_PTR_LOW);
        
        dma_offset = dma_pos - hidden->buffer_base;
        distance = (int32_t)dma_offset - (int32_t)fill_offset;
        
        /* Wraparound-safe distance */
        if (distance < 0) distance += hidden->total_size;
        
        /* CRITICAL: DMA must be at least 2 chunks ahead to prevent underrun */
        if (distance >= (hidden->chunk_size * 2)) {
            break; /* Safe to fill */
        }
        
        pthread_yield(); /* FreeMiNT optimization */
    }
}

static Uint8 *ATARI_GetDeviceBuf(_THIS)
{
    return this->hidden->current_fill_ptr;
}

static void ATARI_PlayDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;
    Uint8 *filled_chunk = hidden->current_fill_ptr;
    
    if (hidden->swap_needed) swap_audio_bytes_asm(filled_chunk, hidden->chunk_size);
    else if (hidden->xor_needed) convert_u8_s8(filled_chunk, hidden->chunk_size);
    
    hidden->current_fill_ptr += hidden->chunk_size;
    if (hidden->current_fill_ptr >= hidden->buffer_end) {
        hidden->current_fill_ptr = hidden->buffer_base;
    }
}

static int ATARI_OpenDevice(SDL_AudioDevice *device, const char *devname)
{
    device->hidden = (struct SDL_PrivateAudioData *)SDL_malloc(sizeof(*device->hidden));
    if (!device->hidden) return SDL_OutOfMemory();
    SDL_memset(device->hidden, 0, sizeof(*device->hidden));

    mint_audio_open_hw(device, &device->spec);
    mint_audio_start_hw(device);
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
    impl->WaitDevice = ATARI_WaitDevice;
    impl->GetDeviceBuf = ATARI_GetDeviceBuf;
    impl->PlayDevice = ATARI_PlayDevice;
    impl->ProvidesOwnCallbackThread = SDL_FALSE;
    impl->HasCaptureSupport = SDL_FALSE;

    return SDL_TRUE;
}

AudioBootStrap ATARIAUDIO_bootstrap = {
    "mint_xbios",
    "Atari XBIOS Audio (Drop-Free Polling)",
    ATARI_Init,
    SDL_FALSE
};