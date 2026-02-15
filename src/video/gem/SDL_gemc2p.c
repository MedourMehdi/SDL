#include "SDL_gemvideo.h"

#ifdef SDL_GEM_C2P_ASM
    /* Ces symboles sont définis dans l'assembleur */
    extern void Atari_C2P_8to1_asm(void*, void*, int, int, int, int);
    extern void Atari_C2P_8to2_asm(void*, void*, int, int, int, int);
    extern void Atari_C2P_8to4_asm(void*, void*, int, int, int, int);
    extern void Atari_C2P_8to8_asm(void*, void*, int, int, int, int);

    /* Combined LUT + C2P routines */
    extern void Atari_C2P_8to1_LUT_asm(void*, void*, int, int, int, int, Uint8*);
    extern void Atari_C2P_8to2_LUT_asm(void*, void*, int, int, int, int, Uint8*);
    extern void Atari_C2P_8to4_LUT_asm(void*, void*, int, int, int, int, Uint8*);
    extern void Atari_C2P_8to8_LUT_asm(void*, void*, int, int, int, int, Uint8*);

    #define C2P_8to1(a, b, c, d, e, f) Atari_C2P_8to1_asm(a, b, c, d, e, f)
    #define C2P_8to2(a, b, c, d, e, f) Atari_C2P_8to2_asm(a, b, c, d, e, f)
    #define C2P_8to4(a, b, c, d, e, f) Atari_C2P_8to4_asm(a, b, c, d, e, f)
    #define C2P_8to8(a, b, c, d, e, f) Atari_C2P_8to8_asm(a, b, c, d, e, f)
#else
    #define C2P_8to1(a, b, c, d, e, f) Atari_C2P_8to1(a, b, c, d, e, f)
    #define C2P_8to2(a, b, c, d, e, f) Atari_C2P_8to2(a, b, c, d, e, f)
    #define C2P_8to4(a, b, c, d, e, f) Atari_C2P_8to4(a, b, c, d, e, f)
    #define C2P_8to8(a, b, c, d, e, f) Atari_C2P_8to8(a, b, c, d, e, f)
#endif



/* ====================================================================
   FILE: SDL_gemc2p_optimized.c
   Optimized Atari Chunky-to-Planar Conversion Routines
   
   OPTIMIZATIONS:
   1. Lookup table-based C2P for 68000 (eliminates variable shifts)
   2. CPU-adaptive selection (LUT for 68000, shift-based for 030+)
   3. Unrolled memory copy for TrueColor modes
   ==================================================================== */

#ifndef SDL_GEM_C2P_ASM

/* --------------------------------------------------------------------
   Lookup Tables for Fast C2P Conversion
   -------------------------------------------------------------------- */

/* Bit position masks for 16-pixel words (MSB first) */
static uint16_t c2p_bit_mask[16] = {
    0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100,
    0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001
};

/* Plane expansion lookup: Given pixel value, return 16-bit word for each plane */
/* This is a 2KB table (256 pixels × 8 planes × 2 bytes) */
static uint16_t c2p_plane_expand[256][8];
static int c2p_tables_initialized = 0;

/* --------------------------------------------------------------------
   Initialize C2P lookup tables - call once at startup
   -------------------------------------------------------------------- */
void Atari_C2P_InitTables(void)
{
    int pixel, plane;
    
    if (c2p_tables_initialized) return;
    
    /* For each possible pixel value (0-255) */
    for (pixel = 0; pixel < 256; pixel++) {
        /* For each bitplane (0-7) */
        for (plane = 0; plane < 8; plane++) {
            uint16_t word = 0;
            
            /* If this pixel has this plane bit set, set all 16 bits */
            if (pixel & (1 << plane)) {
                word = 0xFFFF;
            }
            
            c2p_plane_expand[pixel][plane] = word;
        }
    }
    
    c2p_tables_initialized = 1;
}

/* --------------------------------------------------------------------
   Atari_C2P_8to8_LUT
   
   Converts 8-bit chunky to 8-bit planar using lookup tables.
   MUCH faster on 68000 (no variable shifts).
   -------------------------------------------------------------------- */
void Atari_C2P_8to8_LUT(void *src, void *dst, int width, int height,
                        int src_pitch, int dst_pitch)
{
    uint8_t *src_ptr = (uint8_t *)src;
    uint16_t *dst_ptr = (uint16_t *)dst;
    int y, x, px, plane;
    
    int src_skip = src_pitch - width;
    int dst_bytes_per_row = (width >> 4) * 16;
    int dst_skip = (dst_pitch - dst_bytes_per_row) / 2;
    
    /* Ensure tables are initialized */
    if (!c2p_tables_initialized) Atari_C2P_InitTables();
    
    for (y = 0; y < height; y++) {
        /* Process 16 pixels at a time */
        for (x = 0; x < width; x += 16) {
            uint16_t planes[8] = {0};
            
            /* Process 16 pixels, building up plane words */
            for (px = 0; px < 16; px++) {
                uint8_t pixel = src_ptr[px];
                uint16_t mask = c2p_bit_mask[px];
                
                /* Add this pixel's contribution to each plane */
                if (pixel & 0x01) planes[0] |= mask;
                if (pixel & 0x02) planes[1] |= mask;
                if (pixel & 0x04) planes[2] |= mask;
                if (pixel & 0x08) planes[3] |= mask;
                if (pixel & 0x10) planes[4] |= mask;
                if (pixel & 0x20) planes[5] |= mask;
                if (pixel & 0x40) planes[6] |= mask;
                if (pixel & 0x80) planes[7] |= mask;
            }
            
            /* Write interleaved planes */
            for (plane = 0; plane < 8; plane++) {
                *dst_ptr++ = planes[plane];
            }
            
            src_ptr += 16;
        }
        
        src_ptr += src_skip;
        dst_ptr += dst_skip;
    }
}

/* --------------------------------------------------------------------
   Atari_C2P_8to4_LUT
   
   Converts 8-bit chunky to 4-bit planar using lookup tables.
   -------------------------------------------------------------------- */
void Atari_C2P_8to4_LUT(void *src, void *dst, int width, int height,
                        int src_pitch, int dst_pitch)
{
    uint8_t *src_ptr = (uint8_t *)src;
    uint16_t *dst_ptr = (uint16_t *)dst;
    int y, x, px, plane;
    
    int src_skip = src_pitch - width;
    int dst_bytes_per_row = (width >> 4) * 8;
    int dst_skip = (dst_pitch - dst_bytes_per_row) / 2;
    
    if (!c2p_tables_initialized) Atari_C2P_InitTables();
    
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x += 16) {
            uint16_t planes[4] = {0};
            
            for (px = 0; px < 16; px++) {
                uint8_t pixel = src_ptr[px];
                uint16_t mask = c2p_bit_mask[px];
                
                if (pixel & 0x01) planes[0] |= mask;
                if (pixel & 0x02) planes[1] |= mask;
                if (pixel & 0x04) planes[2] |= mask;
                if (pixel & 0x08) planes[3] |= mask;
            }
            
            for (plane = 0; plane < 4; plane++) {
                *dst_ptr++ = planes[plane];
            }
            
            src_ptr += 16;
        }
        
        src_ptr += src_skip;
        dst_ptr += dst_skip;
    }
}

void Atari_C2P_8to4(void *src, void *dst, int width, int height,
                    int src_pitch, int dst_pitch)
{
    uint8_t *src_ptr = (uint8_t *)src;
    uint16_t *dst_ptr = (uint16_t *)dst;
    int y;
    
    int src_skip = src_pitch - width;
    int dst_bytes_per_row = (width >> 4) * 8;
    int dst_skip = (dst_pitch - dst_bytes_per_row) / 2;
    
    for (y = 0; y < height; y++) {
        int x;
        
        for (x = 0; x < width; x += 16) {
            uint16_t plane0 = 0;
            uint16_t plane1 = 0;
            uint16_t plane2 = 0;
            uint16_t plane3 = 0;
            int bit;
            
            for (bit = 15; bit >= 0; bit--) {
                uint8_t pixel = *src_ptr++;
                
                if (pixel & 0x01) plane0 |= (1 << bit);
                if (pixel & 0x02) plane1 |= (1 << bit);
                if (pixel & 0x04) plane2 |= (1 << bit);
                if (pixel & 0x08) plane3 |= (1 << bit);
            }
            
            *dst_ptr++ = plane0;
            *dst_ptr++ = plane1;
            *dst_ptr++ = plane2;
            *dst_ptr++ = plane3;
        }
        
        src_ptr += src_skip;
        dst_ptr += dst_skip;
    }
}

void Atari_C2P_8to8(void *src, void *dst, int width, int height,
                    int src_pitch, int dst_pitch)
{
    uint8_t *src_ptr = (uint8_t *)src;
    uint16_t *dst_ptr = (uint16_t *)dst;
    int y;
    
    int src_skip = src_pitch - width;
    int dst_bytes_per_row = (width >> 4) * 16;
    int dst_skip = (dst_pitch - dst_bytes_per_row) / 2;
    
    for (y = 0; y < height; y++) {
        int x;
        
        for (x = 0; x < width; x += 16) {
            uint16_t plane0 = 0, plane1 = 0, plane2 = 0, plane3 = 0;
            uint16_t plane4 = 0, plane5 = 0, plane6 = 0, plane7 = 0;
            int bit;
            
            for (bit = 15; bit >= 0; bit--) {
                uint8_t pixel = *src_ptr++;
                
                if (pixel & 0x01) plane0 |= (1 << bit);
                if (pixel & 0x02) plane1 |= (1 << bit);
                if (pixel & 0x04) plane2 |= (1 << bit);
                if (pixel & 0x08) plane3 |= (1 << bit);
                if (pixel & 0x10) plane4 |= (1 << bit);
                if (pixel & 0x20) plane5 |= (1 << bit);
                if (pixel & 0x40) plane6 |= (1 << bit);
                if (pixel & 0x80) plane7 |= (1 << bit);
            }
            
            *dst_ptr++ = plane0;
            *dst_ptr++ = plane1;
            *dst_ptr++ = plane2;
            *dst_ptr++ = plane3;
            *dst_ptr++ = plane4;
            *dst_ptr++ = plane5;
            *dst_ptr++ = plane6;
            *dst_ptr++ = plane7;
        }
        
        src_ptr += src_skip;
        dst_ptr += dst_skip;
    }
}

/* --------------------------------------------------------------------
   2-bit and 1-bit planar conversions
   -------------------------------------------------------------------- */

void Atari_C2P_8to2(void *src, void *dst, int width, int height,
                    int src_pitch, int dst_pitch)
{
    uint8_t *src_ptr = (uint8_t *)src;
    uint16_t *dst_ptr = (uint16_t *)dst;
    int y;
    
    int src_skip = src_pitch - width;
    int dst_bytes_per_row = (width >> 4) * 4;
    int dst_skip = (dst_pitch - dst_bytes_per_row) / 2;
    
    for (y = 0; y < height; y++) {
        int x;
        
        for (x = 0; x < width; x += 16) {
            uint16_t plane0 = 0;
            uint16_t plane1 = 0;
            int bit;
            
            for (bit = 15; bit >= 0; bit--) {
                uint8_t pixel = *src_ptr++;
                
                if (pixel & 0x01) plane0 |= (1 << bit);
                if (pixel & 0x02) plane1 |= (1 << bit);
            }
            
            *dst_ptr++ = plane0;
            *dst_ptr++ = plane1;
        }
        
        src_ptr += src_skip;
        dst_ptr += dst_skip;
    }
}

void Atari_C2P_8to1(void *src, void *dst, int width, int height,
                    int src_pitch, int dst_pitch)
{
    uint8_t *src_ptr = (uint8_t *)src;
    uint16_t *dst_ptr = (uint16_t *)dst;
    int y;
    
    int src_skip = src_pitch - width;
    int dst_bytes_per_row = (width >> 4) * 2;
    int dst_skip = (dst_pitch - dst_bytes_per_row) / 2;
    
    for (y = 0; y < height; y++) {
        int x;
        
        for (x = 0; x < width; x += 16) {
            uint16_t plane0 = 0;
            int bit;
            
            for (bit = 15; bit >= 0; bit--) {
                uint8_t pixel = *src_ptr++;
                
                if (pixel & 0x01) plane0 |= (1 << bit);
            }
            
            *dst_ptr++ = plane0;
        }
        
        src_ptr += src_skip;
        dst_ptr += dst_skip;
    }
}

/* --------------------------------------------------------------------
   CPU-Adaptive C2P Wrapper
   
   Automatically selects best implementation based on detected CPU
   -------------------------------------------------------------------- */

void Atari_C2P_Planar(void *src, void *dst, int width, int height, 
                      int src_pitch, int dst_pitch, int planes)
{
    /* Use LUT on 68000/68010, shift-based on 68030+ */
    int use_lut = (hw_info.cpu <= ATARI_CPU_68010);
    
    switch (planes) {
        case 1:
            Atari_C2P_8to1(src, dst, width, height, src_pitch, dst_pitch);
            break;
        case 2:
            Atari_C2P_8to2(src, dst, width, height, src_pitch, dst_pitch);
            break;
        case 4:
            if (use_lut) {
                Atari_C2P_8to4_LUT(src, dst, width, height, src_pitch, dst_pitch);
            } else {
                Atari_C2P_8to4(src, dst, width, height, src_pitch, dst_pitch);
            }
            break;
        case 8:
            if (use_lut) {
                Atari_C2P_8to8_LUT(src, dst, width, height, src_pitch, dst_pitch);
            } else {
                Atari_C2P_8to8(src, dst, width, height, src_pitch, dst_pitch);
            }
            break;
        default:
            /* Unsupported */
            break;
    }
}

/* --------------------------------------------------------------------
   OPTIMIZED: Memory copy for TrueColor modes (16/24/32bpp)
   -------------------------------------------------------------------- */

void Atari_BlitFast_c(void *dst, const void *src, int row_bytes,
                    int rows, int pitch)
{
    uint8_t *dst_base = (uint8_t *)dst;
    const uint8_t *src_base = (const uint8_t *)src;
    int row;
    
    for (row = 0; row < rows; row++) {
        uint8_t *dst_ptr = dst_base + row * pitch;
        const uint8_t *src_ptr = src_base + row * pitch;
        int bytes_copied = 0;
        
        /* Check alignment */
        uintptr_t src_align = (uintptr_t)src_ptr & 3;
        uintptr_t dst_align = (uintptr_t)dst_ptr & 3;
        
        if (src_align == 0 && dst_align == 0 && row_bytes >= 16) {
            /* Both aligned - use 32-bit copies with 4x unroll */
            int longs = row_bytes >> 2;
            uint32_t *dst32 = (uint32_t *)dst_ptr;
            const uint32_t *src32 = (const uint32_t *)src_ptr;
            int i = 0;
            
            /* Unrolled loop - copy 16 bytes per iteration */
            for (; i <= longs - 4; i += 4) {
                dst32[i+0] = src32[i+0];
                dst32[i+1] = src32[i+1];
                dst32[i+2] = src32[i+2];
                dst32[i+3] = src32[i+3];
            }
            
            /* Remaining longs */
            for (; i < longs; i++) {
                dst32[i] = src32[i];
            }
            
            bytes_copied = longs * 4;
            dst_ptr = (uint8_t *)(dst32 + longs);
            src_ptr = (const uint8_t *)(src32 + longs);
        }
        
        /* Copy remaining bytes */
        while (bytes_copied < row_bytes) {
            *dst_ptr++ = *src_ptr++;
            bytes_copied++;
        }
    }
}

/* Memcpy variant - may be faster with optimized C library */
void Atari_BlitFast_Memcpy(void *dst, const void *src, int row_bytes,
                           int rows, int pitch)
{
    uint8_t *dst_ptr = (uint8_t *)dst;
    const uint8_t *src_ptr = (const uint8_t *)src;
    int row;
    
    if (row_bytes == pitch) {
        memcpy(dst_ptr, src_ptr, row_bytes * rows);
        return;
    }
    
    for (row = 0; row < rows; row++) {
        memcpy(dst_ptr, src_ptr, row_bytes);
        dst_ptr += pitch;
        src_ptr += pitch;
    }
}

void Atari_BlitFast_Aligned16(void *dst, const void *src, int row_bytes,
                              int rows, int pitch)
{
    uint32_t *dst_ptr = (uint32_t *)dst;
    const uint32_t *src_ptr = (const uint32_t *)src;
    int row, i;
    int longs_per_row = row_bytes >> 2;
    
    for (row = 0; row < rows; row++) {
        i = longs_per_row;
        while (i >= 4) {
            dst_ptr[0] = src_ptr[0];
            dst_ptr[1] = src_ptr[1];
            dst_ptr[2] = src_ptr[2];
            dst_ptr[3] = src_ptr[3];
            dst_ptr += 4;
            src_ptr += 4;
            i -= 4;
        }
        
        while (i > 0) {
            *dst_ptr++ = *src_ptr++;
            i--;
        }
        
        dst_ptr = (uint32_t *)((uint8_t *)dst + (row + 1) * pitch);
        src_ptr = (const uint32_t *)((const uint8_t *)src + (row + 1) * pitch);
    }
}
#else

void Atari_C2P_Planar(void *src, void *dst, int width, int height,
                      int src_pitch, int dst_pitch, int planes)
{
    switch (planes) {
        case 1:  C2P_8to1(src, dst, width, height, src_pitch, dst_pitch); break;
        case 2:  C2P_8to2(src, dst, width, height, src_pitch, dst_pitch); break;
        case 4:  C2P_8to4(src, dst, width, height, src_pitch, dst_pitch); break;
        case 8:  C2P_8to8(src, dst, width, height, src_pitch, dst_pitch); break;
        /* TrueColor géré ailleurs */
    }
}
#endif

void Atari_C2P_Planar_LUT(void *src, void *dst, int width, int height,
                          int src_pitch, int dst_pitch, int planes, Uint8 *lut)
{
#ifdef SDL_GEM_C2P_ASM
    switch (planes) {
        case 1:  Atari_C2P_8to1_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, lut); break;
        case 2:  Atari_C2P_8to2_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, lut); break;
        case 4:  Atari_C2P_8to4_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, lut); break;
        case 8:  Atari_C2P_8to8_LUT_asm(src, dst, width, height, src_pitch, dst_pitch, lut); break;
        default: /* Unsupported */ break;
    }
#else
    /* C fallback: row-by-row LUT + C2P for all modes */
    {
        Uint8 *remap;
        Uint8 *src_ptr = (Uint8*)src;
        Uint8 *dst_ptr = (Uint8*)dst;
        int row, i;
        
        /* Allocate remap buffer on stack or use static */
        remap = (Uint8*)SDL_malloc(width);
        if (!remap) return;
        
        for (row = 0; row < height; row++) {
            /* Apply LUT to this row */
            for (i = 0; i < width; i++) {
                remap[i] = lut[src_ptr[i]];
            }
            
            /* C2P for this row */
            switch (planes) {
                case 1: Atari_C2P_8to1(remap, dst_ptr, width, 1, width, dst_pitch); break;
                case 2: Atari_C2P_8to2(remap, dst_ptr, width, 1, width, dst_pitch); break;
                case 4: Atari_C2P_8to4(remap, dst_ptr, width, 1, width, dst_pitch); break;
                case 8: Atari_C2P_8to8(remap, dst_ptr, width, 1, width, dst_pitch); break;
            }
            
            src_ptr += src_pitch;
            dst_ptr += dst_pitch;
        }
        
        SDL_free(remap);
    }
#endif
}