| ============================================================================
| Simple DirectMedia Layer
| Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>
| ============================================================================
| This software is provided 'as-is', without any express or implied
| warranty.  In no event will the authors be held liable for any damages
| arising from the use of this software.
|
| Permission is granted to anyone to use this software for any purpose,
| including commercial applications, and to alter it and redistribute it
| freely, subject to the following restrictions:
|
| 1. The origin of this software must not be misrepresented; you must not
|    claim that you wrote the original software. If you use this software
|    in a product, an acknowledgment in the product documentation would be
|    appreciated but is not required.
| 2. Altered source versions must be plainly marked as such, and must not
|    be misrepresented as being the original software.
| 3. This notice may not be removed or altered from any source distribution.

| ============================================================================
| SDL_gemchecksum.S – 68000 optimised row-checksum routines
| Medour Mehdi - 2026
| Architecture: Motorola 68000 / Atari ST-TT-Falcon
|
| Used by the SDL_GEM_DIRTY_RECT fast-change detection system to avoid
| blitting unchanged rows to the VDI framebuffer.
|
| Contains:
|   - Atari_CalculateRowChecksum        – generic, arbitrary row length
|   - Atari_CalculateRowChecksum_320    – fixed 320 bytes (ST low)
|   - Atari_CalculateRowChecksum_640    – fixed 640 bytes (TT/Falcon)
|   - Atari_CalculateRowChecksumsBlock  – batch, processes N rows at once
|
| Algorithm: 16-bit word accumulator, folded to 8 bits via XOR.
| ABI: d2-d7 / a2-a6 callee-saved.
| ============================================================================

    .text


| ============================================================================
| Atari_CalculateRowChecksum
| Generic row checksum for arbitrary length
|
| C prototype:
|   Uint8 Atari_CalculateRowChecksum(const Uint8 *row, int len);
|
| Parameters: 4(sp) = row pointer, 8(sp) = length in bytes
| Returns:    d0.b = 8-bit folded checksum
|
| Processing: 16-byte chunks (8 words) + 0-15 byte remainder
| ============================================================================

    .globl  _Atari_CalculateRowChecksum

_Atari_CalculateRowChecksum:
    movem.l d2-d6/a2,-(sp)
    
    move.l  28(sp),a0         | Correct: parameter 1 (row pointer)
    move.l  32(sp),d2         | Correct: parameter 2 (len)
    move.l  d2,d0
    moveq   #0,d1
    
    lsr.l   #4,d2
    beq     .Lgeneric_remainder
    subq.l  #1,d2

.Lgeneric_chunk_loop:
    movem.w (a0)+,d3-d6
    add.w   d3,d1
    add.w   d4,d1
    add.w   d5,d1
    add.w   d6,d1
    
    movem.w (a0)+,d3-d6
    add.w   d3,d1
    add.w   d4,d1
    add.w   d5,d1
    add.w   d6,d1
    
    dbra    d2,.Lgeneric_chunk_loop

.Lgeneric_remainder:
    and.l   #15,d0
    beq     .Lgeneric_fold
    move.l  a0,a1
    add.l   d0,a1
    bra     .Lgeneric_rem_test

.Lgeneric_rem_loop:
    move.b  (a0)+,d3
    lsl.w   #8,d3
    add.w   d3,d1

.Lgeneric_rem_test:
    cmp.l   a0,a1
    bgt     .Lgeneric_rem_loop

.Lgeneric_fold:
    move.w  d1,d2
    lsr.w   #8,d2
    eor.w   d2,d1
    move.b  d1,d0
    
    movem.l (sp)+,d2-d6/a2
    rts


| ============================================================================
| Atari_CalculateRowChecksum_320
| Optimized for ST low resolution (320 bytes per row)
|
| Structure: 5 outer iterations x 8 inner iterations x 8 bytes
| Total: 5 x 8 x 8 = 320 bytes
| ============================================================================

    .globl  _Atari_CalculateRowChecksum_320

_Atari_CalculateRowChecksum_320:
    movem.l d2-d7,-(sp)
    
    move.l  28(sp),a0
    moveq   #0,d1
    moveq   #4,d2

.L320_outer:
    moveq   #7,d3

.L320_inner:
    movem.w (a0)+,d4-d7
    add.w   d4,d1
    add.w   d5,d1
    add.w   d6,d1
    add.w   d7,d1
    
    dbra    d3,.L320_inner
    dbra    d2,.L320_outer
    
    move.w  d1,d2
    lsr.w   #8,d2
    eor.w   d2,d1
    move.b  d1,d0
    
    movem.l (sp)+,d2-d7
    rts


| ============================================================================
| Atari_CalculateRowChecksum_640
| Optimized for TT/Falcon high resolution (640 bytes per row)
|
| Structure: 10 outer iterations x 8 inner iterations x 8 bytes
| Total: 10 x 8 x 8 = 640 bytes
| ============================================================================

    .globl  _Atari_CalculateRowChecksum_640

_Atari_CalculateRowChecksum_640:
    movem.l d2-d7,-(sp)
    
    move.l  28(sp),a0
    moveq   #0,d1
    moveq   #9,d2

.L640_outer:
    moveq   #7,d3

.L640_inner:
    movem.w (a0)+,d4-d7
    add.w   d4,d1
    add.w   d5,d1
    add.w   d6,d1
    add.w   d7,d1
    
    dbra    d3,.L640_inner
    dbra    d2,.L640_outer
    
    move.w  d1,d2
    lsr.w   #8,d2
    eor.w   d2,d1
    move.b  d1,d0
    
    movem.l (sp)+,d2-d7
    rts


| ============================================================================
| Atari_CalculateRowChecksumsBlock
| Batch checksum calculation for multiple rows
|
| C prototype:
|   void Atari_CalculateRowChecksumsBlock(
|       const Uint8 *buffer, Uint8 *checksums,
|       int num_rows, int pitch, int bytes_per_row);
|
| Parameters: 40(sp)=buffer, 44(sp)=checksums, 48(sp)=num_rows
|             52(sp)=pitch, 56(sp)=bytes_per_row
|
| Processing: 8-byte chunks per row + 0-7 byte remainder
| ============================================================================

    .globl  _Atari_CalculateRowChecksumsBlock

_Atari_CalculateRowChecksumsBlock:
    movem.l d2-d7/a2-a4,-(sp)
    
    move.l  40(sp),a0
    move.l  44(sp),a1
    move.l  48(sp),d6
    move.l  52(sp),a3
    move.l  56(sp),d5
    
    subq.l  #1,d6
    bmi     .Lblock_done

.Lblock_row_loop:
    move.l  a0,a2
    moveq   #0,d1
    move.l  d5,d4
    lsr.l   #3,d4
    beq     .Lblock_remainder
    subq.l  #1,d4

.Lblock_chunk_loop:
    movem.w (a2)+,d2-d3         | first 4 bytes
    add.w   d2,d1
    add.w   d3,d1
    movem.w (a2)+,d2-d3         | second 4 bytes
    add.w   d2,d1
    add.w   d3,d1
    dbra    d4,.Lblock_chunk_loop

.Lblock_remainder:
    move.l  d5,d7
    and.l   #7,d7
    beq     .Lblock_store
    move.l  a2,a4
    add.l   d7,a4
    bra     .Lblock_rem_test

.Lblock_rem_loop:
    move.b  (a2)+,d2
    lsl.w   #8,d2
    add.w   d2,d1

.Lblock_rem_test:
    cmp.l   a2,a4
    bgt     .Lblock_rem_loop

.Lblock_store:
    move.w  d1,d2
    lsr.w   #8,d2
    eor.w   d2,d1
    move.b  d1,(a1)+
    
    add.l   a3,a0
    dbra    d6,.Lblock_row_loop

.Lblock_done:
    movem.l (sp)+,d2-d7/a2-a4
    rts