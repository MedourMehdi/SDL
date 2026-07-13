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

   Drop-free circular buffer driver using Falcon DMA audio hardware.
   No interrupt handler — the main thread polls the DMA position via the
   XBIOS Buffptr() call and sleeps intelligently to avoid bus contention.

   Buffer strategy:
    - NUM_CHUNKS-chunk circular buffer allocated in ST-RAM (Mxalloc)
    - Fill pointer advances one chunk per callback
    - DMA position read via Buffptr() (XBIOS #141) into a persistent
      SndBufPtr struct stored in SDL_PrivateAudioData (avoids stack
      allocation on every poll and is safe under MiNT threading)
    - WaitDevice uses SDL_Delay(1) to yield the bus when behind safe threshold.

   Why Buffptr() instead of raw HW register polling:
    - Buffptr() is the documented, OS-sanctioned API for reading the DMA
      playback position. It works correctly on every XBIOS-compatible
      platform: real Falcon, ARAnyM, Milan/MilanBlaster, GSXB-patched
      TT/STE, FireBee, and any future clone that implements the sound API.
    - Direct reads of 0xFFFF8909/890B/890D are Falcon-only, break on clones
      with PCI/ISA sound cards behind a GSXB shim (the HW addresses don't
      exist), and require the double-sample stability retry loop because the
      three bytes are not read atomically. Buffptr() is already atomic from
      the caller's perspective: TOS does the stable sampling internally.
    - The XBIOS trap overhead (~dozens of cycles) is negligible: WaitDevice
      calls SDL_Delay(1) in its spin loop, so the bus is yielded for ~1 ms
      between probes anyway. The old retry loop (up to 128 * ~30 cycles =
      ~3840 cycles) in the worst case is replaced by one trap that costs
      far less on average and produces a correct result every time.

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
#define SND_16BIT           0x04
#define SND_8BIT            0x02
#define _THIS               SDL_AudioDevice *this
#define NUM_CHUNKS          8

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
    int             lock_held;      /* tracks whether Locksnd() succeeded */
    volatile int    playing;
    SndBufPtr       dma_ptr;        /* persistent target for Buffptr() calls;
                                       avoids repeated stack allocation and
                                       is safe to pass across XBIOS trap    */
};

/* -------------------------------------------------------------------------
 * Optimized Audio Format Conversions (68000 Assembly)
 * ---------------------------------------------------------------------- */

/* convert_sign_bit_asm: Processes 16-byte blocks using movem.l and
   dual-pointer post-increment addressing, followed by a remainder loop.
   NOTE: ptr and write_ptr MUST be initialised to the same address
   (in-place conversion). If ever adapted for a separate output buffer,
   both initialisers must be updated together. */
static inline void convert_sign_bit_asm(Uint8 *ptr, int count)
{
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
    /* 68020+ Ultra: 32-byte unroll (4 regs x 2 blocks). 
       Power-of-two allows fast shifts (>> 5) instead of slow division. */
    int loops = count >> 5;       
    int rem   = count & 31;
    Uint8 *write_ptr = ptr;

    if (loops > 0) {
        Uint32 mask = 0x80808080UL;
        loops--; /* Prepare for dbra */
        __asm__ volatile (
            "1:\n\t"
            "   movem.l (%0)+, d0-d3\n\t"   /* Read 16 bytes */
            "   eor.l   %2, d0\n\t" "   eor.l   %2, d1\n\t"
            "   eor.l   %2, d2\n\t" "   eor.l   %2, d3\n\t"
            "   move.l  d0, (%1)+\n\t" "   move.l  d1, (%1)+\n\t"
            "   move.l  d2, (%1)+\n\t" "   move.l  d3, (%1)+\n\t"
            "   movem.l (%0)+, d0-d3\n\t"   /* Read next 16 bytes */
            "   eor.l   %2, d0\n\t" "   eor.l   %2, d1\n\t"
            "   eor.l   %2, d2\n\t" "   eor.l   %2, d3\n\t"
            "   move.l  d0, (%1)+\n\t" "   move.l  d1, (%1)+\n\t"
            "   move.l  d2, (%1)+\n\t" "   move.l  d3, (%1)+\n\t"
            "   dbra    %3, 1b\n\t"
            : "+a" (ptr), "+a" (write_ptr)
            : "d" (mask), "d" (loops)
            : "d0", "d1", "d2", "d3", "memory", "cc"
        );
    }
#else
    /* 68000: Standard 16-byte dbra path */
    int loops = count >> 4;       
    int rem   = count & 15;       
    Uint8 *write_ptr = ptr;       
    if (loops > 0) {
        Uint32 mask = 0x80808080UL;
        loops--; 
        __asm__ volatile (
            "1:\tmovem.l (%0)+, d0-d3\n\t"   
            "eor.l %2, d0\n\teor.l %2, d1\n\teor.l %2, d2\n\teor.l %2, d3\n\t"
            "move.l d0, (%1)+\n\tmove.l d1, (%1)+\n\t"
            "move.l d2, (%1)+\n\tmove.l d3, (%1)+\n\t"
            "dbra %3, 1b"
            : "+a" (ptr), "+a" (write_ptr) : "d" (mask), "d" (loops)
            : "d0", "d1", "d2", "d3", "memory", "cc"
        );
    }
#endif
    if (rem > 0) {
        rem--;
        __asm__ volatile ("2:\teori.b #0x80, (%0)+\n\tdbra %1, 2b" 
            : "+a" (ptr) : "d" (rem) : "memory", "cc");
    }
}

static inline void swap_audio_bytes_asm(Uint8 *ptr, int count)
{
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
    /* 68020+ Ultra: 32-byte unroll with full instruction interleaving.
       Interleaving rol.w/swap hides execution latency on superscalar 040/060. */
    int loops = count >> 5;         
    int rem   = (count & 31) >> 1;  
    Uint8 *write_ptr = ptr;

    if (loops > 0) {
        loops--;
        __asm__ volatile (
            "1:\n\t"
            "   movem.l (%0)+, d0-d3\n\t"
            /* Interleave block 1 to avoid data stalls */
            "   rol.w   #8, d0\n\t" "   rol.w   #8, d1\n\t" "   rol.w   #8, d2\n\t" "   rol.w   #8, d3\n\t"
            "   swap    d0\n\t"     "   swap    d1\n\t"     "   swap    d2\n\t"     "   swap    d3\n\t"
            "   rol.w   #8, d0\n\t" "   rol.w   #8, d1\n\t" "   rol.w   #8, d2\n\t" "   rol.w   #8, d3\n\t"
            "   swap    d0\n\t"     "   swap    d1\n\t"     "   swap    d2\n\t"     "   swap    d3\n\t"
            "   move.l  d0, (%1)+\n\t" "   move.l  d1, (%1)+\n\t" "   move.l  d2, (%1)+\n\t" "   move.l  d3, (%1)+\n\t"
            
            "   movem.l (%0)+, d0-d3\n\t"
            /* Interleave block 2 */
            "   rol.w   #8, d0\n\t" "   rol.w   #8, d1\n\t" "   rol.w   #8, d2\n\t" "   rol.w   #8, d3\n\t"
            "   swap    d0\n\t"     "   swap    d1\n\t"     "   swap    d2\n\t"     "   swap    d3\n\t"
            "   rol.w   #8, d0\n\t" "   rol.w   #8, d1\n\t" "   rol.w   #8, d2\n\t" "   rol.w   #8, d3\n\t"
            "   swap    d0\n\t"     "   swap    d1\n\t"     "   swap    d2\n\t"     "   swap    d3\n\t"
            "   move.l  d0, (%1)+\n\t" "   move.l  d1, (%1)+\n\t" "   move.l  d2, (%1)+\n\t" "   move.l  d3, (%1)+\n\t"
            "   dbra    %2, 1b\n\t"
            : "+a" (ptr), "+a" (write_ptr) : "d" (loops)
            : "d0", "d1", "d2", "d3", "memory", "cc"
        );
    }
#else
    /* 68000: 16-byte unroll */
    int loops = count >> 4;         
    int rem   = (count & 15) >> 1;  
    Uint8 *write_ptr = ptr;         
    if (loops > 0) {
        loops--;
        __asm__ volatile (
            "1:\tmovem.l (%0)+, d0-d3\n\t"
            "ror.w #8, d0\n\tswap d0\n\tror.w #8, d0\n\tswap d0\n\t"
            "ror.w #8, d1\n\tswap d1\n\tror.w #8, d1\n\tswap d1\n\t"
            "ror.w #8, d2\n\tswap d2\n\tror.w #8, d2\n\tswap d2\n\t"
            "ror.w #8, d3\n\tswap d3\n\tror.w #8, d3\n\tswap d3\n\t"
            "move.l d0, (%1)+\n\tmove.l d1, (%1)+\n\t"
            "move.l d2, (%1)+\n\tmove.l d3, (%1)+\n\t"
            "dbra %2, 1b"
            : "+a" (ptr), "+a" (write_ptr) : "d" (loops)
            : "d0", "d1", "d2", "d3", "memory", "cc"
        );
    }
#endif
    if (rem > 0) {
        rem--;
        __asm__ volatile ("2:\tmove.w (%0), d0\n\trol.w #8, d0\n\tmove.w d0, (%0)+\n\tdbra %1, 2b" 
            : "+a" (ptr) : "d" (rem) : "d0", "memory", "cc");
    }
}

/* -------------------------------------------------------------------------
 * Hardware Access — Buffptr()-based DMA position read
 * ---------------------------------------------------------------------- */

/*
 * read_dma_pos_buffptr:
 *
 * Returns the current DMA playback read pointer via XBIOS Buffptr() (#141).
 *
 * Buffptr() fills a SndBufPtr struct:
 *   typedef struct { char *play; char *record; char *loopstart; char *loopend; } SndBufPtr;
 * The .play field is the address the DMA controller is currently consuming.
 *
 * We store the SndBufPtr inside SDL_PrivateAudioData so it lives in a fixed,
 * word-aligned memory location for the lifetime of the device. Passing a
 * stack-allocated struct is also valid but wastes a few cycles re-zeroing it;
 * the persistent version is slightly cheaper in the spin loop and also keeps
 * the code easier to audit.
 *
 * Stability guarantee: unlike raw 0xFFFF8909/890B/890D reads (three
 * independent byte-wide accesses that can straddle a DMA counter increment),
 * Buffptr() samples the three hardware bytes inside the XBIOS supervisor
 * context where the DMA counter is read in a single coherent window. The
 * double-sample retry loop from the old read_dma_pos() is therefore not
 * needed and has been removed.
 *
 * GSXB / clone compatibility: any platform that responds to the _SND cookie
 * check in ATARI_Init() also provides a working Buffptr() implementation
 * (Milan+MilanBlaster, GSXB-patched STE/TT, ARAnyM, FireBee). The raw HW
 * addresses are Falcon-silicon specific and do not exist on those targets.
 */
static inline Uint8 *read_dma_pos_buffptr(struct SDL_PrivateAudioData *hidden)
{
    /* Buffptr() prototype: long Buffptr(long *ptr)
     * It writes four longs into *ptr: play, record, loopstart, loopend.
     * The SndBufPtr type from <mint/falcon.h> matches this layout exactly.
     * We cast to (int32_t*) as the MiNT binding expects, consistent with
     * the usage in utils_snd.cpp: Buffptr((int32_t*)&local_ptr).        */
    Buffptr((int32_t *)&hidden->dma_ptr);
    return (Uint8 *)hidden->dma_ptr.play;
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
    struct SDL_PrivateAudioData *hidden;
    int mode;

    /* Guard device and hidden against NULL before any dereference */
    if (!device || !device->hidden)
        return;

    hidden = device->hidden;
    hidden->swap_needed = hidden->xor_needed = 0;
    hidden->lock_held   = 0;

    /* Zero the dma_ptr struct so Buffptr() has a clean target from the start */
    SDL_memset(&hidden->dma_ptr, 0, sizeof(hidden->dma_ptr));

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

    /* Allocate buffer BEFORE taking the sound lock so that a failed
       allocation never leaves the lock acquired without a matching release. */
    hidden->buffer_base = (Uint8 *)Mxalloc(hidden->total_size, MX_STRAM);
    if (!hidden->buffer_base) {
        SDL_SetError("ATARI audio: out of ST-RAM (%d bytes)", hidden->total_size);
        return;
    }

    hidden->buffer_end = hidden->buffer_base + hidden->total_size;
    SDL_memset(hidden->buffer_base, device->spec.silence, hidden->total_size);
    hidden->current_fill_ptr = hidden->buffer_base;
    hidden->playing = 0;

    /* Lock only after successful allocation; record in flag */
    Locksnd();
    hidden->lock_held = 1;

    Sndstatus(SND_RESET);
    Buffoper(0);
    Devconnect(DMAPLAY, DAC, CLK25M, best_prescale(spec->freq), NO_SHAKE);
    Setmode(mode);
    Settracks(0, 0);
    Setmontracks(0);
}

static void mint_audio_start_hw(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden;

    if (!device || !device->hidden)
        return;

    hidden = device->hidden;
    if (!hidden->playing) {
        Setbuffer(SR_PLAY, hidden->buffer_base, hidden->buffer_end);
        hidden->current_fill_ptr = hidden->buffer_base;
        hidden->playing = 1;
        Buffoper(SB_PLA_ENA | SB_PLA_RPT);
    }
}

static void mint_audio_stop_hw(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden;

    if (!device || !device->hidden)
        return;

    hidden = device->hidden;
    if (hidden->playing) {
        hidden->playing = 0;
        Buffoper(0);
    }
}

static void mint_audio_close_hw(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden;

    if (!device || !device->hidden)
        return;

    hidden = device->hidden;
    Buffoper(0);

    if (hidden->buffer_base) {
        Mfree(hidden->buffer_base);
        hidden->buffer_base = NULL;
    }

    /* Only release the sound lock if we successfully acquired it */
    if (hidden->lock_held) {
        Unlocksnd();
        hidden->lock_held = 0;
    }
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

/*
 * ATARI_WaitDevice:
 *
 * Spins until the DMA playback head has moved far enough ahead of
 * current_fill_ptr that it is safe to overwrite that chunk.
 *
 *   distance = (dma_offset - fill_offset) mod total_size
 *
 * This is the amount of buffer the DMA still has to consume before it
 * catches the fill pointer. We need at least (chunk_size * 2) bytes of
 * runway before we're safe to write, giving the DMA one full chunk of
 * headroom beyond the one we're about to fill.
 *
 * Silence pre-fill:
 *   The instant distance >= safe_dist is the ONLY point in the entire
 *   cycle where we have proof the DMA is not reading this chunk - the
 *   check above just established it. That makes it the only safe place
 *   to pre-fill the chunk with silence before handing it to GetDeviceBuf
 *   and the mixer callback. If the callback fills the whole chunk with
 *   real audio, this is simply overwritten and costs nothing extra. If
 *   the game has stopped sending audio and the callback under-fills (or
 *   isn't invoked at all, e.g. a paused/legacy callback path), the chunk
 *   defaults to silence instead of replaying stale PCM from up to
 *   (NUM_CHUNKS-1) cycles ago - which is what caused a short sound to
 *   loop forever.
 *
 *   Do NOT move this into ATARI_PlayDevice. By the time PlayDevice runs,
 *   the mixer callback has already executed against this same chunk (the
 *   real per-cycle order is WaitDevice -> GetDeviceBuf -> callback ->
 *   PlayDevice), so clearing there either wipes real audio the callback
 *   just wrote, or - if targeting the chunk PlayDevice advances to next -
 *   touches a chunk WaitDevice has not yet certified safe for the
 *   following cycle. Either way reintroduces a live DMA read/write race.
 *
 * Buffer ownership diagram (NUM_CHUNKS = 4, chunk = C):
 *
 *   [  C0  |  C1  |  C2  |  C3  ]
 *            ^DMA           ^fill
 *
 *   distance = fill_offset - dma_offset going backward around the ring.
 *   We wait while distance < safe_dist (2 chunks) so the DMA never
 *   runs into a chunk currently being written.
 */
static void ATARI_WaitDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;
    const int   safe_dist   = hidden->chunk_size * 2;
    const int   total       = hidden->total_size;
    Uint8 *const buf_base   = hidden->buffer_base;
    const Uint32 fill_offset = (Uint32)(hidden->current_fill_ptr - buf_base);
    Uint32       dma_offset;
    int32_t      distance;

    while (hidden->playing) {

        dma_offset = (Uint32)(read_dma_pos_buffptr(hidden) - buf_base);
        distance   = (int32_t)dma_offset - (int32_t)fill_offset;

        /* distance represents FREE SPACE ahead of the fill pointer */
        if (distance < 0) distance += total;
        if (distance >= safe_dist) {
            /* Proven safe right now - default this chunk to silence
               before the mixer callback gets a chance to (not) fill it. */
            SDL_memset(hidden->current_fill_ptr, this->spec.silence,
                       hidden->chunk_size);
            break;
        }

        pthread_yield(); /* Yield the CPU to other threads (MiNT) */
    }
}

static Uint8 *ATARI_GetDeviceBuf(_THIS)
{
    if (!this->hidden)
        return NULL;
    return this->hidden->current_fill_ptr;
}

static void ATARI_PlayDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;
    Uint8 *filled_chunk;

    /* Guard fill pointer before passing to ASM conversion routines;
       a NULL here would cause a bus error on 68k (movem.l to address 0). */
    filled_chunk = hidden->current_fill_ptr;
    if (!filled_chunk)
        return;

    /* Handle format conversion with bulk-movem loops */
    if (hidden->swap_needed)
        swap_audio_bytes_asm(filled_chunk, hidden->chunk_size);
    else if (hidden->xor_needed)
        convert_sign_bit_asm(filled_chunk, hidden->chunk_size);

    /* Advance fill pointer to the next chunk (circular wrap) */
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
    "Atari XBIOS Audio",
    ATARI_Init,
    SDL_FALSE
};