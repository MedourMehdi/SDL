/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>
  Optimized for Motorola 68000 / Atari Falcon

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* ============================================================================
   SDL_atariaudio_dma.c – Atari Falcon XBIOS DMA audio driver for SDL2
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Drop-free circular buffer polling driver using Falcon DMA audio hardware.
   No interrupt handler — the main thread polls the DMA position register
   directly and yields/sleeps intelligently to avoid bus contention.

   Buffer strategy:
    - NUM_CHUNKS-chunk circular buffer allocated in ST-RAM (Mxalloc)
    - Fill pointer advances one chunk per callback
    - DMA position read via double-sample loop (read_dma_pos)
    - WaitDevice intelligently uses pthread_yield() when close, and 
      SDL_Delay(2) when far enough ahead to free the hardware bus.

   Format handling (Fully optimized 68k Assembly):
     - 16-bit: S16MSB native; S16LSB triggers inline byte-swap
     - 8-bit:  S8 native; U8 triggers XOR 0x80 conversion
   ============================================================================ */

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
#define SND_16BIT        0x04
#define SND_8BIT         0x02
#define _THIS            SDL_AudioDevice *this
#define NUM_CHUNKS       4      

static Uint32  dma_offset;
static int32_t distance;

typedef struct { int freq; int prescale; } FalconFreq;
static const FalconFreq falcon_freq_table[] = {
    { 49170, 1 }, { 33880, 2 }, { 24585, 3 }, { 20770, 4 },
    { 16490, 5 }, { 12292, 6 }, { 9849,  7 }, { 8195,  8 },
    { 0, 0 }
};

struct SDL_PrivateAudioData {
    Uint8          *current_fill_ptr;
    Uint8          *buffer_base;
    Uint8          *buffer_end;
    int             chunk_size;
    int             total_size;
    int             swap_needed;
    int             xor_needed;
    volatile int    playing;    
};

/* -------------------------------------------------------------------------
 * Optimized Audio Format Conversions (68000 Assembly)
 * ---------------------------------------------------------------------- */

/* convert_sign_bit_asm: Processes 16-byte blocks using movem.l and
   dual-pointer post-increment addressing, followed by a remainder loop. */
static inline void convert_sign_bit_asm(Uint8 *ptr, int count)
{
    int loops = count >> 4;       /* Number of 16-byte blocks */
    int rem   = count & 15;       /* Remaining bytes (0-15) */
    Uint8 *write_ptr = ptr;
    
    if (loops > 0) {
        Uint32 mask = 0x80808080UL;
        loops--; /* Adjust for dbra (stops at -1) */
        __asm__ volatile (
            "1:\n\t"
            "   movem.l (%0)+, d0-d3\n\t"   /* Read 16 bytes and advance ptr */
            "   eor.l   %2, d0\n\t"
            "   eor.l   %2, d1\n\t"
            "   eor.l   %2, d2\n\t"
            "   eor.l   %2, d3\n\t"
            "   move.l  d0, (%1)+\n\t"      /* Write and advance write_ptr */
            "   move.l  d1, (%1)+\n\t"
            "   move.l  d2, (%1)+\n\t"
            "   move.l  d3, (%1)+\n\t"
            "   dbra    %3, 1b\n\t"
            : "+a" (ptr), "+a" (write_ptr)
            : "d" (mask), "d" (loops)
            : "d0", "d1", "d2", "d3", "memory", "cc"
        );
    }
    
    if (rem > 0) {
        rem--;
        __asm__ volatile (
            "2:\n\t"
            "   move.b  (%0), d0\n\t"
            "   eor.b   #0x80, d0\n\t"
            "   move.b  d0, (%0)+\n\t"
            "   dbra    %1, 2b\n\t"
            : "+a" (ptr) : "d" (rem) : "d0", "memory", "cc"
        );
    }
}

/* swap_audio_bytes_asm: Processes 16-byte blocks (8 samples) using movem.l
   and the 4-cycle 'swap' trick, followed by a remainder loop for words. */
static inline void swap_audio_bytes_asm(Uint8 *ptr, int count)
{
    int loops = count >> 4;         /* Number of 16-byte blocks */
    int rem   = (count & 15) >> 1;  /* Remaining 2-byte words */
    Uint8 *write_ptr = ptr;

    if (loops > 0) {
        loops--;
        __asm__ volatile (
            "1:\n\t"
            "   movem.l (%0)+, d0-d3\n\t"
            "   ror.w   #8, d0\n\t" "   swap    d0\n\t" "   ror.w   #8, d0\n\t" "   swap    d0\n\t"
            "   ror.w   #8, d1\n\t" "   swap    d1\n\t" "   ror.w   #8, d1\n\t" "   swap    d1\n\t"
            "   ror.w   #8, d2\n\t" "   swap    d2\n\t" "   ror.w   #8, d2\n\t" "   swap    d2\n\t"
            "   ror.w   #8, d3\n\t" "   swap    d3\n\t" "   ror.w   #8, d3\n\t" "   swap    d3\n\t"
            "   move.l  d0, (%1)+\n\t"
            "   move.l  d1, (%1)+\n\t"
            "   move.l  d2, (%1)+\n\t"
            "   move.l  d3, (%1)+\n\t"
            "   dbra    %2, 1b\n\t"
            : "+a" (ptr), "+a" (write_ptr) 
            : "d" (loops)
            : "d0", "d1", "d2", "d3", "memory", "cc"
        );
    }
    
    if (rem > 0) {
        rem--;
        __asm__ volatile (
            "2:\n\t"
            "   move.w  (%0), d0\n\t"
            "   ror.w   #8, d0\n\t"
            "   move.w  d0, (%0)+\n\t"
            "   dbra    %1, 2b\n\t"
            : "+a" (ptr) : "d" (rem) : "d0", "memory", "cc"
        );
    }
}

/* -------------------------------------------------------------------------
 * Hardware Access
 * ---------------------------------------------------------------------- */

/* read_dma_pos: ASM optimized double-read to ensure stability during DMA motion.
   Uses Absolute Short (.w) addressing for fastest instruction fetch. */
static Uint8 *read_dma_pos(void)
{
    Uint32 res;
    __asm__ volatile (
        "0:\n\t"
        "   moveq   #0, d0\n\t"
        "   move.b  0xFFFF8909.w, d0\n\t"   /* High byte */
        "   swap    d0\n\t"
        "   move.b  0xFFFF890B.w, d0\n\t"   /* Mid byte */
        "   lsl.w   #8, d0\n\t"
        "   move.b  0xFFFF890D.w, d0\n\t"   /* Low byte */
        "   move.l  d0, d1\n\t"             /* Store first sample */
        "   moveq   #0, d0\n\t"
        "   move.b  0xFFFF8909.w, d0\n\t"
        "   swap    d0\n\t"
        "   move.b  0xFFFF890B.w, d0\n\t"
        "   lsl.w   #8, d0\n\t"
        "   move.b  0xFFFF890D.w, d0\n\t"
        "   cmp.l   d0, d1\n\t"             /* Verify stability */
        "   bne.s   0b\n\t"
        "   move.l  d0, %0\n\t"
        : "=d"(res) : : "d0", "d1", "cc"
    );
    return (Uint8 *)res;
}

static int best_prescale(int freq)
{
    int i, best_idx = 0, min_diff = 0x7FFFFFFF, diff;
    for (i = 0; falcon_freq_table[i].freq != 0; i++) {
        diff = freq - falcon_freq_table[i].freq;
        if (diff < 0) diff = -diff;
        if (diff <= min_diff) {
            min_diff = diff;
            best_idx = i;
        }
    }
    return falcon_freq_table[best_idx].prescale;
}

static void mint_audio_open_hw(SDL_AudioDevice *device, SDL_AudioSpec *spec)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    int mode;

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
        SDL_SetError("ATARI audio: out of ST-RAM (%d bytes)", hidden->total_size);
        return;
    }

    hidden->buffer_end = hidden->buffer_base + hidden->total_size;
    SDL_memset(hidden->buffer_base, device->spec.silence, hidden->total_size);
    hidden->current_fill_ptr = hidden->buffer_base;
    hidden->playing = 0;

    Locksnd();
    Sndstatus(SND_RESET);
    Buffoper(0);
    Devconnect(DMAPLAY, DAC, CLK25M, best_prescale(spec->freq), NO_SHAKE);
    Setmode(mode);
    Settracks(0, 0);
    Setmontracks(0);
}

static void mint_audio_start_hw(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    if (!hidden->playing) {
        Setbuffer(SR_PLAY, hidden->buffer_base, hidden->buffer_end);
        hidden->current_fill_ptr = hidden->buffer_base;
        hidden->playing = 1;
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

/* -------------------------------------------------------------------------
 * SDL audio interface
 * ---------------------------------------------------------------------- */

static void ATARI_CloseDevice(SDL_AudioDevice *device)
{
    if (device->hidden) {
        mint_audio_stop_hw(device);
        mint_audio_close_hw(device);
        SDL_free(device->hidden);
        device->hidden = NULL;
    }
}

static void ATARI_WaitDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;
    const int  safe_dist  = hidden->chunk_size * 2;
    const int  total      = hidden->total_size;
    Uint32     fill_offset;

    fill_offset = (Uint32)(hidden->current_fill_ptr - hidden->buffer_base);

    while (hidden->playing) {

        dma_offset = (Uint32)(read_dma_pos() - hidden->buffer_base);
        distance   = (int32_t)dma_offset - (int32_t)fill_offset;

        /* distance represents the FREE SPACE in the circular buffer */
        if (distance < 0) distance += total;
        if (distance >= safe_dist) break;
        SDL_Delay(1); /* Sleep briefly to yield the bus and avoid contention */
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

    /* Handle format conversion with bulk-movem loops */
    if (hidden->swap_needed)
        swap_audio_bytes_asm(filled_chunk, hidden->chunk_size);
    else if (hidden->xor_needed)
        convert_sign_bit_asm(filled_chunk, hidden->chunk_size);

    /* Move fill pointer to the next chunk */
    hidden->current_fill_ptr += hidden->chunk_size;
    if (hidden->current_fill_ptr >= hidden->buffer_end)
        hidden->current_fill_ptr = hidden->buffer_base;
}

static int ATARI_OpenDevice(SDL_AudioDevice *device, const char *devname)
{
    (void)devname;
    device->hidden = (struct SDL_PrivateAudioData *)SDL_malloc(sizeof(*device->hidden));
    if (!device->hidden) return SDL_OutOfMemory();
    SDL_memset(device->hidden, 0, sizeof(*device->hidden));

    mint_audio_open_hw(device, &device->spec);
    if (!device->hidden->buffer_base) {
        SDL_free(device->hidden);
        device->hidden = NULL;
        return -1;
    }
    mint_audio_start_hw(device);
    return 0;
}

static SDL_bool ATARI_Init(SDL_AudioDriverImpl *impl)
{
    long cookie;
    if (Getcookie(C__SND, &cookie) != C_FOUND) return SDL_FALSE;
    if (!(cookie & (SND_16BIT | SND_8BIT))) return SDL_FALSE;

    impl->OpenDevice                 = ATARI_OpenDevice;
    impl->CloseDevice                = ATARI_CloseDevice;
    impl->WaitDevice                 = ATARI_WaitDevice;
    impl->GetDeviceBuf               = ATARI_GetDeviceBuf;
    impl->PlayDevice                 = ATARI_PlayDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->ProvidesOwnCallbackThread  = SDL_FALSE;
    impl->HasCaptureSupport          = SDL_FALSE;

    return SDL_TRUE;
}

AudioBootStrap ATARIAUDIO_bootstrap = {
    "mint_xbios",
    "Atari XBIOS Audio (Fully Optimized)",
    ATARI_Init,
    SDL_FALSE
};