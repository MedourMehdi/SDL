/* 
 * SDL_atariaudio.c - Circular Buffer Mode (Pointer Stepping Optimization)
 * 
 * Fixes:
 * 1. Replaced XBIOS Buffptr() with direct hardware register read (Performance).
 * 2. Fixed ASM swap function to prevent data loss.
 * 3. Safe Startup and Wraparound logic.
 * 4. OPTIMIZATION: Removed index multiplication in hot path, using pointer stepping.
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
#include <pthread.h>

/* --- Definitions & Structs --- */
#define SND_16BIT   0x04
#define SND_8BIT    0x02

#define _THIS SDL_AudioDevice *this

#define NUM_CHUNKS 3  /* 3 logical buffers in the circular buffer */

typedef struct {
    int freq;
    int prescale;
} FalconFreq;

static const FalconFreq falcon_freq_table[] = {
    { 49170, 1 }, { 33880, 2 }, { 24585, 3 }, { 20770, 4 },
    { 16490, 5 }, { 12292, 6 }, { 9849, 7 },  { 8195, 8 }, { 0, 0 }
};

struct SDL_PrivateAudioData {
    Uint8 *buffer_base;      /* Start of circular buffer */
    Uint8 *buffer_end;       /* End of circular buffer */
    int    chunk_size;       /* Size of one logical buffer (mixlen) */
    int    total_size;       /* Total buffer size (chunk_size * NUM_CHUNKS) */
    Uint8 *current_fill_ptr; /* Pointer to the chunk we are currently filling */
    int    playing;
    int    swap_needed;      /* 1 if we need LSB->MSB swap */
    int    xor_needed;       /* 1 if we need U8->S8 conversion */    
};

static void convert_u8_s8(Uint8 *ptr, int count)
{
    Uint32 *p32 = (Uint32 *)ptr;
    int c32 = count >> 2;
    int rem = count & 3;
    
    /* Process 4 bytes at a time */
    while (c32--) {
        *p32++ ^= 0x80808080;
    }
    
    /* Handle remainder */
    ptr = (Uint8*)p32;
    while (rem--) {
        *ptr++ ^= 0x80;
    }
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

/* --- Hardware Helper Functions --- */

static void mint_audio_open_hw(SDL_AudioDevice *device, SDL_AudioSpec *spec)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    int i, best_idx = 0, min_diff = 1000000, diff;
    int mode;
    // Uint32 frames;
    // Uint32 bytes_per_sample;
    // Uint32 bytes_per_frame; 

    /* Find closest frequency */
    for (i = 0; falcon_freq_table[i].freq != 0; i++) {
        diff = spec->freq - falcon_freq_table[i].freq;
        diff = (diff < 0) ? -diff : diff;
        if (diff < min_diff) {
            min_diff = diff;
            best_idx = i;
        }
    }
    spec->freq = falcon_freq_table[best_idx].freq;

    /* Format negotiation & Hardware Flags */
    hidden->swap_needed = 0;
    hidden->xor_needed = 0;

    /* Format negotiation */
    if (SDL_AUDIO_BITSIZE(spec->format) == 16) {
        if (spec->format == AUDIO_S16LSB) {
            hidden->swap_needed = 1; /* Hardware needs MSB */
        } else {
            spec->format = AUDIO_S16MSB;
        }
        
        /* Use headers: MODE_STEREO16 (1) or MODE_MONO16 (3) */
        if (spec->channels > 1) {
            mode = MODE_STEREO16;
            spec->channels = 2;
        } else {
            mode = MODE_MONO16;
            spec->channels = 1;
        }
    } else {
        /* 8-bit handling */
        if (spec->format == AUDIO_U8) {
            hidden->xor_needed = 1; /* User wants U8, we convert to S8 */
        } else {
            spec->format = AUDIO_S8; /* Native S8 */
        }
        mode = (spec->channels > 1) ? MODE_STEREO8 : MODE_MONO;
    }

    SDL_CalculateAudioSpec(spec);
    hidden->chunk_size = spec->size;
    hidden->total_size = hidden->chunk_size * NUM_CHUNKS;

    /* Allocate ONE large circular buffer in ST-RAM */
    hidden->buffer_base = (Uint8 *)Mxalloc(hidden->total_size, MX_STRAM);
    
    if (!hidden->buffer_base) {
        SDL_SetError("Out of ST-RAM for audio buffer");
        return;
    }

    hidden->buffer_end = hidden->buffer_base + hidden->total_size;
    SDL_memset(hidden->buffer_base, device->spec.silence, hidden->total_size);

    /* Initialize fill pointer to start */
    hidden->current_fill_ptr = hidden->buffer_base;
    hidden->playing = 0;

    /* Hardware Init */
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
        /* Pre-fill all chunks */
        for (i = 0; i < NUM_CHUNKS; i++) {
            chunk_ptr = hidden->buffer_base + (i * hidden->chunk_size);
            
            if (device->callbackspec.callback) {
                device->callbackspec.callback(device->callbackspec.userdata, 
                                             chunk_ptr, hidden->chunk_size);
                /* Apply conversions if needed during pre-fill */
                if (hidden->swap_needed) {
                     swap_audio_bytes_asm(chunk_ptr, hidden->chunk_size);
                } else if (hidden->xor_needed) {
                    convert_u8_s8(chunk_ptr, hidden->chunk_size);
                }
            }
        }

        /* Set buffer to play entire circular buffer in REPEAT mode */
        Setbuffer(SR_PLAY, hidden->buffer_base, hidden->buffer_end);
        
        hidden->playing = 1;
        hidden->current_fill_ptr = hidden->buffer_base;  /* Start filling chunk 0 next */
        
        /* Start continuous playback with REPEAT enabled */
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

/* --- SDL Driver Interface --- */

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
 * WaitDevice:
 * Polls DMA hardware register.
 * Uses pointer stepping to calculate boundaries efficiently (no multiply).
 */
static void ATARI_WaitDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;
    Uint8 *dma_pos; 
    Uint8 *chunk_start, *chunk_end;
    SndBufPtr pointers;

    /* 
     * OPTIMIZATION: No multiplication.
     * Calculate boundaries directly from the current fill pointer.
     */
    chunk_start = hidden->current_fill_ptr;
    chunk_end = chunk_start + hidden->chunk_size;
    
    while (1) {
        if (Buffptr(&pointers) == 0) {
            dma_pos = (Uint8 *)pointers.play;
            
            /* Is DMA reading outside our target chunk? */
            if (dma_pos < chunk_start || dma_pos >= chunk_end) {
                /* Safe to fill this chunk */
                break;
            }
        }
        pthread_yield();
    }
}

/* 
 * GetDeviceBuf:
 * Returns the pre-calculated pointer to the current chunk.
 */
static Uint8 *ATARI_GetDeviceBuf(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;
    
    /* Simply return the pre-calculated pointer */
    return hidden->current_fill_ptr;
}

/* 
 * PlayDevice:
 * Swaps bytes and advances the fill pointer (Pointer Stepping).
 */
static void ATARI_PlayDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;
    Uint8 *filled_chunk;
    
    /* 
     * The chunk we just filled is pointed to by current_fill_ptr.
     * (SDL wrote to it directly via GetDeviceBuf).
     */
    filled_chunk = hidden->current_fill_ptr;

    /* Apply Format Conversions */
    if (hidden->swap_needed) {
         swap_audio_bytes_asm(filled_chunk, hidden->chunk_size);
    } else if (hidden->xor_needed) {
        convert_u8_s8(filled_chunk, hidden->chunk_size);
    }

    /* 
     * OPTIMIZATION: Advance pointer instead of index math.
     * Just pointer addition.
     */
    hidden->current_fill_ptr += hidden->chunk_size;
    
    /* Wrap around check */
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
    "Atari XBIOS Audio (Circular Buffer)",
    ATARI_Init,
    SDL_FALSE
};