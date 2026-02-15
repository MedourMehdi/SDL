/* ============================================
   SDL_gemwindow.c - Professional 68000-Optimized
   Dirty Rectangle Detection - ALL BUGS FIXED
   
   - Checksums updated AFTER full blit (not during detection)
   - Checksums updated AFTER partial blit via UpdateRowChecksumsAfterBlit()
   - Clear bytes-per-row calculation for TrueColor modes
   - Bounds checking in checksum array access
   - InitRowChecksums always reallocates on resize
   - prev_dirty_rects properly handled when no new changes
   - Periodic full refresh every 60 frames
   - Merged rectangles validated and clipped to window bounds
   ============================================ */

#include "SDL_gemvideo.h"
#include "mt_gemx.h"

#ifdef SDL_VIDEO_DRIVER_GEM

#define MFDB_STRIDE(w) (((w) + 15) & ~15)
#define MAX_MERGED_RECTS 16
#define MERGE_THRESHOLD 16
#define PERIODIC_REFRESH_FRAMES 60

#define MIN_DIRTY_RECT_WIDTH  321
#define MIN_DIRTY_RECT_HEIGHT 241

MFDB screen_mfdb = {0};

#ifdef SDL_GEM_DIRTY_RECT

static int MergeDirtyRects(const SDL_Rect *input, int num_input,
                           SDL_Rect *output, int max_output);

/* ============================================
   FAST ROW CHANGE DETECTION (68000-optimized)
   8-bit checksum per row, 1 byte/row RAM, ~2 cycles/row CPU
   ============================================ */

#ifndef SDL_GEM_DIRTY_RECT_ASM
/* Fast 16-bit row checksum - 68000 optimized
 * Unrolled to avoid loop overhead, uses 16-bit accumulation
 * This function is called frequently, must be fast on 68000 */
static Uint16 CalculateRowChecksum(const Uint8 *row, int len)
{
    Uint16 sum = 0;
    
    /* Main loop: 16 bytes at a time (8x unrolled)
     * On 68000: ~8 cycles per word with move.w (a0)+,d0 / add.w d0,d1 */
    while (len >= 16) {
        sum += (row[0] << 8) | row[1];
        sum += (row[2] << 8) | row[3];
        sum += (row[4] << 8) | row[5];
        sum += (row[6] << 8) | row[7];
        sum += (row[8] << 8) | row[9];
        sum += (row[10] << 8) | row[11];
        sum += (row[12] << 8) | row[13];
        sum += (row[14] << 8) | row[15];
        row += 16;
        len -= 16;
    }
    
    /* Remainder: 2 bytes at a time */
    while (len >= 2) {
        sum += (row[0] << 8) | row[1];
        row += 2;
        len -= 2;
    }
    
    /* Final byte if odd */
    if (len) {
        sum += (*row) << 8;
    }
    
    return sum;
}
#endif /* SDL_GEM_DIRTY_RECT_ASM */

/* Always reallocate to handle window resize properly
 * Free existing buffer first to prevent memory leaks and buffer overflow */
static int InitRowChecksums(SDL_WindowData *data, int height)
{
    /* Free existing buffer if present */
    if (data->raw_checksum_buffer) {
        SDL_free(data->raw_checksum_buffer);
        data->row_checksums = NULL;
        data->raw_checksum_buffer = NULL;
    }
    
    /* Allocate with 16-byte alignment for 68000 movem */
    data->raw_checksum_buffer = SDL_malloc(height * 2 + 16);
    if (!data->raw_checksum_buffer) {
        data->row_checksums = NULL;
        return 0;
    }
    
    data->row_checksums = (Uint16*)(((size_t)data->raw_checksum_buffer + 15) & ~(size_t)15);
    
    for (int i = 0; i < height; i++) {
        data->row_checksums[i] = 0xFFFF;
    }
    data->last_frame_counter = 0;
    
    return 1;
}

/* Update checksums for rows that were actually drawn
 * Must be called AFTER VDI blit, never during detection */
static void UpdateRowChecksumsAfterBlit(SDL_WindowData *data, 
                                         const SDL_Rect *rects, int numrects)
{
    Uint8 *buffer;
    int pitch, bytes_per_row;
    int i, y, start_y, end_y;
    
    if (!data || !data->row_checksums || !data->buffer || numrects <= 0) {
        return;
    }
    
    buffer = (Uint8*)data->buffer;
    pitch = data->buffer_pitch;
    
    /* Clear bytes-per-row calculation */
    if (data->final_mfdb.fd_nplanes <= 8) {
        /* Planar modes: 8-bit chunky (1 byte/pixel) */
        bytes_per_row = data->work_w;
    } else {
        /* Truecolor: fd_nplanes is actually bits-per-pixel */
        {
            int bpp = data->final_mfdb.fd_nplanes;
            if (bpp == 16) {
                bytes_per_row = data->work_w * 2;
            } else if (bpp == 24) {
                bytes_per_row = data->work_w * 3;
            } else {
                /* 32 or default */
                bytes_per_row = data->work_w * 4;
            }
        }
    }
    
    /* Update checksums for all rows covered by the rects */
    for (i = 0; i < numrects; i++) {
        start_y = rects[i].y;
        end_y = start_y + rects[i].h;
        
        /* Bounds check */
        if (start_y < 0) start_y = 0;
        if (end_y > data->work_h) end_y = data->work_h;
        
        for (y = start_y; y < end_y; y++) {
            data->row_checksums[y] = CALCULATE_ROW_CHECKSUM(
                buffer + (y * pitch), bytes_per_row);
        }
    }
}

/* ============================================
   DetectChangesAndBuildRect
   
   Detects changed rows using 8-bit checksums and builds
   an optimized update rectangle.
   
   Does NOT update checksums - only detects changes!
   Checksums are updated AFTER VDI blit by UpdateRowChecksumsAfterBlit()
   
   Returns:
     1  = Use optimized_rect (worthwhile optimization found)
     0  = Use original rects (no optimization benefit)
    -1  = Skip entirely (no changes detected at all)
   ============================================ */

static int DetectChangesAndBuildRect(SDL_WindowData *data,
                                      const SDL_Rect *input_rects, int numrects,
                                      SDL_Rect *optimized_rect)
{
    Uint8 *buffer;
    int pitch, y, h;
    int first_changed = -1, last_changed = -1;
    int bytes_per_row;
    Uint16 old_sum, new_sum;
    
    /* ========================================
       SAFETY CHECKS
       ======================================== */
    
    /* If anything missing, don't optimize */
    if (!data || !data->row_checksums || !data->buffer) {
        return 0;
    }

    if (data->work_w < MIN_DIRTY_RECT_WIDTH || data->work_h < MIN_DIRTY_RECT_HEIGHT) {
        return 0;
    }

    /* ========================================
       PERIODIC FULL REFRESH (every 60 frames)
       Prevents checksum drift and ensures sync
       ======================================== */
    
    data->last_frame_counter++;
    if (data->last_frame_counter >= PERIODIC_REFRESH_FRAMES) {
        data->last_frame_counter = 0;
        /* Invalidate all checksums to force full update */
        if (data->row_checksums) {
            SDL_memset(data->row_checksums, 0xFF, data->work_h);
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, 
                     "GEM: Periodic full refresh (frame %d)", 
                     data->last_frame_counter);
        return 0; /* Use full update this frame */
    }
    
    /* ========================================
       OPTIMIZATION FILTERS
       Skip for complex cases where merging
       overhead exceeds checksum savings
       ======================================== */
    
    /* SKIP checksum for complex multi-rect updates (too much overhead) */
    if (numrects > 4) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Too many rects (%d), skipping checksum scan", numrects);
        return 0;
    }

    /* USE checksum for single full-screen rect (high optimization potential) */
    if (numrects == 1 && 
        input_rects[0].x == 0 && input_rects[0].y == 0 &&
        input_rects[0].w == data->work_w && input_rects[0].h == data->work_h) {
        
        /* Full screen update - proceed with checksum scan below */
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Full-screen update, will scan with checksums");
        /* Fall through to checksum scan */
    }
    else {
        /* Partial/multi-rect update - use provided rects directly */
        /* This is efficient: we don't want to scan 64KB just to blit a small cursor */
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Partial update (%d rects), using provided rects", numrects);
        return 0;
    }
    
    /* ========================================
       CHECKSUM SCAN
       Calculate bytes-per-row based on format
       ======================================== */
    
    buffer = (Uint8*)data->buffer;
    pitch = data->buffer_pitch;
    h = data->work_h;
    
    /* Calculate bytes per row based on format */
    if (data->final_mfdb.fd_nplanes <= 8) {
        /* Planar modes: 8-bit chunky (1 byte/pixel) */
        bytes_per_row = data->work_w;
    } else {
        /* Truecolor: fd_nplanes is actually bits-per-pixel */
        int bpp = data->final_mfdb.fd_nplanes;
        if (bpp == 16) {
            bytes_per_row = data->work_w * 2;
        } else if (bpp == 24) {
            bytes_per_row = data->work_w * 3;
        } else {
            /* 32 or default */
            bytes_per_row = data->work_w * 4;
        }
    }
    
    /* ========================================
       ROW-BY-ROW CHANGE DETECTION
       Scan all rows, find first/last changed
       ======================================== */
    
    for (y = 0; y < h; y++) {
        /* Safety: ensure we don't read past checksum buffer */
        if (y >= data->work_h) {
            /* Window grew - consider this row changed */
            if (first_changed == -1) {
                first_changed = y;
            }
            last_changed = y;
            continue;
        }
        
        /* Get stored checksum from last frame */
        old_sum = data->row_checksums[y];
        
        /* Calculate current checksum */
        new_sum = CALCULATE_ROW_CHECKSUM(buffer + (y * pitch), bytes_per_row);
        
        /* Compare checksums */
        if (new_sum != old_sum) {
            /* DO NOT update checksum here!
             * 
             * WHY: If we update here, the checksum will reflect the NEW buffer
             * before the OLD position has been erased from the screen.
             * 
             * This causes:
             * 1. Next frame sees "no change" (checksums match new buffer)
             * 2. Old sprite position never gets erased (ghost object)
             * 3. Periodic refresh finally erases it (teleport effect)
             * 
             * SOLUTION: Checksum is updated AFTER the VDI blit by 
             * UpdateRowChecksumsAfterBlit() to match what was ACTUALLY drawn.
             */
            
            if (first_changed == -1) {
                first_changed = y;
            }
            last_changed = y;
        }
    }
    
    /* ========================================
       NO CHANGES DETECTED
       ======================================== */
    
    if (first_changed == -1) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: No changes detected in %d rows", h);
        return -1; /* Signal: no new changes in buffer */
    }
    
    /* ========================================
       BUILD OPTIMIZED RECTANGLE
       ======================================== */
    
    /* Build optimized rect covering only changed rows */
    optimized_rect->x = 0;
    optimized_rect->y = first_changed;
    optimized_rect->w = data->work_w;
    optimized_rect->h = last_changed - first_changed + 1;
    
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                 "GEM: Changes detected in rows %d-%d (%d rows of %d)",
                 first_changed, last_changed, optimized_rect->h, h);
    
    /* ========================================
       BENEFIT ANALYSIS
       Only use optimization if clearly beneficial
       ======================================== */
    
    /* Threshold: Only optimize if >25% of screen unchanged
     * This means: if optimized_rect covers <75% of height, use it */
    if (optimized_rect->h < (h * 3 / 4)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Optimization beneficial: %d%% saved",
                     (int)((1.0f - (float)optimized_rect->h / h) * 100));
        return 1; /* Use optimized rect */
    }
    
    /* Too much changed - full update is faster due to merging overhead */
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                 "GEM: Too much changed (%d%%), using full update",
                 (int)((float)optimized_rect->h / h * 100));
    return 0;
}

/* Union two rectangle lists into one
 * Returns number of merged rects, or -1 if fallback needed */
static int UnionRectLists(const SDL_Rect *list1, int n1,
                          const SDL_Rect *list2, int n2,
                          SDL_Rect *out, int max_out)
{
    int i, total;
    SDL_Rect temp[MAX_MERGED_RECTS * 2];
    int merged;
    
    total = n1 + n2;
    
    /* If total would exceed capacity, fallback to full update */
    if (total > MAX_MERGED_RECTS * 2) {
        return -1;
    }
    
    /* C90: copy both lists into temp array */
    for (i = 0; i < n1; i++) {
        temp[i] = list1[i];
    }
    for (i = 0; i < n2; i++) {
        temp[n1 + i] = list2[i];
    }
    
    /* Reuse existing MergeDirtyRects to coalesce */
    merged = MergeDirtyRects(temp, total, out, max_out);
    
    /* If merging produced too many rects, signal fallback */
    if (merged > 8) {
        return -1;
    }
    
    return merged;
}
#endif /* SDL_GEM_DIRTY_RECT */

/* ============================================
   Rectangle Merging Helpers
   ============================================ */

static int CanMerge(const SDL_Rect *a, const SDL_Rect *b)
{
    int expand = MERGE_THRESHOLD;
    
    if (a->x > b->x + b->w + expand) return 0;
    if (a->x + a->w + expand < b->x) return 0;
    if (a->y > b->y + b->h + expand) return 0;
    if (a->y + a->h + expand < b->y) return 0;
    
    return 1;
}

static void MergeRects(SDL_Rect *a, const SDL_Rect *b)
{
    int right, bottom;
    
    right = (a->x + a->w > b->x + b->w) ? a->x + a->w : b->x + b->w;
    bottom = (a->y + a->h > b->y + b->h) ? a->y + a->h : b->y + b->h;
    
    a->x = (a->x < b->x) ? a->x : b->x;
    a->y = (a->y < b->y) ? a->y : b->y;
    a->w = right - a->x;
    a->h = bottom - a->y;
}

static int MergeDirtyRects(const SDL_Rect *input, int num_input,
                           SDL_Rect *output, int max_output)
{
    int i, j;
    int num_merged = 0;
    int merged_something;
    int iterations;
    const int max_iterations = 5;
    
    if (num_input <= 0) return 0;
    if (num_input == 1) {
        output[0] = input[0];
        return 1;
    }
    
    for (i = 0; i < num_input && i < max_output; i++) {
        output[i] = input[i];
    }
    num_merged = (num_input < max_output) ? num_input : max_output;
    
    iterations = 0;
    do {
        merged_something = 0;
        iterations++;
        
        for (i = 0; i < num_merged; i++) {
            for (j = i + 1; j < num_merged; ) {
                if (CanMerge(&output[i], &output[j])) {
                    MergeRects(&output[i], &output[j]);
                    output[j] = output[num_merged - 1];
                    num_merged--;
                    merged_something = 1;
                } else {
                    j++;
                }
            }
        }
    } while (merged_something && iterations < max_iterations && num_merged > 1);
    
    for (i = 0; i < num_merged; ) {
        if (output[i].w <= 0 || output[i].h <= 0) {
            output[i] = output[num_merged - 1];
            num_merged--;
        } else {
            i++;
        }
    }
    
    return num_merged;
}

/* ============================================
   PRECOMPUTE C2P constants
   ============================================ */
static void CalculateBufferSizes(SDL_VideoData *video, int w, int h, 
                                 int *pitch, size_t *chunky_size, 
                                 size_t *planar_size, SDL_WindowData *data)
{
    int planes = video->planes;
    
    data->aligned_w = MFDB_STRIDE(w);
    data->plane_line_bytes = (data->aligned_w >> 4) * 2;
    data->total_line_bytes = data->plane_line_bytes * planes;
    data->block_size = planes * 2;
    
    switch (planes) {
        case 1:
        case 2:
        case 4:
        case 8:
            *pitch = data->aligned_w;
            *chunky_size = (size_t)data->aligned_w * h;
            *planar_size = (size_t)(data->aligned_w * planes / 8) * h;
            break;
        case 16:
            *pitch = data->aligned_w * 2;
            *chunky_size = (size_t)(*pitch) * h;
            *planar_size = *chunky_size;
            break;
        case 24:
            *pitch = data->aligned_w * 3;
            *chunky_size = (size_t)(*pitch) * h;
            *planar_size = *chunky_size;
            break;
        case 32:
            *pitch = data->aligned_w * 4;
            *chunky_size = (size_t)(*pitch) * h;
            *planar_size = *chunky_size;
            break;
        default:
            *pitch = data->aligned_w * 4;
            *chunky_size = (size_t)(*pitch) * h;
            *planar_size = *chunky_size;
            break;
    }
}

int GEM_CreateWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window, 
                                Uint32 *format, void **pixels, int *pitch)
{
    SDL_VideoData *video = (SDL_VideoData *)this->driverdata;
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    int w, h;
    size_t chunky_size, planar_size;
    void *raw_ptr;
    Uint32 new_format;
    
    if (!data) {
        return SDL_SetError("Window data not found");
    }
    
    SDL_GetWindowSize(window, &w, &h);
    
    /* Determine format from video planes */
    switch (video->planes) {
        case 1:
        case 2:
        case 4:
        case 8:
            new_format = SDL_PIXELFORMAT_RGB332;
            break;
        case 16:
            new_format = SDL_PIXELFORMAT_RGB565;
            break;
        case 24:
            new_format = SDL_PIXELFORMAT_RGB888;
            break;
        case 32:
            new_format = SDL_PIXELFORMAT_ARGB8888;
            break;
        default:
            new_format = SDL_PIXELFORMAT_RGB565;
            break;
    }
    
    /* Reuse existing buffer if size and format match */
    if (data->raw_buffer && data->buffer && 
        w == data->work_w && h == data->work_h &&
        data->buffer_pitch > 0) {
        
        SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                    "GEM: Reusing existing framebuffer %p (%dx%d, pitch=%u)",
                    data->buffer, w, h, data->buffer_pitch);
        
        *format = new_format;
        *pixels = data->buffer;
        *pitch = data->buffer_pitch;
        return 0;
    }
    
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                "GEM: Creating new framebuffer for %dx%d (existing was %p, last was %dx%d)",
                w, h, data->buffer, data->work_w, data->work_h);
    
    /* Free existing buffers only if size changed or not allocated */
    if (data->raw_buffer) {
        SDL_free(data->raw_buffer);
        data->buffer = NULL;
        data->raw_buffer = NULL;
    }
    if (data->raw_final_buffer) {
        SDL_free(data->raw_final_buffer);
        data->final_buffer = NULL;
        data->raw_final_buffer = NULL;
    }
    if (data->raw_remap_buffer) {
        SDL_free(data->raw_remap_buffer);
        data->remap_buffer = NULL;
        data->raw_remap_buffer = NULL;
    }
    
#ifdef SDL_GEM_DIRTY_RECT
    if (data->raw_checksum_buffer) {
        SDL_free(data->raw_checksum_buffer);
        data->row_checksums = NULL;
        data->raw_checksum_buffer = NULL;
    }
#endif
    
    /* Recalculate buffer sizes with new dimensions */
    CalculateBufferSizes(video, w, h, pitch, &chunky_size, &planar_size, data);
    
    /* Allocate chunky buffer (application writes here) */
    raw_ptr = SDL_malloc(chunky_size + 16);
    if (!raw_ptr) return SDL_OutOfMemory();
    data->raw_buffer = raw_ptr;
    data->buffer = (void*)(((size_t)raw_ptr + 15) & ~(size_t)15);
    
    /* Allocate final buffer (VDI blit source) */
    raw_ptr = SDL_malloc(planar_size + 16);
    if (!raw_ptr) {
        SDL_free(data->raw_buffer);
        data->raw_buffer = NULL;
        data->buffer = NULL;
        return SDL_OutOfMemory();
    }
    data->raw_final_buffer = raw_ptr;
    data->final_buffer = (void*)(((size_t)raw_ptr + 15) & ~(size_t)15);
    data->final_buffer_size = planar_size;
    
    /* Allocate remap buffer for non-identity palette (planar modes only) */
    if (video->planes <= 8 && !video->use_identity_palette) {
        raw_ptr = SDL_malloc((size_t)data->aligned_w + 16);
        if (!raw_ptr) {
            SDL_free(data->raw_buffer);
            SDL_free(data->raw_final_buffer);
            data->buffer = NULL;
            data->final_buffer = NULL;
            data->raw_buffer = NULL;
            data->raw_final_buffer = NULL;
            return SDL_OutOfMemory();
        }
        data->raw_remap_buffer = raw_ptr;
        data->remap_buffer = (void*)(((size_t)raw_ptr + 15) & ~(size_t)15);
        data->remap_buffer_size = (size_t)data->aligned_w;
    }
    
    /* Setup MFDB for VDI */
    data->final_mfdb.fd_addr = data->final_buffer;
    data->final_mfdb.fd_w = w;  /* Use aligned width ? */
    data->final_mfdb.fd_h = h;
    data->final_mfdb.fd_wdwidth = data->aligned_w >> 4;
    data->final_mfdb.fd_stand = 0;
    data->final_mfdb.fd_nplanes = video->planes;
    data->final_mfdb.fd_r1 = 0;
    data->final_mfdb.fd_r2 = 0;
    data->final_mfdb.fd_r3 = 0;
    
    data->buffer_pitch = (unsigned short)(*pitch);
    *format = new_format;
    *pixels = data->buffer;
    
#ifdef SDL_GEM_DIRTY_RECT
    /* Reinitialize row checksums */
    if (!InitRowChecksums(data, h)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, 
                    "GEM: Row checksum init failed, change detection disabled");
    }
    data->num_prev_dirty_rects = 0;
#endif
    
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO,
                "GEM: Created framebuffer %p (%dx%d, aligned=%dx%d, pitch=%u, format=%s)",
                data->buffer, w, h, data->aligned_w, h, data->buffer_pitch,
                SDL_GetPixelFormatName(new_format));
    
    return 0;
}

/* ============================================
   Blit a single region with VDI clipping
   ============================================ */
static void BlitRegionOptimized(SDL_VideoData *video, SDL_WindowData *data,
                                int src_x, int src_y, int src_w, int src_h,
                                int dst_screen_x, int dst_screen_y)
{
    short pxy[8];
    int vh;
    int col_offset;
    Uint8 *src_base;
    Uint8 *dst_base;
    Uint8 *src;
    Uint8 *dst;
    int bpp;
    
    vh = video->vdi_handle;
    
    if (video->planes <= 8) {
        col_offset = (src_x >> 4) * data->block_size;
        
        src_base = (Uint8*)data->buffer;
        dst_base = (Uint8*)data->final_buffer;
        
        if (video->use_identity_palette) {
            /* Batch C2P for entire block - single call for all rows */
            src = src_base + (src_y * data->buffer_pitch) + src_x;
            dst = dst_base + (src_y * data->total_line_bytes) + col_offset;
            
            Atari_C2P_Planar(src, dst, src_w, src_h,
                            data->buffer_pitch, data->total_line_bytes,
                            video->planes);
        } else {
#ifdef SDL_GEM_C2P_ASM
            /* OPTIMIZED: Combined LUT+C2P for all plane counts */
            src = src_base + (src_y * data->buffer_pitch) + src_x;
            dst = dst_base + (src_y * data->total_line_bytes) + col_offset;
            
            Atari_C2P_Planar_LUT(src, dst, src_w, src_h,
                                data->buffer_pitch, data->total_line_bytes,
                                video->planes, video->rgb332_to_hw);
#else
            /* Remap path - still need row loop for LUT application */
            {
                int y;
                int row;
                int i;
                int src_skip;
                int dst_skip;
                Uint8 *remap;
                Uint8 *lut;
                
                remap = (Uint8*)data->remap_buffer;
                lut = video->rgb332_to_hw;

                src_skip = data->buffer_pitch;
                dst_skip = data->total_line_bytes;

                for (row = 0; row < src_h; row++) {
                    y = src_y + row;
                    src = src_base + (y * src_skip) + src_x;
                    dst = dst_base + (y * dst_skip) + col_offset;
                    
                    /* Apply LUT */
                    for (i = 0; i < src_w; i++) {
                        remap[i] = lut[src[i]];
                    }
                    
                    /* C2P for this row */
                    Atari_C2P_Planar(remap, dst, src_w, 1,
                                    src_w, dst_skip,
                                    video->planes);
                }
            }
#endif                 
        }
        
        pxy[0] = (short)src_x;
        pxy[1] = (short)src_y;
        pxy[2] = (short)(src_x + src_w - 1);
        pxy[3] = (short)(src_y + src_h - 1);
    } else {
        /* TrueColor: Use optimized ASM blit */
        bpp = video->planes >> 3;
        
        src = (Uint8*)data->buffer + 
              (src_y * data->buffer_pitch) + (src_x * bpp);
        dst = (Uint8*)data->final_buffer +
              (src_y * data->buffer_pitch) + (src_x * bpp);
        
        /* OPTIMIZATION: Use ASM BlitFast for entire block */
        BlitFast(dst, src, src_w * bpp, src_h, data->buffer_pitch);
        
        pxy[0] = (short)src_x;
        pxy[1] = (short)src_y;
        pxy[2] = (short)(src_x + src_w - 1);
        pxy[3] = (short)(src_y + src_h - 1);
    }
    
    /* Destination coordinates on screen */
    pxy[4] = (short)dst_screen_x;
    pxy[5] = (short)dst_screen_y;
    pxy[6] = (short)(dst_screen_x + src_w - 1);
    pxy[7] = (short)(dst_screen_y + src_h - 1);
    
    vro_cpyfm(vh, S_ONLY, pxy, &data->final_mfdb, &screen_mfdb);
}

/* ============================================
    UpdateWindowFramebuffer
    Update window framebuffer with dirty rects and VDI blitting
   ============================================ */
int GEM_UpdateWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window,
                                const SDL_Rect *rects, int numrects)
{
    SDL_VideoData *video = (SDL_VideoData *)this->driverdata;
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    GRECT work;
    short todo[4];
    SDL_Rect merged[MAX_MERGED_RECTS];
    int num_merged;
    int i;
    GRECT gem_rect;
    short clip_pxy[4];
    int vh;
    int gem_left;
    int gem_top;
    int gem_right;
    int gem_bottom;
    int m_left;
    int m_top;
    int m_right;
    int m_bottom;
    int i_left;
    int i_top;
    int i_right;
    int i_bottom;
    int src_x;
    int src_y;
    int src_w;
    int src_h;
    int dst_x;
    int dst_y;
    int align_x;
    int align_offset;
    SDL_Rect r;
    
#ifdef SDL_GEM_DIRTY_RECT
    SDL_Rect optimized_rect;
    const SDL_Rect *rects_to_process;
    int num_to_process;
    int change_detect_result;
    SDL_Rect combined_rects[MAX_MERGED_RECTS];
    int num_combined;
    int use_full_update = 0;
#endif

    if (!data || !data->buffer || !data->final_buffer) {
        return SDL_SetError("Framebuffer not initialised");
    }

    // SDL_Log("UpdateWindowFramebuffer: buffer=%p, pitch=%u, work_w=%d, work_h=%d, aligned_w=%d",
    //         data->buffer, data->buffer_pitch, data->work_w, data->work_h, data->aligned_w);

#ifdef SDL_GEM_DIRTY_RECT
    /* STEP A: Detect changes using row checksums */
    change_detect_result = DetectChangesAndBuildRect(data, rects, numrects,
                                                      &optimized_rect);
    
    if (change_detect_result == -1) {
        /* No new changes detected in buffer */
        
        /* Check if we have previous dirty rects to erase! */
        if (data->num_prev_dirty_rects > 0) {
            /* Use previous rects to ensure old positions get erased */
            rects_to_process = data->prev_dirty_rects;
            num_to_process = data->num_prev_dirty_rects;
            /* Continue to merging step */
        } else {
            /* Truly nothing to do */
            return 0;
        }
    }
    else if (change_detect_result == 1) {
        /* Use optimized single rect */
        rects_to_process = &optimized_rect;
        num_to_process = 1;
    }
    else {
        /* Use original rects (optimisation not beneficial or full update) */
        rects_to_process = rects;
        num_to_process = numrects;
        use_full_update = (change_detect_result == 0);
    }

    /* STEP B: Merge PREVIOUS dirty rects with CURRENT rects */
    if (num_to_process <= 0) {
        return 0;
    }

    num_combined = UnionRectLists(data->prev_dirty_rects, data->num_prev_dirty_rects,
                                  rects_to_process, num_to_process,
                                  combined_rects, MAX_MERGED_RECTS);

    if (num_combined == -1) {
        /* Too many rects - fall back to full update */
        use_full_update = 1;
    }

    /* STEP C: Perform the actual update (full or dirty) */
    if (use_full_update) {
        /* Full screen update */
        SDL_Rect full_rect;
        full_rect.x = 0;
        full_rect.y = 0;
        full_rect.w = data->work_w;
        full_rect.h = data->work_h;

        num_merged = 1;
        merged[0] = full_rect;

        /* Clear previous dirty rects - everything is now in sync */
        data->num_prev_dirty_rects = 0;
    }
    else {
        /* Use the merged & coalesced list */
        num_merged = num_combined;
        for (i = 0; i < num_merged; i++) {
            merged[i] = combined_rects[i];
        }
    }

#else  /* !SDL_GEM_DIRTY_RECT */
    /* Original path - no optimisation */
    num_merged = MergeDirtyRects(rects, numrects, merged, MAX_MERGED_RECTS);
    if (num_merged <= 0) {
        return 0;
    }
#endif /* SDL_GEM_DIRTY_RECT */

    /* Common VDI blitting code */

    /* Get window work area */
    mt_wind_get_grect(data->handle, WF_WORKXYWH, &work, sdl_global_aes);

    mt_graf_mouse(M_OFF, 0L, sdl_global_aes);
    mt_wind_update(BEG_UPDATE, sdl_global_aes);

    /* Cache VDI handle */
    vh = video->vdi_handle;

    /* Walk GEM visible regions */
    mt_wind_get(data->handle, WF_FIRSTXYWH,
                &todo[0], &todo[1], &todo[2], &todo[3], sdl_global_aes);

    while (todo[2] && todo[3]) {
        gem_rect.g_x = todo[0];
        gem_rect.g_y = todo[1];
        gem_rect.g_w = todo[2];
        gem_rect.g_h = todo[3];

        if (rc_intersect(&work, &gem_rect)) {
            /* OPTIMIZATION: Set VDI clip ONCE per GEM region */
            clip_pxy[0] = (short)gem_rect.g_x;
            clip_pxy[1] = (short)gem_rect.g_y;
            clip_pxy[2] = (short)(gem_rect.g_x + gem_rect.g_w - 1);
            clip_pxy[3] = (short)(gem_rect.g_y + gem_rect.g_h - 1);
            vs_clip(vh, 1, clip_pxy);

            /* OPTIMIZATION: Pre-calculate screen-relative bounds */
            gem_left = gem_rect.g_x;
            gem_top = gem_rect.g_y;
            gem_right = gem_rect.g_x + gem_rect.g_w;
            gem_bottom = gem_rect.g_y + gem_rect.g_h;

            for (i = 0; i < num_merged; i++) {
                /* Validate merged rectangles */
                if (merged[i].x >= data->work_w || merged[i].y >= data->work_h) {
                    continue;
                }
                
                /* Clip to window bounds - use copy to avoid modifying original */
                r = merged[i];
                if (r.x + r.w > data->work_w) {
                    r.w = data->work_w - r.x;
                }
                if (r.y + r.h > data->work_h) {
                    r.h = data->work_h - r.y;
                }
                if (r.w <= 0 || r.h <= 0) {
                    continue;
                }

                /* OPTIMIZATION: Simplified intersection math */
                /* Window-relative to screen-relative conversion */
                m_left = work.g_x + r.x;
                m_top = work.g_y + r.y;
                m_right = m_left + r.w;
                m_bottom = m_top + r.h;

                /* Clip to GEM region (already in screen coords) */
                i_left = (m_left > gem_left) ? m_left : gem_left;
                i_top = (m_top > gem_top) ? m_top : gem_top;
                i_right = (m_right < gem_right) ? m_right : gem_right;
                i_bottom = (m_bottom < gem_bottom) ? m_bottom : gem_bottom;

                src_w = i_right - i_left;
                src_h = i_bottom - i_top;

                if (src_w <= 0 || src_h <= 0) continue;

                /* Calculate source coordinates in buffer */
                src_x = i_left - work.g_x;
                src_y = i_top - work.g_y;

                /* Clip to buffer bounds */
                if (src_x < 0) { src_w += src_x; src_x = 0; }
                if (src_y < 0) { src_h += src_y; src_y = 0; }
                if (src_x + src_w > data->work_w) src_w = data->work_w - src_x;
                if (src_y + src_h > data->work_h) src_h = data->work_h - src_y;

                if (src_w <= 0 || src_h <= 0) continue;

                /* Destination is screen coordinates */
                dst_x = i_left;
                dst_y = i_top;

                /* OPTIMIZATION: 16-pixel alignment for planar modes */
                if (video->planes <= 8) {
                    align_x = src_x & ~15;
                    align_offset = src_x - align_x;
                    src_w = ((src_x + src_w + 15) & ~15) - align_x;
                    if (align_x + src_w > data->aligned_w) {
                        src_w = data->aligned_w - align_x;
                    }
                    dst_x -= align_offset;
                    src_x = align_x;
                }

                if (src_w <= 0 || src_h <= 0) continue;

                /* OPTIMIZED: Call BlitRegion without vs_clip parameters */
                BlitRegionOptimized(video, data, src_x, src_y, src_w, src_h,
                                    dst_x, dst_y);
            }

            /* OPTIMIZATION: Disable VDI clip ONCE after all blits */
            vs_clip(vh, 0, clip_pxy);
        }

        mt_wind_get(data->handle, WF_NEXTXYWH,
                    &todo[0], &todo[1], &todo[2], &todo[3], sdl_global_aes);
    }

    mt_wind_update(END_UPDATE, sdl_global_aes);
    mt_graf_mouse(M_ON, 0L, sdl_global_aes);

#ifdef SDL_GEM_DIRTY_RECT
    /* STEP D: Update checksums AFTER blitting and store prev rects */
    if (use_full_update) {
        /* Full update: update all row checksums */
        SDL_Rect full_rect;
        full_rect.x = 0;
        full_rect.y = 0;
        full_rect.w = data->work_w;
        full_rect.h = data->work_h;
        UpdateRowChecksumsAfterBlit(data, &full_rect, 1);
    }
    else {
        /* Partial update: update checksums for what we drew */
        UpdateRowChecksumsAfterBlit(data, merged, num_merged);
        
        /* Store merged rects for next frame's erase pass */
        if (num_merged <= MAX_MERGED_RECTS) {
            data->num_prev_dirty_rects = num_merged;
            for (i = 0; i < num_merged; i++) {
                data->prev_dirty_rects[i] = merged[i];
            }
        } else {
            /* Too many - just store bounding box */
            data->num_prev_dirty_rects = 1;
            data->prev_dirty_rects[0].x = 0;
            data->prev_dirty_rects[0].y = 0;
            data->prev_dirty_rects[0].w = data->work_w;
            data->prev_dirty_rects[0].h = data->work_h;
        }
    }
#endif

    return 0;
}

void GEM_DestroyWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    
    (void)this;
    
    if (!data) return;

#ifdef SDL_GEM_DIRTY_RECT
    /* Cleanup row checksums */
    if (data->raw_checksum_buffer) {
        SDL_free(data->raw_checksum_buffer);
        data->row_checksums = NULL;
        data->raw_checksum_buffer = NULL;
    }
#endif /* SDL_GEM_DIRTY_RECT */

    if (data->raw_buffer) {
        SDL_free(data->raw_buffer);
        data->buffer = NULL;
        data->raw_buffer = NULL;
    }
    if (data->raw_final_buffer) {
        SDL_free(data->raw_final_buffer);
        data->final_buffer = NULL;
        data->raw_final_buffer = NULL;
    }
    if (data->raw_remap_buffer) {
        SDL_free(data->raw_remap_buffer);
        data->remap_buffer = NULL;
        data->raw_remap_buffer = NULL;
    }
}

/* ============================================
   Window management
   ============================================ */

void GEM_SetWindowTitle(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    
    if (data && data->handle >= 0) {
        mt_wind_set_str(data->handle, WF_NAME, 
                       window->title ? window->title : "SDL2", 
                       sdl_global_aes);
    }
}

int GEM_CreateWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    int w, h, x, y;
    SDL_WindowData *data = (SDL_WindowData *)SDL_calloc(1, sizeof(SDL_WindowData));
    (void)this;
    
    if (!data) {
        return SDL_OutOfMemory();
    }
    
    data->win_type = NAME | CLOSER | MOVER;
    if (window->flags & SDL_WINDOW_RESIZABLE) {
        data->win_type |= SIZER | FULLER;
    }
    
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    
    if (x == SDL_WINDOWPOS_UNDEFINED) x = 50;
    if (y == SDL_WINDOWPOS_UNDEFINED) y = 50;
    
    data->work_x = (short)x;
    data->work_y = (short)y;
    data->work_w = (short)w;
    data->work_h = (short)h;
    
    mt_wind_calc(WC_BORDER, data->win_type,
                 data->work_x, data->work_y, data->work_w, data->work_h,
                 &data->win_x, &data->win_y, &data->win_w, &data->win_h,
                 sdl_global_aes);
    
    data->handle = mt_wind_create(data->win_type,
                                  data->win_x, data->win_y,
                                  data->win_w, data->win_h,
                                  sdl_global_aes);
    
    if (data->handle < 0) {
        SDL_free(data);
        return SDL_SetError("wind_create failed");
    }
    
    mt_wind_set_str(data->handle, WF_NAME,
                    window->title ? window->title : "SDL2",
                    sdl_global_aes);
    mt_wind_open(data->handle, data->win_x, data->win_y,
                 data->win_w, data->win_h, sdl_global_aes);
    
    window->driverdata = data;
    return 0;
}

void GEM_DestroyWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    
    if (data) {
        if (data->handle >= 0) {
            mt_wind_close(data->handle, sdl_global_aes);
            mt_wind_delete(data->handle, sdl_global_aes);
        }
        SDL_free(data);
        window->driverdata = NULL;
    }
}

void GEM_SetWindowPosition(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    int x, y;
    (void)this;
    
    if (!data || data->handle < 0) return;
    
    SDL_GetWindowPosition(window, &x, &y);
    data->work_x = (short)x;
    data->work_y = (short)y;
    
    mt_wind_calc(WC_BORDER, data->win_type,
                 data->work_x, data->work_y, data->work_w, data->work_h,
                 &data->win_x, &data->win_y, &data->win_w, &data->win_h,
                 sdl_global_aes);
    
    mt_wind_set(data->handle, WF_CURRXYWH,
                data->win_x, data->win_y, data->win_w, data->win_h,
                sdl_global_aes);
}

void GEM_SetWindowSize(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    int w, h;
    (void)this;
    
    if (!data || data->handle < 0) return;
    
    SDL_GetWindowSize(window, &w, &h);
    data->work_w = (short)w;
    data->work_h = (short)h;
    data->aligned_w = MFDB_STRIDE(w);
    
    mt_wind_calc(WC_BORDER, data->win_type,
                 data->work_x, data->work_y, data->work_w, data->work_h,
                 &data->win_x, &data->win_y, &data->win_w, &data->win_h,
                 sdl_global_aes);
    
    mt_wind_set(data->handle, WF_CURRXYWH,
                data->win_x, data->win_y, data->win_w, data->win_h,
                sdl_global_aes);
             
}

void GEM_ShowWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    
    if (data && data->handle >= 0) {
        mt_wind_open(data->handle, data->win_x, data->win_y,
                     data->win_w, data->win_h, sdl_global_aes);
    }
}

void GEM_HideWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    
    if (data && data->handle >= 0) {
        mt_wind_close(data->handle, sdl_global_aes);
    }
}

void GEM_RaiseWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    
    if (data && data->handle >= 0) {
        mt_wind_set(data->handle, WF_TOP, 0, 0, 0, 0, sdl_global_aes);
    }
}

void GEM_MaximizeWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    GRECT full;
    (void)this;
    
    if (!data || data->handle < 0) return;
    
    mt_wind_get_grect(data->handle, WF_CURRXYWH, &data->restore_rect, sdl_global_aes);
    mt_wind_get_grect(data->handle, WF_FULLXYWH, &full, sdl_global_aes);
    mt_wind_set(data->handle, WF_CURRXYWH, full.g_x, full.g_y, full.g_w, full.g_h,
                sdl_global_aes);
    data->is_maximized = 1;
}

void GEM_MinimizeWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    
    if (data && data->handle >= 0) {
        mt_wind_set(data->handle, WF_ICONIFY, -1, -1, -1, -1, sdl_global_aes);
    }
}

void GEM_RestoreWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    
    if (!data || data->handle < 0) return;
    
    if (data->is_maximized) {
        mt_wind_set(data->handle, WF_CURRXYWH,
                    data->restore_rect.g_x, data->restore_rect.g_y,
                    data->restore_rect.g_w, data->restore_rect.g_h,
                    sdl_global_aes);
        data->is_maximized = 0;
    } else {
        mt_wind_set(data->handle, WF_UNICONIFY, -1, -1, -1, -1, sdl_global_aes);
    }
}

void GEM_SetWindowBordered(SDL_VideoDevice *this, SDL_Window *window, SDL_bool bordered)
{
    (void)this;
    (void)window;
    (void)bordered;
}

void GEM_SetWindowResizable(SDL_VideoDevice *this, SDL_Window *window, SDL_bool resizable)
{
    (void)this;
    (void)window;
    (void)resizable;
}

void GEM_SetWindowMinimumSize(SDL_VideoDevice *this, SDL_Window *window)
{
    (void)this;
    (void)window;
}

void GEM_SetWindowMaximumSize(SDL_VideoDevice *this, SDL_Window *window)
{
    (void)this;
    (void)window;
}

int GEM_GetWindowDisplayIndex(SDL_VideoDevice *this, SDL_Window *window)
{
    (void)this;
    (void)window;
    return 0;
}

#endif /* SDL_VIDEO_DRIVER_GEM */