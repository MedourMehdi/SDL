/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

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
   SDL_gemwindow.c – GEM window, framebuffer and C2P dispatch
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Responsibilities: window creation/destruction, framebuffer allocation,
   dirty-rect detection (optional SDL_GEM_DIRTY_RECT), C2P dispatch via
   Atari_C2P_Planar_LUT, and all AES window management calls.

   Key design decisions:
   - Buffers are reused across resizes when size is unchanged.
   - Checksum width uses aligned_w consistently to avoid mismatch bugs.
   - All C2P paths go through the LUT; no shift-based fallback at runtime.

   C90 compliant.
   ============================================================================ */

#include "SDL_gemvideo.h"
#include "mt_gemx.h"

#ifdef SDL_VIDEO_DRIVER_GEM

#define MFDB_STRIDE(w) (((w) + 15) & ~15)
#define MAX_MERGED_RECTS 16
#define MERGE_THRESHOLD 16
#define PERIODIC_REFRESH_FRAMES 60

MFDB screen_mfdb = {0};

#ifdef SDL_GEM_DIRTY_RECT

static int MergeDirtyRects(const SDL_Rect *input, int num_input,
                           SDL_Rect *output, int max_output);

/* ============================================
   HELPER FUNCTIONS
   ============================================ */

/* Calculate bytes per row for checksum computation
 * CRITICAL: Must match buffer allocation for consistency */
static int CalculateBytesPerRow(const SDL_WindowData *data)
{
    if (data->final_mfdb.fd_nplanes <= 8) {
        /* Planar: buffer allocated with aligned_w pitch
         * FIXES ORIGINAL BUG: DetectChanges used work_w, UpdateAfterBlit used aligned_w */
        return data->aligned_w;
    } else {
        /* TrueColor: work_w * bytes-per-pixel */
        int bpp = data->final_mfdb.fd_nplanes;
        return data->work_w * (bpp == 16 ? 2 : bpp == 24 ? 3 : 4);
    }
}

/* ============================================
   FAST ROW CHANGE DETECTION (68000-optimized)
   ============================================ */

#ifndef SDL_GEM_DIRTY_RECT_ASM
static Uint8 CalculateRowChecksum(const Uint8 *row, int len)
{
    Uint16 sum = 0;
    
    /* Main loop: 16 bytes at a time (8x unrolled) */
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
    
    /* Fold 16-bit to 8-bit with XOR */
    return (Uint8)((sum ^ (sum >> 8)) & 0xFF);
}
#endif /* SDL_GEM_DIRTY_RECT_ASM */

/* Initialize row checksums with buffer reuse optimization */
static int InitRowChecksums(SDL_WindowData *data, int height)
{
    /* Reuse buffer if size unchanged - avoids reallocation */
    if (data->raw_checksum_buffer && data->checksum_height == height) {
        SDL_memset(data->row_checksums, 0xFF, height);
        data->last_frame_counter = 0;  /* Always reset */
        return 1;
    }
    
    /* Free old buffer if exists */
    if (data->raw_checksum_buffer) {
        SDL_free(data->raw_checksum_buffer);
        data->raw_checksum_buffer = NULL;
    }
    
    /* Allocate with 16-byte alignment */
    data->raw_checksum_buffer = SDL_malloc(height + 15);
    if (!data->raw_checksum_buffer) {
        data->row_checksums = NULL;
        return 0;
    }
    
    data->row_checksums = (Uint8*)(((size_t)data->raw_checksum_buffer + 15) & ~(size_t)15);
    data->checksum_height = height;
    
    /* Initialize to force full first frame update */
    SDL_memset(data->row_checksums, 0xFF, height);
    data->last_frame_counter = 0;

#ifdef SDL_GEM_DIRTY_RECT_ASM
    /* Allocate reusable batch buffer for DetectChangesAndBuildRect */
    if (!data->batch_checksums || data->batch_checksums_size < (size_t)height) {
        if (data->batch_checksums) SDL_free(data->batch_checksums);
        data->batch_checksums = (Uint8*)SDL_malloc(height);
        if (data->batch_checksums) {
            data->batch_checksums_size = height;
        } else {
            data->batch_checksums_size = 0;
        }
    }
#endif    

    return 1;
}

/* Update checksums after VDI blit */
static void UpdateRowChecksumsAfterBlit(SDL_WindowData *data, 
                                         const SDL_Rect *rects, int numrects)
{
    Uint8 *buffer;
    int pitch, bytes_per_row, i, y, start_y, end_y;
    const int max_y = data->work_h;
    
    if (!data || !data->row_checksums || !data->buffer || numrects <= 0) {
        return;
    }
    
    buffer = (Uint8*)data->buffer;
    pitch = data->buffer_pitch;
    
    /* Use helper for consistency */
    bytes_per_row = CalculateBytesPerRow(data);
    
    for (i = 0; i < numrects; i++) {
        start_y = rects[i].y;
        end_y = start_y + rects[i].h;
        
        if (start_y < 0) start_y = 0;
        if (end_y > max_y) end_y = max_y;
        
#ifdef SDL_GEM_DIRTY_RECT_ASM
        if (rects[i].x == 0 && rects[i].w == data->work_w) {
            /* Full width: batch process with ASM */
            Atari_CalculateRowChecksumsBlock(
                buffer + ((size_t)start_y * pitch),
                data->row_checksums + start_y,
                end_y - start_y, pitch, bytes_per_row);
        } else
#endif
        {
            /* Partial width: row by row */
            for (y = start_y; y < end_y; y++) {
                data->row_checksums[y] = CALCULATE_ROW_CHECKSUM(
                    buffer + ((size_t)y * pitch), bytes_per_row);
            }
        }
    }
}

/* Detect changes and build optimized rectangle */
static int DetectChangesAndBuildRect(SDL_WindowData *data,
                                      const SDL_Rect *input_rects, int numrects,
                                      SDL_Rect *optimized_rect)
{
    Uint8 *buffer;
    int pitch, y, h, bytes_per_row;
    int first_changed = -1;
    int last_changed = -1;
    Uint8 old_sum, new_sum;
    
    /* Safety checks */
    if (!data || !data->row_checksums || !data->buffer) {
        return 0;
    }

    /* Increment frame counter - happens before all checks */
    data->last_frame_counter++;
    
    /* Periodic full refresh */
    if (data->last_frame_counter >= PERIODIC_REFRESH_FRAMES) {
        data->last_frame_counter = 0;
        SDL_memset(data->row_checksums, 0xFF, data->work_h);
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, 
                     "GEM: Periodic full refresh (frame %d)", 
                     PERIODIC_REFRESH_FRAMES);
        return 0;
    }
    
    /* Skip checksum for complex updates */
    if (numrects > 4) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Too many rects (%d), skipping checksum scan", numrects);
        return 2;
    }

    /* Check for full-screen update */
    if (numrects == 1 && 
        input_rects[0].x == 0 && input_rects[0].y == 0 &&
        input_rects[0].w == data->work_w && input_rects[0].h == data->work_h) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Full-screen update, will scan with checksums");
    }
    else {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Partial update (%d rects), using provided rects", numrects);
        return 2;
    }
    
    /* Initialize scan variables */
    buffer = (Uint8*)data->buffer;
    pitch = data->buffer_pitch;
    h = data->work_h;
    
    /* Use helper for consistency with UpdateRowChecksumsAfterBlit */
    bytes_per_row = CalculateBytesPerRow(data);

#ifdef SDL_GEM_DIRTY_RECT_ASM
    /* Batch calculation for full-screen updates (reusable buffer, no malloc) */
    if (numrects == 1 && 
        input_rects[0].x == 0 && input_rects[0].y == 0 &&
        input_rects[0].w == data->work_w && input_rects[0].h == data->work_h &&
        data->batch_checksums && data->batch_checksums_size >= (size_t)h) {
        
        Atari_CalculateRowChecksumsBlock(buffer, data->batch_checksums, h,
                                         pitch, bytes_per_row);
        for (y = 0; y < h; y++) {
            if (data->batch_checksums[y] != data->row_checksums[y]) {
                if (first_changed == -1) first_changed = y;
                last_changed = y;
            }
        }
        /* CRITICAL: DO NOT update data->row_checksums here - done after blit */
        goto have_result;
    }
    /* Fall through to C path if not full-screen or no batch buffer */
#endif    

    /* Row-by-row change detection */
    for (y = 0; y < h; y++) {
        old_sum = data->row_checksums[y];
        new_sum = CALCULATE_ROW_CHECKSUM(buffer + (y * pitch), bytes_per_row);
        
        if (new_sum != old_sum) {
            if (first_changed == -1) {
                first_changed = y;
            }
            last_changed = y;
        }
    }

#ifdef SDL_GEM_DIRTY_RECT_ASM
have_result:
#endif

    /* No changes detected */
    if (first_changed == -1) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: No changes detected in %d rows", h);
        return -1;
    }
    
    /* Build optimized rect */
    optimized_rect->x = 0;
    optimized_rect->y = first_changed;
    optimized_rect->w = data->work_w;
    optimized_rect->h = last_changed - first_changed + 1;
    
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                 "GEM: Changes detected in rows %d-%d (%d rows of %d)",
                 first_changed, last_changed, optimized_rect->h, h);
    
    /* Benefit analysis */
    if (optimized_rect->h < (h * 3 / 4)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: Optimization beneficial: %d%% saved",
                     (int)((1.0f - (float)optimized_rect->h / h) * 100));
        return 1;
    }
    
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                 "GEM: Too much changed (%d%%), using full update",
                 (int)((float)optimized_rect->h / h * 100));
    return 0;
}

/* Union two rectangle lists */
static int UnionRectLists(const SDL_Rect *list1, int n1,
                          const SDL_Rect *list2, int n2,
                          SDL_Rect *out, int max_out)
{
    int total, merged;
    static SDL_Rect temp[MAX_MERGED_RECTS * 2];
    
    total = n1 + n2;
    
    if (total > MAX_MERGED_RECTS * 2) {
        return -1;
    }
    
    /* Use SDL_memcpy for efficiency */
    SDL_memcpy(temp, list1, n1 * sizeof(SDL_Rect));
    SDL_memcpy(temp + n1, list2, n2 * sizeof(SDL_Rect));
    
    merged = MergeDirtyRects(temp, total, out, max_out);
    
    return (merged > 8) ? -1 : merged;
}
#endif /* SDL_GEM_DIRTY_RECT */

/* ============================================
   Rectangle Merging
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
    int new_x, new_y, new_right, new_bottom;
    
    new_x = (a->x < b->x) ? a->x : b->x;
    new_y = (a->y < b->y) ? a->y : b->y;
    new_right = (a->x + a->w > b->x + b->w) ? a->x + a->w : b->x + b->w;
    new_bottom = (a->y + a->h > b->y + b->h) ? a->y + a->h : b->y + b->h;
    
    a->x = new_x;
    a->y = new_y;
    a->w = new_right - new_x;
    a->h = new_bottom - new_y;
}

static int MergeDirtyRects(const SDL_Rect *input, int num_input,
                           SDL_Rect *output, int max_output)
{
    int i, j, num_merged, merged_something, iterations;
    const int max_iterations = 5;
    
    if (num_input <= 0) return 0;
    if (num_input == 1) {
        output[0] = input[0];
        return 1;
    }
    
    /* Initialize with memcpy */
    num_merged = (num_input < max_output) ? num_input : max_output;
    SDL_memcpy(output, input, num_merged * sizeof(SDL_Rect));
    
    /* Merge pass */
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
    
    /* Remove invalid rectangles */
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
   Buffer Management Helpers
   ============================================ */

static void CalculateBufferSizes(SDL_VideoData *video, int w, int h, 
                                 int *pitch, size_t *chunky_size, 
                                 size_t *planar_size, SDL_WindowData *data)
{
    const int planes = video->planes;
    int bpp;
    
    data->aligned_w = MFDB_STRIDE(w);
    data->plane_line_bytes = (data->aligned_w >> 4) * 2;
    data->total_line_bytes = data->plane_line_bytes * planes;
    data->block_size = planes * 2;
    
    if (planes <= 8) {
        /* Planar modes */
        bpp = 1;
        *planar_size = ((size_t)data->aligned_w * planes / 8) * h;
    } else {
        /* TrueColor modes */
        bpp = (planes == 16) ? 2 : (planes == 24) ? 3 : 4;
        *planar_size = ((size_t)data->aligned_w * bpp) * h;
    }
    
    *pitch = data->aligned_w * bpp;
    *chunky_size = (size_t)(*pitch) * h;
}

static void FreeWindowBuffers(SDL_WindowData *data)
{
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
#ifdef SDL_GEM_DIRTY_RECT_ASM
    if (data->batch_checksums) {
        SDL_free(data->batch_checksums);
        data->batch_checksums = NULL;
        data->batch_checksums_size = 0;
    }
#endif /* SDL_GEM_DIRTY_RECT_ASM */
#endif /* SDL_GEM_DIRTY_RECT */
}

static int AllocateAlignedBuffer(void **raw, void **aligned, size_t size)
{
    *raw = SDL_malloc(size + 15);
    if (!*raw) {
        *aligned = NULL;
        return 0;
    }
    *aligned = (void*)(((size_t)*raw + 15) & ~(size_t)15);
    return 1;
}

static void InitializeMFDB(MFDB *mfdb, void *buffer, int w, int h, 
                           int aligned_w, int planes)
{
    mfdb->fd_addr = buffer;
    mfdb->fd_w = w;
    mfdb->fd_h = h;
    mfdb->fd_wdwidth = aligned_w >> 4;
    mfdb->fd_stand = 0;
    mfdb->fd_nplanes = planes;
    mfdb->fd_r1 = 0;
    mfdb->fd_r2 = 0;
    mfdb->fd_r3 = 0;
}

/* ============================================
   Framebuffer Management
   ============================================ */

int GEM_CreateWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window, 
                                Uint32 *format, void **pixels, int *pitch)
{
    SDL_VideoData *video = (SDL_VideoData *)this->driverdata;
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    int w, h;
    size_t chunky_size, planar_size;

    Uint32 new_format;
    const int planes = video->planes;
    
    if (!data) {
        return SDL_SetError("Window data not found");
    }
    
    SDL_GetWindowSize(window, &w, &h);
    
    /* Determine format */
    if (planes <= 8) {
        new_format = SDL_PIXELFORMAT_RGB332;
    } else if (planes == 16) {
        new_format = SDL_PIXELFORMAT_RGB565;
    } else if (planes == 24) {
        new_format = SDL_PIXELFORMAT_RGB888;
    } else {
        new_format = SDL_PIXELFORMAT_ARGB8888;
    }
    
    /* Reuse existing buffer if size matches */
    if (data->raw_buffer && data->buffer && 
        w == data->work_w && h == data->work_h && data->buffer_pitch > 0) {
        
        SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                    "GEM: Reusing existing framebuffer %p (%dx%d, pitch=%u)",
                    data->buffer, w, h, data->buffer_pitch);
        
        *format = new_format;
        *pixels = data->buffer;
        *pitch = data->buffer_pitch;
        return 0;
    }
    
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, 
                "GEM: Creating new framebuffer for %dx%d", w, h);
    
    /* Free all existing buffers */
    FreeWindowBuffers(data);
    
    /* Calculate buffer sizes */
    CalculateBufferSizes(video, w, h, pitch, &chunky_size, &planar_size, data);
    
    /* Allocate chunky buffer */
    if (!AllocateAlignedBuffer(&data->raw_buffer, &data->buffer, chunky_size)) {
        return SDL_OutOfMemory();
    }
    
    /* Allocate final buffer */
    if (!AllocateAlignedBuffer(&data->raw_final_buffer, &data->final_buffer, planar_size)) {
        FreeWindowBuffers(data);
        return SDL_OutOfMemory();
    }
    data->final_buffer_size = planar_size;
    
    /* Allocate remap buffer if needed */
    if (planes <= 8 && !video->use_identity_palette) {
        if (!AllocateAlignedBuffer(&data->raw_remap_buffer, &data->remap_buffer, 
                                   data->aligned_w)) {
            FreeWindowBuffers(data);
            return SDL_OutOfMemory();
        }
        data->remap_buffer_size = (size_t)data->aligned_w;
    }
    
    /* Initialize MFDB */
    InitializeMFDB(&data->final_mfdb, data->final_buffer, w, h, 
                   data->aligned_w, planes);
    
    data->buffer_pitch = (unsigned short)(*pitch);
    *format = new_format;
    *pixels = data->buffer;
    
#ifdef SDL_GEM_DIRTY_RECT
    /* Initialize row checksums */
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
   Blit Operations
   ============================================ */
#ifndef SDL_GEM_C2P_ASM
static void ProcessRemapPath(SDL_VideoData *video, SDL_WindowData *data,
                             int src_x, int src_y, int src_w, int src_h,
                             Uint8 *src_base, Uint8 *dst_base, int col_offset)
{
    int y, row, i, aligned_width;
    Uint8 *remap, *lut, *src, *dst;
    const int src_skip = data->buffer_pitch;
    const int dst_skip = data->total_line_bytes;
    
    remap = (Uint8*)data->remap_buffer;
    lut = video->rgb332_to_hw;
    aligned_width = (src_w + 15) & ~15;
    
    for (row = 0; row < src_h; row++) {
        y = src_y + row;
        src = src_base + (y * src_skip) + src_x;
        dst = dst_base + (y * dst_skip) + col_offset;
        
        /* Apply LUT */
        for (i = 0; i < src_w; i++) {
            remap[i] = lut[src[i]];
        }
        
        /* Pad to alignment */
        while (i < aligned_width) {
            remap[i++] = 0;
        }
        
        /* C2P for this row */
        Atari_C2P_Planar(remap, dst, src_w, 1,
                        data->aligned_w, dst_skip, video->planes);
    }
}
#endif /* SDL_GEM_C2P_ASM */

static void BlitRegionOptimized(SDL_VideoData *video, SDL_WindowData *data,
                                int src_x, int src_y, int src_w, int src_h,
                                int dst_screen_x, int dst_screen_y)
{
    short pxy[8];
    const int vh = video->vdi_handle;
    const int planes = video->planes;
    int col_offset, bpp;
    Uint8 *src_base, *dst_base, *src, *dst;
    
    if (planes <= 8) {
        col_offset = (src_x >> 4) * data->block_size;
        src_base = (Uint8*)data->buffer;
        dst_base = (Uint8*)data->final_buffer;
        
        if (video->use_identity_palette) {
            src = src_base + (src_y * data->buffer_pitch) + src_x;
            dst = dst_base + (src_y * data->total_line_bytes) + col_offset;
            
            Atari_C2P_Planar(src, dst, src_w, src_h,
                            data->buffer_pitch, data->total_line_bytes, planes);
        } else {
#ifdef SDL_GEM_C2P_ASM
            src = src_base + (src_y * data->buffer_pitch) + src_x;
            dst = dst_base + (src_y * data->total_line_bytes) + col_offset;
            
            Atari_C2P_Planar_LUT(src, dst, src_w, src_h,
                                data->buffer_pitch, data->total_line_bytes,
                                planes, video->rgb332_to_hw);
#else
            ProcessRemapPath(video, data, src_x, src_y, src_w, src_h,
                           src_base, dst_base, col_offset);
#endif
        }
        
        pxy[0] = (short)src_x;
        pxy[1] = (short)src_y;
        pxy[2] = (short)(src_x + src_w - 1);
        pxy[3] = (short)(src_y + src_h - 1);
    } else {
        /* TrueColor */
        bpp = planes >> 3;
        
        src = (Uint8*)data->buffer + 
              (src_y * data->buffer_pitch) + (src_x * bpp);
        dst = (Uint8*)data->final_buffer +
              (src_y * data->buffer_pitch) + (src_x * bpp);
        
        BlitFast(dst, src, src_w * bpp, src_h, data->buffer_pitch);
        
        pxy[0] = (short)src_x;
        pxy[1] = (short)src_y;
        pxy[2] = (short)(src_x + src_w - 1);
        pxy[3] = (short)(src_y + src_h - 1);
    }
    
    /* Destination screen coordinates */
    pxy[4] = (short)dst_screen_x;
    pxy[5] = (short)dst_screen_y;
    pxy[6] = (short)(dst_screen_x + src_w - 1);
    pxy[7] = (short)(dst_screen_y + src_h - 1);
    
    vro_cpyfm(vh, S_ONLY, pxy, &data->final_mfdb, &screen_mfdb);
}

static void ProcessDirtyRectsInRegion(SDL_VideoData *video, SDL_WindowData *data,
                                     const SDL_Rect *merged, int num_merged,
                                     const GRECT *work, const GRECT *gem_rect)
{
    int i, src_x, src_y, src_w, src_h, dst_x, dst_y;
    int gem_left, gem_top, gem_right, gem_bottom;
    int m_left, m_top, m_right, m_bottom;
    int i_left, i_top, i_right, i_bottom;
    int align_x, align_offset;
    SDL_Rect r;
    
    /* Cache GEM region bounds */
    gem_left = gem_rect->g_x;
    gem_top = gem_rect->g_y;
    gem_right = gem_rect->g_x + gem_rect->g_w;
    gem_bottom = gem_rect->g_y + gem_rect->g_h;
    
    for (i = 0; i < num_merged; i++) {
        /* Validate */
        if (merged[i].x >= data->work_w || merged[i].y >= data->work_h) {
            continue;
        }
        
        /* Clip to window bounds */
        r = merged[i];
        if (r.x + r.w > data->work_w) r.w = data->work_w - r.x;
        if (r.y + r.h > data->work_h) r.h = data->work_h - r.y;
        if (r.w <= 0 || r.h <= 0) continue;

        /* Convert to screen coordinates */
        m_left = work->g_x + r.x;
        m_top = work->g_y + r.y;
        m_right = m_left + r.w;
        m_bottom = m_top + r.h;

        /* Clip to GEM region */
        i_left = (m_left > gem_left) ? m_left : gem_left;
        i_top = (m_top > gem_top) ? m_top : gem_top;
        i_right = (m_right < gem_right) ? m_right : gem_right;
        i_bottom = (m_bottom < gem_bottom) ? m_bottom : gem_bottom;

        src_w = i_right - i_left;
        src_h = i_bottom - i_top;
        if (src_w <= 0 || src_h <= 0) continue;

        /* Calculate source coordinates */
        src_x = i_left - work->g_x;
        src_y = i_top - work->g_y;

        /* Bounds check */
        if (src_x < 0) { src_w += src_x; src_x = 0; }
        if (src_y < 0) { src_h += src_y; src_y = 0; }
        if (src_x + src_w > data->work_w) src_w = data->work_w - src_x;
        if (src_y + src_h > data->work_h) src_h = data->work_h - src_y;
        if (src_w <= 0 || src_h <= 0) continue;

        dst_x = i_left;
        dst_y = i_top;

        /* 16-pixel alignment for planar */
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

        BlitRegionOptimized(video, data, src_x, src_y, src_w, src_h, dst_x, dst_y);
    }
}

int GEM_UpdateWindowFramebuffer(SDL_VideoDevice *this, SDL_Window *window,
                                const SDL_Rect *rects, int numrects)
{
    SDL_VideoData *video = (SDL_VideoData *)this->driverdata;
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    SDL_Rect merged[MAX_MERGED_RECTS];
    int num_merged;
    GRECT work, gem_rect;
    short todo[4], clip_pxy[4];
    const int vh = video->vdi_handle;
    SDL_Rect full_rect;
    
#ifdef SDL_GEM_DIRTY_RECT
    SDL_Rect optimized_rect, combined_rects[MAX_MERGED_RECTS];
    const SDL_Rect *rects_to_process;
    int num_to_process, num_combined;
    int change_detect_result, use_full_update = 0;
#endif

    /* Handle NULL/empty rects */
    full_rect.x = 0;
    full_rect.y = 0;
    full_rect.w = data->work_w;
    full_rect.h = data->work_h;
    
    if (!rects || numrects <= 0) {
        rects = &full_rect;
        numrects = 1;
    }

    if (!data || !data->buffer || !data->final_buffer) {
        return SDL_SetError("Framebuffer not initialised");
    }
    /* Pitch not initialised — GEM_CreateWindowFramebuffer was not called yet */
    if (data->buffer_pitch == 0) {
        int pitch;
        size_t chunky_size, planar_size;
        CalculateBufferSizes(video, data->work_w, data->work_h,
                             &pitch, &chunky_size, &planar_size, data);
        data->buffer_pitch = (unsigned short)pitch;
    }
#ifdef SDL_GEM_DIRTY_RECT
    /* Detect changes */
    change_detect_result = DetectChangesAndBuildRect(data, rects, numrects,
                                                      &optimized_rect);
    
    if (change_detect_result == -1) {
        /* No new changes */
        if (video->planes <= 8 && data->num_prev_dirty_rects > 0) {
            rects_to_process = data->prev_dirty_rects;
            num_to_process = data->num_prev_dirty_rects;
        } else if (video->planes > 8) {
            rects_to_process = rects;
            num_to_process = numrects;
        } else {
            return 0;
        }
    }
    else if (change_detect_result == 1) {
        rects_to_process = &optimized_rect;
        num_to_process = 1;
    }
    else if (change_detect_result == 2) {
        rects_to_process = rects;
        num_to_process = numrects;
        use_full_update = 0;
    }    
    else {
        rects_to_process = rects;
        num_to_process = numrects;
        use_full_update = (change_detect_result == 0);
    }

    if (num_to_process <= 0) {
        return 0;
    }

    /* Merge with previous rects */
    num_combined = UnionRectLists(data->prev_dirty_rects, data->num_prev_dirty_rects,
                                  rects_to_process, num_to_process,
                                  combined_rects, MAX_MERGED_RECTS);

    if (num_combined == -1) {
        use_full_update = 1;
    }

    /* Perform update */
    if (use_full_update) {
        num_merged = 1;
        merged[0] = full_rect;
        data->num_prev_dirty_rects = 0;
    }
    else {
        num_merged = num_combined;
        SDL_memcpy(merged, combined_rects, num_merged * sizeof(SDL_Rect));
    }

#else
    /* No optimization */
    num_merged = MergeDirtyRects(rects, numrects, merged, MAX_MERGED_RECTS);
    if (num_merged <= 0) {
        return 0;
    }
#endif

    /* VDI blitting */
    mt_wind_get_grect(data->handle, WF_WORKXYWH, &work, sdl_global_aes);
    mt_graf_mouse(M_OFF, 0L, sdl_global_aes);
    mt_wind_update(BEG_UPDATE, sdl_global_aes);

    /* Walk GEM visible regions */
    mt_wind_get(data->handle, WF_FIRSTXYWH,
                &todo[0], &todo[1], &todo[2], &todo[3], sdl_global_aes);

    while (todo[2] && todo[3]) {
        gem_rect.g_x = todo[0];
        gem_rect.g_y = todo[1];
        gem_rect.g_w = todo[2];
        gem_rect.g_h = todo[3];

        if (rc_intersect(&work, &gem_rect)) {
            /* Set VDI clip */
            clip_pxy[0] = (short)gem_rect.g_x;
            clip_pxy[1] = (short)gem_rect.g_y;
            clip_pxy[2] = (short)(gem_rect.g_x + gem_rect.g_w - 1);
            clip_pxy[3] = (short)(gem_rect.g_y + gem_rect.g_h - 1);
            vs_clip(vh, 1, clip_pxy);

            /* Process dirty rects */
            ProcessDirtyRectsInRegion(video, data, merged, num_merged, 
                                     &work, &gem_rect);

            /* Disable VDI clip */
            vs_clip(vh, 0, clip_pxy);
        }

        mt_wind_get(data->handle, WF_NEXTXYWH,
                    &todo[0], &todo[1], &todo[2], &todo[3], sdl_global_aes);
    }

    mt_wind_update(END_UPDATE, sdl_global_aes);
    mt_graf_mouse(M_ON, 0L, sdl_global_aes);

#ifdef SDL_GEM_DIRTY_RECT
    /* Update checksums after blit */
    if (use_full_update) {
        UpdateRowChecksumsAfterBlit(data, &full_rect, 1);
    }
    else {
        UpdateRowChecksumsAfterBlit(data, merged, num_merged);
        
        /* Store merged rects */
        if (num_merged <= MAX_MERGED_RECTS) {
            data->num_prev_dirty_rects = num_merged;
            SDL_memcpy(data->prev_dirty_rects, merged, 
                      num_merged * sizeof(SDL_Rect));
        } else {
            data->num_prev_dirty_rects = 1;
            data->prev_dirty_rects[0] = full_rect;
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

    FreeWindowBuffers(data);
}

/* ============================================
   Window Management
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
    SDL_WindowData *data;
    int w, h, x, y;
    short win_type;
    (void)this;
    
    data = (SDL_WindowData *)SDL_calloc(1, sizeof(SDL_WindowData));
    if (!data) {
        return SDL_OutOfMemory();
    }
    
    win_type = NAME | CLOSER | MOVER;
    if (window->flags & SDL_WINDOW_RESIZABLE) {
        win_type |= SIZER | FULLER;
    }
    
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    
    if (x == SDL_WINDOWPOS_UNDEFINED) x = 50;
    if (y == SDL_WINDOWPOS_UNDEFINED) y = 50;
    
    data->win_type = win_type;
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

    SDL_SetKeyboardFocus(window);

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
    return;
}

void GEM_SetWindowResizable(SDL_VideoDevice *this, SDL_Window *window, SDL_bool resizable)
{
    return;
}

void GEM_SetWindowMinimumSize(SDL_VideoDevice *this, SDL_Window *window)
{
    return;
}

void GEM_SetWindowMaximumSize(SDL_VideoDevice *this, SDL_Window *window)
{
    return;
}

int GEM_GetWindowDisplayIndex(SDL_VideoDevice *this, SDL_Window *window)
{
    return 0;
}

#endif /* SDL_VIDEO_DRIVER_GEM */
