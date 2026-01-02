| Atari Chunky-to-Planar Conversion Routines
| Fixed for m68k-atari-mint calling convention (with underscores)

    .text
    .even

| void Atari_C2P_8to4(void *src, void *dst, int width, int height,
|                     int src_pitch, int dst_pitch)
    .globl _Atari_C2P_8to4        | Note: underscore prefix for C

| CPU detection: movep is 3-10x slower on 68030+
#if defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
    #define MOVEP_IS_SLOW 1
#endif

_Atari_C2P_8to4:
    movem.l d4-d7/a2-a4,-(sp)
    
    | Prepare plane pointers
    move.l  a1,a2
    move.l  a1,a3
    move.l  a1,a4
    adda.l  d3,a2
    adda.l  d3,a3
    adda.l  d3,a4
    adda.l  d3,a3
    adda.l  d3,a4
    
    subq.l  #1,d1
    
.height_loop:
    move.w  d0,d7
    and.w   #7,d7
    lsr.w   #3,d0
    
.block_loop:
    move.l  (a0)+,d4
    move.l  (a0)+,d5
    
    | Plane 0
    move.w  d4,d6
    and.w   #0x0101,d6
    movep.w d6,0(a1)
    move.w  d5,d6
    and.w   #0x0101,d6
    movep.w d6,2(a1)
    adda.l  #4,a1
#ifndef MOVEP_IS_SLOW    
    | Plane 1
    move.w  d4,d6
    and.w   #0x0202,d6
    lsr.w   #1,d6
    movep.w d6,0(a2)
    move.w  d5,d6
    and.w   #0x0202,d6
    lsr.w   #1,d6
    movep.w d6,2(a2)
    adda.l  #4,a2
#else
    | Plane 1 (68030+): 5-10x faster with move.w
    move.w  d4,d6
    and.w   #0x0202,d6
    lsr.w   #1,d6
    move.w  d6,(a2)+
    move.w  d5,d6
    and.w   #0x0202,d6
    lsr.w   #1,d6
    move.w  d6,(a2)+
#endif

    | Plane 2
    move.w  d4,d6
    and.w   #0x0404,d6
    lsr.w   #2,d6
    movep.w d6,0(a3)
    move.w  d5,d6
    and.w   #0x0404,d6
    lsr.w   #2,d6
#ifndef MOVEP_IS_SLOW    
    movep.w d6,2(a3)
    adda.l  #4,a3
    
    | Plane 3
    move.w  d4,d6
    and.w   #0x0808,d6
    lsr.w   #3,d6
    movep.w d6,0(a4)
    move.w  d5,d6
    and.w   #0x0808,d6
    lsr.w   #3,d6
    movep.w d6,2(a4)
#else
    | Planes 2-3 (68030+): move.w for speed
    move.w  d6,(a3)+
    move.w  d5,d6
    and.w   #0x0404,d6
    lsr.w   #2,d6
    move.w  d6,(a3)+
    
    move.w  d4,d6
    and.w   #0x0808,d6
    lsr.w   #3,d6
    move.w  d6,(a4)+
    move.w  d5,d6
    and.w   #0x0808,d6
    lsr.w   #3,d6
    move.w  d6,(a4)+
#endif    
    adda.l  #4,a4
    
    dbra    d0,.block_loop
    
    tst.w   d7
    beq.s   .line_done
    
.leftover_loop:
    move.b  (a0)+,d4
    
    btst    #0,d4
    beq.s   .p0_clear
    bset    #7,(a1)
.p0_clear:
    
    btst    #1,d4
    beq.s   .p1_clear
    bset    #7,(a2)
.p1_clear:
    
    btst    #2,d4
    beq.s   .p2_clear
    bset    #7,(a3)
.p2_clear:
    
    btst    #3,d4
    beq.s   .p3_clear
    bset    #7,(a4)
.p3_clear:
    
    addq.l  #1,a1
    addq.l  #1,a2
    addq.l  #1,a3
    addq.l  #1,a4
    
    dbra    d7,.leftover_loop
    
.line_done:
    adda.l  d2,a0
    adda.l  d3,a1
    adda.l  d3,a2
    adda.l  d3,a3
    adda.l  d3,a4
    
    dbra    d1,.height_loop
    
    movem.l (sp)+,d4-d7/a2-a4
    rts

| void Atari_C2P_8to8(void *src, void *dst, int width, int height,
|                     int src_pitch, int dst_pitch)
    .globl _Atari_C2P_8to8        | Note: underscore prefix
_Atari_C2P_8to8:
    | Stub for now
    rts

| void Atari_BlitFast(void *dst, const void *src, int row_bytes,
|                     int rows, int pitch)
    .globl _Atari_BlitFast        | Note: underscore prefix
_Atari_BlitFast:
    movem.l d2-d4/a2,-(sp)
    
    | Get parameters (after movem saves 4 regs = 16 bytes)
    move.l  20(sp),a0       | dst
    move.l  24(sp),a1       | src  
    move.l  28(sp),d0       | row_bytes
    move.l  32(sp),d1       | rows
    move.l  36(sp),d2       | pitch
    
    subq.l  #1,d1           | Adjust for dbra
    
.row_loop:
    move.l  d0,d3           | Bytes to copy this row
    
    | Copy using longs where possible
    move.l  d3,d4
    lsr.l   #2,d4           | Number of longs
    beq.s   .copy_bytes_only
    subq.l  #1,d4
    
.copy_longs:
    move.l  (a1)+,(a0)+
    dbra    d4,.copy_longs
    
.copy_bytes_only:
    | Copy remaining bytes (0-3)
    and.l   #3,d3
    beq.s   .row_done
    subq.l  #1,d3
    
.copy_bytes:
    move.b  (a1)+,(a0)+
    dbra    d3,.copy_bytes
    
.row_done:
    | Move to next row
    suba.l  d0,a0           | Back to start of dest row
    suba.l  d0,a1           | Back to start of src row
    adda.l  d2,a0           | Add pitch to move to next dest row
    adda.l  d2,a1           | Add pitch to move to next src row
    
    dbra    d1,.row_loop
    
    movem.l (sp)+,d2-d4/a2
    rts