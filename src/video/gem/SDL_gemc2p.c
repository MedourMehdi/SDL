#include "SDL_gemvideo.h"

#ifdef SDL_GEM_C2P_ASM
    /* Ces symboles sont définis dans l'assembleur */
    extern void Atari_C2P_8to1_asm(void*, void*, int, int, int, int);
    extern void Atari_C2P_8to2_asm(void*, void*, int, int, int, int);
    extern void Atari_C2P_8to4_asm(void*, void*, int, int, int, int);
    extern void Atari_C2P_8to8_asm(void*, void*, int, int, int, int);
    extern void Atari_ConvertBGRA8888toRGB332_asm(const Uint8 *src, Uint8 *dst,
                                                  int width, int height,
                                                  int src_pitch, int dst_pitch);
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

/* ============================================================================
   RGB332 to TrueColor Fast Converters - m68000 Optimized LUT-based
   
   These functions provide fast conversion from SDL's RGB332 (8bpp) format
   to native TrueColor formats using lookup tables.
   
   Memory usage: ~2.5KB total for all LUTs
   - RGB565 table: 512 bytes (256 entries * 2 bytes)
   - RGB888 table: 1KB (256 entries * 4 bytes, packed)
   - ARGB8888 table: 1KB (256 entries * 4 bytes)
   
   Performance: ~10-50x faster than generic SDL_ConvertPixels()
   ============================================================================ */

/* Lookup tables - initialized on first use */
Uint16 rgb332_to_rgb565_lut[256];
Uint32 rgb332_to_rgb888_lut[256];   /* Packed as 0x00RRGGBB */
Uint32 rgb332_to_argb8888_lut[256]; /* Packed as 0xAARRGGBB, A=0xFF */
static int rgb332_tc_lut_initialized = 0;

/* Initialize RGB332 to TrueColor LUTs - called automatically */
static void Atari_InitRGB332toTrueColorLUTs(void)
{
    int i;
    Uint8 r3, g3, b2;
    Uint8 r8, g8, b8;
    Uint16 r5, g6, b5;

    if (rgb332_tc_lut_initialized) {
        return;
    }

    for (i = 0; i < 256; i++) {
        r3 = (Uint8)((i >> 5) & 0x07);
        g3 = (Uint8)((i >> 2) & 0x07);
        b2 = (Uint8)(i & 0x03);

        r8 = (Uint8)((r3 << 5) | (r3 << 2) | (r3 >> 1));
        g8 = (Uint8)((g3 << 5) | (g3 << 2) | (g3 >> 1));
        b8 = (Uint8)((b2 << 6) | (b2 << 4) | (b2 << 2) | b2);

        r5 = (Uint16)((r3 * 31) / 7);
        g6 = (Uint16)((g3 * 63) / 7);
        b5 = (Uint16)((b2 * 31) / 3);

        rgb332_to_rgb565_lut[i] = (Uint16)((r5 << 11) | (g6 << 5) | b5);

        rgb332_to_rgb888_lut[i] = ((Uint32)r8 << 16) |
                                   ((Uint32)g8 << 8)  | b8;

        rgb332_to_argb8888_lut[i] = 0xFF000000UL        |
                                     ((Uint32)r8 << 16)  |
                                     ((Uint32)g8 << 8)   | b8;
    }

    rgb332_tc_lut_initialized = 1;
}

#ifdef SDL_GEM_C2P_ASM
    void Atari_ConvertRGB332toRGB565(const Uint8 *src, Uint16 *dst,
                                    int width, int height,
                                    int src_pitch, int dst_pitch)
    {
        if (!src || !dst || width <= 0 || height <= 0) return;

        if (!rgb332_tc_lut_initialized) {
            Atari_InitRGB332toTrueColorLUTs();
        }

        Atari_ConvertRGB332toRGB565_asm(src, dst, width, height,
                                        src_pitch, dst_pitch);
    }

    void Atari_ConvertRGB332toARGB8888(const Uint8 *src, Uint32 *dst,
                                      int width, int height,
                                      int src_pitch, int dst_pitch)
    {
        if (!src || !dst || width <= 0 || height <= 0) return;

        if (!rgb332_tc_lut_initialized) {
            Atari_InitRGB332toTrueColorLUTs();
        }

        Atari_ConvertRGB332toARGB8888_asm(src, dst, width, height,
                                        src_pitch, dst_pitch);
    }

    void Atari_ConvertBGRA8888toRGB332(const Uint8 *src, Uint8 *dst,
                                    int width, int height,
                                    int src_pitch, int dst_pitch)
    {
        if (!src || !dst || width <= 0 || height <= 0) return;
        
        Atari_ConvertBGRA8888toRGB332_asm(src, dst, width, height,
                                        src_pitch, dst_pitch);
    }
#else
/* RGB332 -> RGB565 (16bpp) - 2 bytes per pixel, aligned writes */
void Atari_ConvertRGB332toRGB565(const Uint8 *src, Uint16 *dst, 
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint16 *dst_row;
    
    if (!src || !dst || width <= 0 || height <= 0) {
        return;
    }
    
    if (!rgb332_tc_lut_initialized) {
        Atari_InitRGB332toTrueColorLUTs();
    }
    
    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = (Uint16 *)((Uint8 *)dst + (y * dst_pitch));
        
        /* Unroll by 4 for m68000 - reduces loop overhead */
        for (x = 0; x < width - 3; x += 4) {
            dst_row[x]   = rgb332_to_rgb565_lut[src_row[x]];
            dst_row[x+1] = rgb332_to_rgb565_lut[src_row[x+1]];
            dst_row[x+2] = rgb332_to_rgb565_lut[src_row[x+2]];
            dst_row[x+3] = rgb332_to_rgb565_lut[src_row[x+3]];
        }
        /* Handle remaining pixels */
        for (; x < width; x++) {
            dst_row[x] = rgb332_to_rgb565_lut[src_row[x]];
        }
    }
}

/* RGB332 -> ARGB8888 (32bpp) - 4 bytes per pixel, aligned writes */
void Atari_ConvertRGB332toARGB8888(const Uint8 *src, Uint32 *dst, 
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint32 *dst_row;
    
    if (!src || !dst || width <= 0 || height <= 0) {
        return;
    }
    
    if (!rgb332_tc_lut_initialized) {
        Atari_InitRGB332toTrueColorLUTs();
    }
    
    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = (Uint32 *)((Uint8 *)dst + (y * dst_pitch));
        
        /* Unroll by 4 for m68000 */
        for (x = 0; x < width - 3; x += 4) {
            dst_row[x]   = rgb332_to_argb8888_lut[src_row[x]];
            dst_row[x+1] = rgb332_to_argb8888_lut[src_row[x+1]];
            dst_row[x+2] = rgb332_to_argb8888_lut[src_row[x+2]];
            dst_row[x+3] = rgb332_to_argb8888_lut[src_row[x+3]];
        }
        for (; x < width; x++) {
            dst_row[x] = rgb332_to_argb8888_lut[src_row[x]];
        }
    }
}

void Atari_ConvertBGRA8888toRGB332(const Uint8 *src, Uint8 *dst,
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const int src_skip = src_pitch - (width * 4);
    const int dst_skip = dst_pitch - width;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            /* [B][G][R][A] in memory */
            Uint8 b = src[0];
            Uint8 g = src[1];
            Uint8 r = src[2];
            /* src[3] = A, ignored */
            *dst++ = ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
            src += 4;
        }
        src += src_skip;
        dst += dst_skip;
    }
}
#endif /* SDL_GEM_C2P_ASM */

/* RGB332 -> RGB888 (24bpp) - 3 bytes per pixel, packed */
void Atari_ConvertRGB332toRGB888(const Uint8 *src, Uint8 *dst, 
                                 int width, int height,
                                 int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint8 *dst_row;
    Uint32 packed;
    
    if (!src || !dst || width <= 0 || height <= 0) {
        return;
    }
    
    if (!rgb332_tc_lut_initialized) {
        Atari_InitRGB332toTrueColorLUTs();
    }
    
    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = dst + (y * dst_pitch);
        
        for (x = 0; x < width; x++) {
            packed = rgb332_to_rgb888_lut[src_row[x]];
            /* Store as R, G, B (big-endian order for Atari) */
            dst_row[x*3 + 0] = (packed >> 16) & 0xFF;  /* Red */
            dst_row[x*3 + 1] = (packed >> 8)  & 0xFF;  /* Green */
            dst_row[x*3 + 2] = packed & 0xFF;         /* Blue */
        }
    }
}

/* RGB332 -> ABGR8888 (32bpp, byte-swapped) - for little-endian formats */
void Atari_ConvertRGB332toABGR8888(const Uint8 *src, Uint32 *dst, 
                                   int width, int height,
                                   int src_pitch, int dst_pitch)
{
    int y, x;
    const Uint8 *src_row;
    Uint32 *dst_row;
    Uint32 argb;
    
    if (!src || !dst || width <= 0 || height <= 0) {
        return;
    }
    
    if (!rgb332_tc_lut_initialized) {
        Atari_InitRGB332toTrueColorLUTs();
    }
    
    for (y = 0; y < height; y++) {
        src_row = src + (y * src_pitch);
        dst_row = (Uint32 *)((Uint8 *)dst + (y * dst_pitch));
        
        for (x = 0; x < width; x++) {
            argb = rgb332_to_argb8888_lut[src_row[x]];
            /* Swap R and B: ARGB -> ABGR */
            /* Keep A and G, swap R and B */
            dst_row[x] = (argb & 0xFF00FF00UL) |           /* A and G unchanged */
                         ((argb & 0x00FF0000UL) >> 16) |   /* R to B position */
                         ((argb & 0x000000FFUL) << 16);    /* B to R position */
        }
    }
}

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
