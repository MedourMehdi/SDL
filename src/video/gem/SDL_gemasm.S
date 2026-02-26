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
| SDL_gemasm.S – 68000 hand-optimised C2P and pixel-format converters
| Medour Mehdi - 2026
| Architecture: Motorola 68000 / Atari ST-TT-Falcon
|
| Contains:
|   - LUT + C2P routines for 1/2/4/8 bitplanes (primary code path)
|   - RGB332 → RGB565 and RGB332 → ARGB8888 LUT converters
|   - BGRA8888 → RGB332 converter
|   - Atari_BlitFast_asm – aligned 32-bit row copy for TrueColor modes
|   - Shared c2p_bit_mask table (16 words, referenced by all C2P routines)
|
| ABI: d2-d7 / a2-a6 callee-saved (split movem, parameters at offset 48).
| All routines require 68000 only – no 68020 addressing modes used.
| ============================================================================

    .text

| ----------------------------------------------------------------------------
| Table des masques binaires – GLOBALE
| ----------------------------------------------------------------------------
    .globl  _c2p_bit_mask
_c2p_bit_mask:
    .dc.w   0x8000,0x4000,0x2000,0x1000
    .dc.w   0x0800,0x0400,0x0200,0x0100
    .dc.w   0x0080,0x0040,0x0020,0x0010
    .dc.w   0x0008,0x0004,0x0002,0x0001

| ============================================================================
| Atari_C2P_8to4_LUT_asm - Combined LUT remap + C2P
| ============================================================================
    .globl  _Atari_C2P_8to4_LUT_asm

_Atari_C2P_8to4_LUT_asm:
    movem.l d2-d7/a2-a6,-(sp)
    
    move.l  48(sp),a0           | src
    move.l  52(sp),a1           | dst
    move.l  56(sp),d4           | width
    move.l  60(sp),d5           | height
    move.l  64(sp),d2           | src_pitch
    move.l  68(sp),d3           | dst_pitch
    move.l  72(sp),a6           | lut
    
    move.l  d2,d7
    sub.l   d4,d7               | src_skip
    move.l  d3,a3
    move.l  d4,d0
    lsr.l   #4,d0
    lsl.l   #3,d0
    sub.l   d0,a3               | dst_skip
    
    move.l  d4,d6
    lsr.l   #4,d6               | blocks per row
    
    subq.l  #1,d5
    bmi.s   .lut_exit

.lut_row_loop:
    move.l  d6,d3
    subq.l  #1,d3

.lut_block_loop:
    clr.l   (a1)+
    clr.l   (a1)+
    subq.l  #8,a1
    lea     _c2p_bit_mask,a5
    moveq   #15,d2

.lut_pixel_loop:
    moveq   #0,d0
    move.b  (a0)+,d0
    move.b  (a6,d0.w),d0
    move.w  (a5)+,d1
    
    btst    #0,d0
    beq.s   .p0
    or.w    d1,0(a1)
.p0:btst    #1,d0
    beq.s   .p1
    or.w    d1,2(a1)
.p1:btst    #2,d0
    beq.s   .p2
    or.w    d1,4(a1)
.p2:btst    #3,d0
    beq.s   .p3
    or.w    d1,6(a1)
.p3:dbra    d2,.lut_pixel_loop
    
    addq.l  #8,a1
    dbra    d3,.lut_block_loop
    
    add.l   d7,a0
    add.l   a3,a1
    dbra    d5,.lut_row_loop

.lut_exit:
    movem.l (sp)+,d2-d7/a2-a6
    rts

| ============================================================================
| Atari_C2P_8to1_LUT_asm - Combined LUT remap + C2P for 1 plane (mono)
| ============================================================================
    .globl  _Atari_C2P_8to1_LUT_asm

_Atari_C2P_8to1_LUT_asm:
    movem.l d2-d7/a2-a6,-(sp)
    
    move.l  48(sp),a0           | src
    move.l  52(sp),a1           | dst
    move.l  56(sp),d4           | width
    move.l  60(sp),d5           | height
    move.l  64(sp),d2           | src_pitch
    move.l  68(sp),d3           | dst_pitch
    move.l  72(sp),a6           | lut
    
    move.l  d2,d7
    sub.l   d4,d7               | src_skip
    move.l  d3,a3
    move.l  d4,d0
    lsr.l   #4,d0               | width / 16 = blocks
    lsl.l   #1,d0               | blocks * 2 bytes (1 plane × 2)
    sub.l   d0,a3               | dst_skip
    
    move.l  d4,d6
    lsr.l   #4,d6               | blocks per row
    
    subq.l  #1,d5
    bmi.s   .lut1_exit

.lut1_row_loop:
    move.l  d6,d3
    subq.l  #1,d3

.lut1_block_loop:
    clr.w   (a1)                | Clear 1 plane word
    lea     _c2p_bit_mask,a5
    moveq   #15,d2

.lut1_pixel_loop:
    moveq   #0,d0
    move.b  (a0)+,d0
    move.b  (a6,d0.w),d0        | LUT remap
    move.w  (a5)+,d1            | Bit mask
    
    btst    #0,d0
    beq.s   .lut1_p0
    or.w    d1,0(a1)
.lut1_p0:
    dbra    d2,.lut1_pixel_loop
    
    addq.l  #2,a1               | Next plane word
    dbra    d3,.lut1_block_loop
    
    add.l   d7,a0               | src += src_skip
    add.l   a3,a1               | dst += dst_skip
    dbra    d5,.lut1_row_loop

.lut1_exit:
    movem.l (sp)+,d2-d7/a2-a6
    rts

| ============================================================================
| Atari_C2P_8to2_LUT_asm - Combined LUT remap + C2P for 2 planes (4 colors)
| ============================================================================
    .globl  _Atari_C2P_8to2_LUT_asm

_Atari_C2P_8to2_LUT_asm:
    movem.l d2-d7/a2-a6,-(sp)
    
    move.l  48(sp),a0           | src
    move.l  52(sp),a1           | dst
    move.l  56(sp),d4           | width
    move.l  60(sp),d5           | height
    move.l  64(sp),d2           | src_pitch
    move.l  68(sp),d3           | dst_pitch
    move.l  72(sp),a6           | lut
    
    move.l  d2,d7
    sub.l   d4,d7               | src_skip
    move.l  d3,a3
    move.l  d4,d0
    lsr.l   #4,d0               | width / 16 = blocks
    lsl.l   #2,d0               | blocks * 4 bytes (2 planes × 2)
    sub.l   d0,a3               | dst_skip
    
    move.l  d4,d6
    lsr.l   #4,d6               | blocks per row
    
    subq.l  #1,d5
    bmi.s   .lut2_exit

.lut2_row_loop:
    move.l  d6,d3
    subq.l  #1,d3

.lut2_block_loop:
    clr.l   (a1)                | Clear 2 planes (4 bytes)
    lea     _c2p_bit_mask,a5
    moveq   #15,d2

.lut2_pixel_loop:
    moveq   #0,d0
    move.b  (a0)+,d0
    move.b  (a6,d0.w),d0        | LUT remap
    move.w  (a5)+,d1            | Bit mask
    
    btst    #0,d0
    beq.s   .lut2_p0
    or.w    d1,0(a1)
.lut2_p0:
    btst    #1,d0
    beq.s   .lut2_p1
    or.w    d1,2(a1)
.lut2_p1:
    dbra    d2,.lut2_pixel_loop
    
    addq.l  #4,a1               | Next 2-plane block
    dbra    d3,.lut2_block_loop
    
    add.l   d7,a0               | src += src_skip
    add.l   a3,a1               | dst += dst_skip
    dbra    d5,.lut2_row_loop

.lut2_exit:
    movem.l (sp)+,d2-d7/a2-a6
    rts

| ============================================================================
| Atari_C2P_8to8_LUT_asm - Combined LUT remap + C2P for 8 planes (256 colors)
| ============================================================================
    .globl  _Atari_C2P_8to8_LUT_asm

_Atari_C2P_8to8_LUT_asm:
    movem.l d2-d7/a2-a6,-(sp)
    
    move.l  48(sp),a0           | src
    move.l  52(sp),a1           | dst
    move.l  56(sp),d4           | width
    move.l  60(sp),d5           | height
    move.l  64(sp),d2           | src_pitch
    move.l  68(sp),d3           | dst_pitch
    move.l  72(sp),a6           | lut
    
    move.l  d2,d7
    sub.l   d4,d7               | src_skip
    move.l  d3,a3
    move.l  d4,d0
    lsr.l   #4,d0               | width / 16 = blocks
    lsl.l   #4,d0               | blocks * 16 bytes (8 planes × 2)
    sub.l   d0,a3               | dst_skip
    
    move.l  d4,d6
    lsr.l   #4,d6               | blocks per row
    
    subq.l  #1,d5
    bmi   .lut8_exit

.lut8_row_loop:
    move.l  d6,d3
    subq.l  #1,d3

.lut8_block_loop:
    clr.l   (a1)+               | Clear planes 0-1
    clr.l   (a1)+               | Clear planes 2-3
    clr.l   (a1)+               | Clear planes 4-5
    clr.l   (a1)+               | Clear planes 6-7
    lea     -16(a1),a1          | Back to start of block
    lea     _c2p_bit_mask,a5
    moveq   #15,d2

.lut8_pixel_loop:
    moveq   #0,d0
    move.b  (a0)+,d0
    move.b  (a6,d0.w),d0        | LUT remap
    move.w  (a5)+,d1            | Bit mask
    
    btst    #0,d0
    beq.s   .lut8_p0
    or.w    d1,0(a1)
.lut8_p0:
    btst    #1,d0
    beq.s   .lut8_p1
    or.w    d1,2(a1)
.lut8_p1:
    btst    #2,d0
    beq.s   .lut8_p2
    or.w    d1,4(a1)
.lut8_p2:
    btst    #3,d0
    beq.s   .lut8_p3
    or.w    d1,6(a1)
.lut8_p3:
    btst    #4,d0
    beq.s   .lut8_p4
    or.w    d1,8(a1)
.lut8_p4:
    btst    #5,d0
    beq.s   .lut8_p5
    or.w    d1,10(a1)
.lut8_p5:
    btst    #6,d0
    beq.s   .lut8_p6
    or.w    d1,12(a1)
.lut8_p6:
    btst    #7,d0
    beq.s   .lut8_p7
    or.w    d1,14(a1)
.lut8_p7:
    dbra    d2,.lut8_pixel_loop
    
    lea     16(a1),a1           | Next 8-plane block
    dbra    d3,.lut8_block_loop
    
    add.l   d7,a0               | src += src_skip
    add.l   a3,a1               | dst += dst_skip
    dbra    d5,.lut8_row_loop

.lut8_exit:
    movem.l (sp)+,d2-d7/a2-a6
    rts

| ============================================================================
| Atari_ConvertRGB332toRGB565_asm - 68000-optimized LUT converter
| ============================================================================

    .text
    .globl  _Atari_ConvertRGB332toRGB565_asm

_Atari_ConvertRGB332toRGB565_asm:
    movem.l d2-d7/a2-a6,-(sp)

    move.l  48(sp),a0
    move.l  52(sp),a1
    move.l  56(sp),d4
    move.l  60(sp),d5
    move.l  64(sp),d6
    move.l  68(sp),d7

    move.l  d6,a2
    sub.l   d4,a2

    move.l  d4,d0
    lsl.l   #1,d0
    move.l  d7,a3
    sub.l   d0,a3

    lea     _rgb332_to_rgb565_lut,a6

    move.l  d4,d2
    lsr.l   #2,d2
    and.l   #3,d4

    subq.l  #1,d5
    bmi.s   .rgb565_exit

.rgb565_row:
    move.l  d2,d6
    beq.s   .rgb565_rem_only

    subq.l  #1,d6

.rgb565_4px:
    moveq   #0,d0
    move.b  (a0)+,d0
    add.w   d0,d0
    move.w  (a6,d0.w),d1

    moveq   #0,d0
    move.b  (a0)+,d0
    add.w   d0,d0
    move.w  d1,(a1)+
    move.w  (a6,d0.w),d1

    moveq   #0,d0
    move.b  (a0)+,d0
    add.w   d0,d0
    move.w  d1,(a1)+
    move.w  (a6,d0.w),d1

    moveq   #0,d0
    move.b  (a0)+,d0
    add.w   d0,d0
    move.w  d1,(a1)+
    move.w  (a6,d0.w),d1
    move.w  d1,(a1)+

    dbra    d6,.rgb565_4px

.rgb565_rem_only:
    move.l  d4,d6
    beq.s   .rgb565_next
    subq.l  #1,d6

.rgb565_rem:
    moveq   #0,d0
    move.b  (a0)+,d0
    add.w   d0,d0               | d0 = index * 2
    move.w  (a6,d0.w),(a1)+
    dbra    d6,.rgb565_rem

.rgb565_next:
    add.l   a2,a0
    add.l   a3,a1

    dbra    d5,.rgb565_row

.rgb565_exit:
    movem.l (sp)+,d2-d7/a2-a6
    rts

| ============================================================================
| Atari_ConvertBGRA8888toRGB332_asm
|
| C: void Atari_ConvertBGRA8888toRGB332_asm(const Uint8 *src, Uint8 *dst,
|                                           int width, int height,
|                                           int src_pitch, int dst_pitch)
|
| BGRA8888 in memory: [B][G][R][A] — loaded byte by byte, endian-safe.
| Quantize: R3=(R>>5), G3=(G>>5), B2=(B>>6)
| Pack:     (R3<<5) | (G3<<2) | B2
|
| Register save: split movem d2-d7 / a2-a6 → parameters at offset 48.
| d0-d2: pixel components (low byte used only — upper bits harmless)
| d4:    width, d5: height, d6: pixel loop counter
| a0:    src, a1: dst, a2: src_skip (bytes), a3: dst_skip (bytes)
| ============================================================================

    .globl  _Atari_ConvertBGRA8888toRGB332_asm

_Atari_ConvertBGRA8888toRGB332_asm:
    movem.l d2-d7,-(sp)
    movem.l a2-a6,-(sp)

    move.l  48(sp),a0               | a0 = src
    move.l  52(sp),a1               | a1 = dst
    move.l  56(sp),d4               | d4 = width
    move.l  60(sp),d5               | d5 = height
    move.l  64(sp),a2               | a2 = src_pitch (bytes)
    move.l  68(sp),a3               | a3 = dst_pitch (bytes)

    | src_skip = src_pitch - width*4
    move.l  d4,d0
    lsl.l   #2,d0                   | width * 4
    sub.l   d0,a2                   | a2 = src_skip

    | dst_skip = dst_pitch - width
    sub.l   d4,a3                   | a3 = dst_skip

    subq.l  #1,d5
    bmi.s   .bgra_exit

.bgra_row:
    move.l  d4,d6
    subq.l  #1,d6

.bgra_pixel:
    | Load B, G, R — byte by byte. d0-d2 not pre-cleared:
    | move.b loads low byte only; lsr.b/lsl.b/or.b all byte-only ops;
    | garbage in bits 8-31 never reaches move.b (a1)+ output.
    move.b  (a0)+,d0                | d0[7:0] = B
    lsr.b   #6,d0                   | d0 = B >> 6  (2 bits: 0-3)

    move.b  (a0)+,d1                | d1[7:0] = G
    lsr.b   #5,d1
    lsl.b   #2,d1                   | d1 = (G >> 5) << 2

    move.b  (a0)+,d2                | d2[7:0] = R
    lsr.b   #5,d2
    lsl.b   #5,d2                   | d2 = (R >> 5) << 5

    or.b    d1,d2                   | d2 = (R<<5) | (G<<2)
    or.b    d0,d2                   | d2 = (R<<5) | (G<<2) | B
    move.b  d2,(a1)+                | store RGB332 byte

    addq.l  #1,a0                   | skip A byte

    dbra    d6,.bgra_pixel

    add.l   a2,a0                   | src += src_skip
    add.l   a3,a1                   | dst += dst_skip
    dbra    d5,.bgra_row

.bgra_exit:
    movem.l (sp)+,a2-a6
    movem.l (sp)+,d2-d7
    rts

| ============================================================================
| Atari_ConvertARGB8888toRGB332_asm
|
| C: void Atari_ConvertARGB8888toRGB332_asm(const Uint8 *src, Uint8 *dst,
|                                           int width, int height,
|                                           int src_pitch, int dst_pitch)
|
| ARGB8888 in memory: [A][R][G][B] — loaded byte by byte, endian-safe.
| Quantize: R3=(R>>5), G3=(G>>5), B2=(B>>6)
| Pack:     (R3<<5) | (G3<<2) | B2
|
| Register save: split movem d2-d7 / a2-a6 → parameters at offset 48.
| d0-d2: pixel components (low byte used only — upper bits harmless)
| d4:    width, d5: height, d6: pixel loop counter
| a0:    src, a1: dst, a2: src_skip (bytes), a3: dst_skip (bytes)
| ============================================================================

    .globl  _Atari_ConvertARGB8888toRGB332_asm

_Atari_ConvertARGB8888toRGB332_asm:
    movem.l d2-d7,-(sp)
    movem.l a2-a6,-(sp)

    move.l  48(sp),a0               | a0 = src
    move.l  52(sp),a1               | a1 = dst
    move.l  56(sp),d4               | d4 = width
    move.l  60(sp),d5               | d5 = height
    move.l  64(sp),a2               | a2 = src_pitch (bytes)
    move.l  68(sp),a3               | a3 = dst_pitch (bytes)

    | src_skip = src_pitch - width*4
    move.l  d4,d0
    lsl.l   #2,d0                   | width * 4
    sub.l   d0,a2                   | a2 = src_skip

    | dst_skip = dst_pitch - width
    sub.l   d4,a3                   | a3 = dst_skip

    subq.l  #1,d5
    bmi.s   .argb_exit_1

.argb_row_1:
    move.l  d4,d6
    subq.l  #1,d6

.argb_pixel:
    | Memory layout: [A][R][G][B]
    addq.l  #1,a0                   | skip A (byte 0)

    move.b  (a0)+,d2                | d2[7:0] = R (byte 1)
    lsr.b   #5,d2
    lsl.b   #5,d2                   | d2 = (R >> 5) << 5

    move.b  (a0)+,d1                | d1[7:0] = G (byte 2)
    lsr.b   #5,d1
    lsl.b   #2,d1                   | d1 = (G >> 5) << 2

    move.b  (a0)+,d0                | d0[7:0] = B (byte 3)
    lsr.b   #6,d0                   | d0 = B >> 6  (2 bits: 0-3)

    or.b    d1,d2                   | d2 = (R<<5) | (G<<2)
    or.b    d0,d2                   | d2 = (R<<5) | (G<<2) | B
    move.b  d2,(a1)+                | store RGB332 byte

    dbra    d6,.argb_pixel

    add.l   a2,a0                   | src += src_skip
    add.l   a3,a1                   | dst += dst_skip
    dbra    d5,.argb_row_1

.argb_exit_1:
    movem.l (sp)+,a2-a6
    movem.l (sp)+,d2-d7
    rts

| ============================================================================
| Atari_ConvertRGB332toBGRA8888_asm
|
| C: void Atari_ConvertRGB332toBGRA8888_asm(const Uint8 *src, Uint8 *dst,
|                                           int width, int height,
|                                           int src_pitch, int dst_pitch)
|
| RGB332 input: [RRRGGGBB]
| BGRA8888 output: [B][G][R][A] — A = 0xFF
|
| Expansion:
|   B2 (0-3) -> B8 = (B2 << 6) | (B2 << 4) | (B2 << 2) | B2
|   G3 (0-7) -> G8 = (G3 << 5) | (G3 << 2) | (G3 >> 1)
|   R3 (0-7) -> R8 = (R3 << 5) | (R3 << 2) | (R3 >> 1)
|   A = 0xFF
|
| Register save: split movem d2-d7 / a2-a6 → parameters at offset 48.
| d0: pixel, d1: temp, d2-d4: B,G,R components
| d5: width, d6: height, d7: loop counter
| a0: src, a1: dst, a2: src_skip, a3: dst_skip
| ============================================================================

    .text
    .globl  _Atari_ConvertRGB332toBGRA8888_asm

_Atari_ConvertRGB332toBGRA8888_asm:
    movem.l d2-d7,-(sp)
    movem.l a2-a6,-(sp)

    move.l  48(sp),a0               | a0 = src
    move.l  52(sp),a1               | a1 = dst
    move.l  56(sp),d5               | d5 = width
    move.l  60(sp),d6               | d6 = height
    move.l  64(sp),a2               | a2 = src_pitch
    move.l  68(sp),a3               | a3 = dst_pitch

    | src_skip = src_pitch - width
    sub.l   d5,a2                   | a2 = src_skip

    | dst_skip = dst_pitch - width*4
    move.l  d5,d0
    lsl.l   #2,d0                   | width * 4
    sub.l   d0,a3                   | a3 = dst_skip

    subq.l  #1,d6
    bmi.s   .rgb332_bgra_exit

.rgb332_bgra_row:
    move.l  d5,d7
    subq.l  #1,d7

.rgb332_bgra_pixel:
    moveq   #0,d0
    move.b  (a0)+,d0                | d0 = RGB332 pixel

    | Extract B (bits 1-0) -> d2
    move.b  d0,d2
    and.b   #0x03,d2                | d2 = B2 (0-3)
    | Expand B2 to B8: (B2 << 6) | (B2 << 4) | (B2 << 2) | B2
    move.b  d2,d1
    lsl.b   #2,d1
    or.b    d1,d2                   | d2 = B2 | (B2 << 2) = lower 4 bits: 0000BBBB
    move.b  d2,d1
    lsl.b   #4,d1
    or.b    d1,d2                   | d2 = full B8

    | Extract G (bits 4-2) -> d3
    move.b  d0,d3
    lsr.b   #2,d3                   | shift right 2
    and.b   #0x07,d3                | d3 = G3 (0-7)
    | Expand G3 to G8: (G3 << 5) | (G3 << 2) | (G3 >> 1)
    move.b  d3,d1
    lsr.b   #1,d1                   | d1 = G3 >> 1
    lsl.b   #2,d3                   | d3 = G3 << 2
    or.b    d1,d3                   | d3 = (G3 << 2) | (G3 >> 1)
    move.b  d3,d1
    lsl.b   #5,d1
    or.b    d1,d3                   | d3 = full G8

    | Extract R (bits 7-5) -> d4
    lsr.b   #5,d0                   | d0 = R3 (0-7)
    | Expand R3 to R8: (R3 << 5) | (R3 << 2) | (R3 >> 1)
    move.b  d0,d4
    move.b  d0,d1
    lsr.b   #1,d1                   | d1 = R3 >> 1
    lsl.b   #2,d4                   | d4 = R3 << 2
    or.b    d1,d4                   | d4 = (R3 << 2) | (R3 >> 1)
    move.b  d4,d1
    lsl.b   #5,d1
    or.b    d1,d4                   | d4 = full R8

    | Store BGRA
    move.b  d2,(a1)+                | B
    move.b  d3,(a1)+                | G
    move.b  d4,(a1)+                | R
    move.b  #0xFF,(a1)+             | A = 0xFF

    dbra    d7,.rgb332_bgra_pixel

    add.l   a2,a0                   | src += src_skip
    add.l   a3,a1                   | dst += dst_skip
    dbra    d6,.rgb332_bgra_row

.rgb332_bgra_exit:
    movem.l (sp)+,a2-a6
    movem.l (sp)+,d2-d7
    rts

| ============================================================================
| Atari_ConvertRGB332toARGB8888_asm - 68000-optimized LUT converter
|
| C: void Atari_ConvertRGB332toARGB8888_asm(const Uint8 *src, Uint32 *dst,
|                                           int width, int height,
|                                           int src_pitch, int dst_pitch)
|
| Output: 4 bytes per pixel (AA RR GG BB), alpha = 0xFF
| LUT: 256 entries, 4 bytes each (0xFFRRGGBB), indexed by byte value * 4
| ============================================================================

    .text
    .globl  _Atari_ConvertRGB332toARGB8888_asm

_Atari_ConvertRGB332toARGB8888_asm:
    movem.l d2-d7/a2-a6,-(sp)

    move.l  48(sp),a0               | a0 = src
    move.l  52(sp),a1               | a1 = dst
    move.l  56(sp),d4               | d4 = width (preserved for remainder)
    move.l  60(sp),d5               | d5 = height
    move.l  64(sp),d6               | d6 = src_pitch
    move.l  68(sp),d7               | d7 = dst_pitch

    | Precompute skips
    move.l  d6,a2
    sub.l   d4,a2                   | a2 = src_skip

    move.l  d4,d0
    lsl.l   #2,d0                   | d0 = width * 4
    move.l  d7,a3
    sub.l   d0,a3                   | a3 = dst_skip

    lea     _rgb332_to_argb8888_lut,a6

    | Split: d2 = width/4, d4 = width%4
    move.l  d4,d2
    lsr.l   #2,d2                   | d2 = width / 4 (PRESERVED - don't clobber)
    and.l   #3,d4                   | d4 = width % 4 (PRESERVED)

    subq.l  #1,d5
    bmi.s   .argb_exit

.argb_row:
    move.l  d2,d6                   | d6 = blocks for this row
    beq.s   .argb_rem_only
    subq.l  #1,d6

.argb_4px:
    | Pixel 0
    moveq   #0,d0
    move.b  (a0)+,d0
    lsl.l   #2,d0                   | index * 4
    move.l  (a6,d0.l),d1            | d1 = pixel 0 (use d1, not d2!)

    | Pixel 1
    moveq   #0,d0
    move.b  (a0)+,d0
    lsl.l   #2,d0
    move.l  (a6,d0.l),d3            | d3 = pixel 1 (use d3, not d2!)
    move.l  d1,(a1)+                | store pixel 0

    | Pixel 2
    moveq   #0,d0
    move.b  (a0)+,d0
    lsl.l   #2,d0
    move.l  (a6,d0.l),d1            | d1 = pixel 2 (reuse d1)
    move.l  d3,(a1)+                | store pixel 1

    | Pixel 3
    moveq   #0,d0
    move.b  (a0)+,d0
    lsl.l   #2,d0
    move.l  (a6,d0.l),d3            | d3 = pixel 3 (reuse d3)
    move.l  d1,(a1)+                | store pixel 2
    move.l  d3,(a1)+                | store pixel 3

    dbra    d6,.argb_4px

.argb_rem_only:
    move.l  d4,d6
    beq.s   .argb_next
    subq.l  #1,d6

.argb_rem:
    moveq   #0,d0
    move.b  (a0)+,d0
    lsl.l   #2,d0
    move.l  (a6,d0.l),(a1)+
    dbra    d6,.argb_rem

.argb_next:
    add.l   a2,a0
    add.l   a3,a1

    dbra    d5,.argb_row

.argb_exit:
    movem.l (sp)+,d2-d7/a2-a6
    rts

| ============================================================================
| Atari_BlitFast_asm - Direct translation of C version
| void Atari_BlitFast_asm(void *dst, const void *src, int row_bytes, int rows, int pitch)
|
| This is a straightforward translation of the C code:
|   for (row = 0; row < rows; row++) {
|       dst_ptr = dst_base + row * pitch;
|       src_ptr = src_base + row * pitch;
|       // copy row_bytes from src_ptr to dst_ptr
|   }
| ============================================================================

    .text
    .globl  _Atari_BlitFast_asm

| ============================================================================
| Atari_BlitFast_asm – Optimized memory copy for TrueColor modes
|
| C: void Atari_BlitFast_asm(void *dst, const void *src,
|                            int row_bytes, int rows, int pitch);
|
| This version uses pointer increments to avoid per‑row multiplication,
| yet correctly advances by 'pitch' after each row, not by row_bytes.
| ============================================================================

_Atari_BlitFast_asm:
    movem.l d2-d7/a2-a6,-(sp)       | Save registers (44 bytes)
    
    move.l  48(sp),a1               | a1 = dst_base
    move.l  52(sp),a2               | a2 = src_base  
    move.l  56(sp),d4               | d4 = row_bytes
    move.l  60(sp),d5               | d5 = rows
    move.l  64(sp),d6               | d6 = pitch
    
    tst.l   d5
    ble     .exit
    
    | Initialize row pointers
    move.l  a1,a3                   | a3 = dst row pointer
    move.l  a2,a0                   | a0 = src row pointer
    subq.l  #1,d5                   | dbra count

.row_loop:
    | Use temporary registers for the copy
    move.l  a0,a4                   | a4 = src copy pointer
    move.l  a3,a5                   | a5 = dst copy pointer

    | Check alignment of a4 and a5 (both must be longword-aligned)
    move.l  a4,d0
    and.l   #3,d0
    bne     .copy_bytes             | src misaligned
    move.l  a5,d0
    and.l   #3,d0
    bne     .copy_bytes             | dst misaligned

    | Aligned: copy longwords
    move.l  d4,d2
    lsr.l   #2,d2                   | d2 = number of longwords
    beq     .check_byte_remainder

    | Copy 4 longwords at a time (unrolled)
    move.l  d2,d3
    lsr.l   #2,d3                   | d3 = longs / 4
    beq     .copy_remaining_longs

    subq.l  #1,d3
.copy_4longs:
    move.l  (a4)+,(a5)+
    move.l  (a4)+,(a5)+
    move.l  (a4)+,(a5)+
    move.l  (a4)+,(a5)+
    dbra    d3,.copy_4longs

.copy_remaining_longs:
    move.l  d2,d3
    and.l   #3,d3                   | d3 = longs % 4
    beq     .check_byte_remainder

    subq.l  #1,d3
.copy_1long:
    move.l  (a4)+,(a5)+
    dbra    d3,.copy_1long

.check_byte_remainder:
    | Check for remaining bytes
    move.l  d4,d0
    and.l   #3,d0
    beq     .next_row

    subq.l  #1,d0
.copy_rem_bytes:
    move.b  (a4)+,(a5)+
    dbra    d0,.copy_rem_bytes
    bra     .next_row

.copy_bytes:
    | Byte-by-byte copy (unaligned)
    move.l  d4,d0
    beq     .next_row
    subq.l  #1,d0
.byte_loop:
    move.b  (a4)+,(a5)+
    dbra    d0,.byte_loop

.next_row:
    | Advance to next row by pitch (not row_bytes)
    add.l   d6,a0                   | src += pitch
    add.l   d6,a3                   | dst += pitch
    dbra    d5,.row_loop

.exit:
    movem.l (sp)+,d2-d7/a2-a6
    rts
