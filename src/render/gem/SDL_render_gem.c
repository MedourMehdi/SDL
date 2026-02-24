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
   SDL_render_gem.c – GEM software renderer for SDL2
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Implements the SDL2 render driver interface on top of the GEM framebuffer.
   Textures are stored in the window's native pixel format at creation time
   (RGB332 is pre-converted via LUT at upload) so GEM_OptimizedTextureCopy
   always takes the fast memcpy path with no per-frame conversion cost.

   Dirty rectangle strategy:
     <= 4 rects  → direct partial VDI update
      > 4 rects  → delegate to SDL_gemwindow checksum-based full scan

   Supports: points, lines, filled rects, texture copy, partial lock/unlock.
   Pixel formats: RGB332, RGB565, RGB888, ARGB8888, BGRA8888.

   C90 compliant.
   ============================================================================ */

#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_RENDER_GEM

#include "../SDL_sysrender.h"
#include "../../video/gem/SDL_gemvideo.h"
#include "SDL_hints.h"
#include <mint/osbind.h>

/* Configuration */
#define MAX_DIRTY_RECTS 32
#define MERGE_THRESHOLD 16
#define DIRECT_UPDATE_THRESHOLD 4  /* Use direct update for <=4 rects */
#define MAX_MERGE_ITERATIONS 5     /* Prevent O(n^2) worst case */

/* ============================================================================
 * Private Data Structures
 * ============================================================================ */

typedef struct GEM_RenderData {
    SDL_Surface *window_surface;
    SDL_Window *window;
    SDL_bool surface_dirty;
    SDL_bool vsync_enabled;
    int window_w;
    int window_h;
    SDL_bool surface_acquired;

    /* Dirty rectangle tracking */
    SDL_Rect dirty_rects[MAX_DIRTY_RECTS];
    int num_dirty_rects;
    SDL_bool force_full_update;

} GEM_RenderData;

typedef struct GEM_TextureData {
    SDL_Surface *surface;
    Uint32 src_format;   /* Original format requested by app */
    void *lock_buffer;   /* Temporary RGB332 buffer for LockTexture/UnlockTexture */
    SDL_Rect lock_rect;  /* Which region is currently locked (valid only if lock_buffer != NULL) */
} GEM_TextureData;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static SDL_bool GEM_AcquireWindowSurface(GEM_RenderData *data);
static void GEM_AddDirtyRect(GEM_RenderData *data, const SDL_Rect *rect);
static void GEM_MergeDirtyRects(GEM_RenderData *data);
static void GEM_DrawLine(SDL_Surface *surface, int x0, int y0, int x1, int y1, Uint32 color);
static void GEM_OptimizedTextureCopy(SDL_Surface *texture_surface,
                                     const SDL_Rect *srcrect,
                                     SDL_Surface *dest_surface,
                                     SDL_Rect *dstrect);

static void GEM_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event);
static int GEM_GetOutputSize(SDL_Renderer *renderer, int *w, int *h);
static int GEM_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture);
static int GEM_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *rect, const void *pixels, int pitch);
static int GEM_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                            const SDL_Rect *rect, void **pixels, int *pitch);
static void GEM_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture);
static void GEM_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture,
                                     SDL_ScaleMode scaleMode);
static int GEM_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture);
static int GEM_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd);
static int GEM_QueueSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd);
static int GEM_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                const SDL_FPoint *points, int count);
static int GEM_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                               const SDL_FPoint *points, int count);
static int GEM_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                               const SDL_FRect *rects, int count);
static int GEM_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                         SDL_Texture *texture, const SDL_Rect *srcrect,
                         const SDL_FRect *dstrect);
static int GEM_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                            SDL_Texture *texture, const SDL_Rect *srcrect,
                            const SDL_FRect *dstrect, const double angle,
                            const SDL_FPoint *center, const SDL_RendererFlip flip,
                            float scale_x, float scale_y);
static int GEM_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                              SDL_Texture *texture,
                              const float *xy, int xy_stride,
                              const SDL_Color *color, int color_stride,
                              const float *uv, int uv_stride,
                              int num_vertices, const void *indices,
                              int num_indices, int size_indices,
                              float scale_x, float scale_y);
static int GEM_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                void *vertices, size_t vertsize);
static int GEM_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect,
                                 Uint32 format, void *pixels, int pitch);
static int GEM_RenderPresent(SDL_Renderer *renderer);
static void GEM_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture);
static void GEM_DestroyRenderer(SDL_Renderer *renderer);
static int GEM_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, Uint32 flags);

/* ============================================================================
 * Format-Matched Texture Copy
 * Uses memcpy when formats match (fast path), SDL_BlitScaled otherwise (slow).
 * With native-format texture storage, the fast path is always taken for
 * properly created textures.
 * ============================================================================ */

static void GEM_OptimizedTextureCopy(SDL_Surface *texture_surface,
                                     const SDL_Rect *srcrect,
                                     SDL_Surface *dest_surface,
                                     SDL_Rect *dstrect)
{
    if (texture_surface->format->format == dest_surface->format->format &&
        srcrect->w == dstrect->w &&
        srcrect->h == dstrect->h) {
        if (texture_surface->format->Amask) {
            /* Has alpha — use SDL_BlitSurface for correct blending */
            SDL_BlitSurface(texture_surface, (SDL_Rect*)srcrect,
                            dest_surface, dstrect);
        } else {
            /* Fast path: direct memcpy row by row */
            int y;
            const int bpp = texture_surface->format->BytesPerPixel;
            const int row_bytes = srcrect->w * bpp;
            const int src_pitch = texture_surface->pitch;
            const int dst_pitch = dest_surface->pitch;
            Uint8 *src;
            Uint8 *dst;
            int lock_mask = 0; /* bit 0 = src locked, bit 1 = dst locked */

            if (SDL_MUSTLOCK(texture_surface)) {
                SDL_LockSurface(texture_surface);
                lock_mask |= 1;
            }
            if (SDL_MUSTLOCK(dest_surface)) {
                SDL_LockSurface(dest_surface);
                lock_mask |= 2;
            }

            src = (Uint8 *)texture_surface->pixels +
                (srcrect->y * src_pitch) + (srcrect->x * bpp);
            dst = (Uint8 *)dest_surface->pixels +
                (dstrect->y * dst_pitch) + (dstrect->x * bpp);

            for (y = 0; y < srcrect->h; y++) {
                SDL_memcpy(dst, src, row_bytes);
                src += src_pitch;
                dst += dst_pitch;
            }

            if (lock_mask & 2) SDL_UnlockSurface(dest_surface);
            if (lock_mask & 1) SDL_UnlockSurface(texture_surface);
        }
    } else {
        /* Slow path: format conversion or scaling needed.
         * This should only happen for textures created before the window
         * surface was available (early-init edge case). */
        SDL_BlitScaled(texture_surface, srcrect, dest_surface, dstrect);
    }
}

/* ============================================================================
 * Dirty Rectangle Tracking
 * ============================================================================ */

static void GEM_AddDirtyRect(GEM_RenderData *data, const SDL_Rect *rect)
{
    SDL_Rect clipped;
    SDL_Rect *last;
    int new_right, new_bottom, last_right, last_bottom;
    int min_x, min_y, max_x, max_y;

    /* Clip to window bounds */
    clipped.x = (rect->x < 0) ? 0 : rect->x;
    clipped.y = (rect->y < 0) ? 0 : rect->y;
    clipped.w = rect->w + ((rect->x < 0) ? rect->x : 0);
    clipped.h = rect->h + ((rect->y < 0) ? rect->y : 0);

    new_right  = clipped.x + clipped.w;
    new_bottom = clipped.y + clipped.h;

    if (new_right  > data->window_w) clipped.w = data->window_w - clipped.x;
    if (new_bottom > data->window_h) clipped.h = data->window_h - clipped.y;

    if (clipped.w <= 0 || clipped.h <= 0) {
        return;
    }

    /* Try to merge with last rect */
    if (data->num_dirty_rects > 0) {
        last = &data->dirty_rects[data->num_dirty_rects - 1];
        last_right  = last->x + last->w;
        last_bottom = last->y + last->h;

        if (clipped.x <= last_right  + MERGE_THRESHOLD &&
            new_right  + MERGE_THRESHOLD >= last->x &&
            clipped.y <= last_bottom + MERGE_THRESHOLD &&
            new_bottom + MERGE_THRESHOLD >= last->y) {

            min_x = (last->x < clipped.x) ? last->x : clipped.x;
            min_y = (last->y < clipped.y) ? last->y : clipped.y;
            max_x = (last_right  > new_right)  ? last_right  : new_right;
            max_y = (last_bottom > new_bottom) ? last_bottom : new_bottom;

            last->x = min_x;
            last->y = min_y;
            last->w = max_x - min_x;
            last->h = max_y - min_y;
            return;
        }
    }

    if (data->num_dirty_rects < MAX_DIRTY_RECTS) {
        data->dirty_rects[data->num_dirty_rects++] = clipped;
    } else {
        data->force_full_update = SDL_TRUE;
    }
}

static void GEM_MergeDirtyRects(GEM_RenderData *data)
{
    int i, j;
    SDL_bool merged;
    int iterations = 0;

    // if (data->num_dirty_rects <= 1) return; -- No need to check, the loop will handle it and exit immediately if 0 or 1 rects

    do {
        merged = SDL_FALSE;
        iterations++;

        for (i = 0; i < data->num_dirty_rects; i++) {
            for (j = i + 1; j < data->num_dirty_rects; ) {
                SDL_Rect *a = &data->dirty_rects[i];
                SDL_Rect *b = &data->dirty_rects[j];
                int a_right  = a->x + a->w;
                int a_bottom = a->y + a->h;
                int b_right  = b->x + b->w;
                int b_bottom = b->y + b->h;
                int min_x, min_y, max_x, max_y;

                if (a->x <= b_right  + MERGE_THRESHOLD &&
                    a_right  + MERGE_THRESHOLD >= b->x &&
                    a->y <= b_bottom + MERGE_THRESHOLD &&
                    a_bottom + MERGE_THRESHOLD >= b->y) {

                    min_x = (a->x < b->x) ? a->x : b->x;
                    min_y = (a->y < b->y) ? a->y : b->y;
                    max_x = (a_right  > b_right)  ? a_right  : b_right;
                    max_y = (a_bottom > b_bottom) ? a_bottom : b_bottom;

                    a->x = min_x;
                    a->y = min_y;
                    a->w = max_x - min_x;
                    a->h = max_y - min_y;

                    /* Remove b by replacing with last */
                    *b = data->dirty_rects[data->num_dirty_rects - 1];
                    data->num_dirty_rects--;
                    merged = SDL_TRUE;
                } else {
                    j++;
                }
            }
        }
    } while (merged && data->num_dirty_rects > 1 && iterations < MAX_MERGE_ITERATIONS);
}

/* ============================================================================
 * Surface Acquisition
 * ============================================================================ */

static SDL_bool GEM_AcquireWindowSurface(GEM_RenderData *data)
{
    SDL_Surface *surface;
    int w, h;

    /* Always re-fetch: SDL may recreate the surface internally */
    SDL_GetWindowSize(data->window, &w, &h);
    data->window_w = w;
    data->window_h = h;

    surface = SDL_GetWindowSurface(data->window);
    if (!surface || !surface->pixels) {
        data->surface_acquired = SDL_FALSE;
        data->window_surface = NULL;
        return SDL_FALSE;
    }

    data->window_surface = surface;
    data->surface_acquired = SDL_TRUE;
    return SDL_TRUE;
}

/* ============================================================================
 * RenderPresent
 * ============================================================================ */

static int GEM_RenderPresent(SDL_Renderer *renderer)
{
    GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;
    int result;

    if (!data || !data->surface_dirty) {
        return 0;
    }

    if (!GEM_AcquireWindowSurface(data)) {
        return SDL_SetError("GEM: Could not acquire window surface");
    }

    if (data->vsync_enabled) {
        Vsync();
    }

    if (data->num_dirty_rects > 0 &&
        data->num_dirty_rects <= DIRECT_UPDATE_THRESHOLD &&
        !data->force_full_update) {
        result = SDL_UpdateWindowSurfaceRects(data->window,
                                              data->dirty_rects,
                                              data->num_dirty_rects);
    } else {
        result = SDL_UpdateWindowSurface(data->window);
    }

    data->surface_dirty   = SDL_FALSE;
    data->num_dirty_rects = 0;
    data->force_full_update = SDL_FALSE;

    return result;
}

/* ============================================================================
 * Bresenham Line Drawing
 * ============================================================================ */

static void GEM_DrawLine(SDL_Surface *surface, int x0, int y0, int x1, int y1,
                         Uint32 color)
{
    int dx, dy, sx, sy, err, e2;
    const int pitch = surface->pitch;
    const int bpp   = surface->format->BytesPerPixel;
    const int w     = surface->w;
    const int h     = surface->h;
    Uint8 *pixels   = (Uint8 *)surface->pixels;

    if (!surface || !pixels) return;

    dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    while (1) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
            Uint8 *pixel = pixels + y0 * pitch + x0 * bpp;
            switch (bpp) {
                case 1: *pixel = (Uint8)color; break;
                case 2: *(Uint16 *)pixel = (Uint16)color; break;
                case 3:
                    pixel[0] = (Uint8)(color >> 16);
                    pixel[1] = (Uint8)(color >> 8);
                    pixel[2] = (Uint8)color;
                    break;
                case 4: *(Uint32 *)pixel = color; break;
            }
        }

        if (x0 == x1 && y0 == y1) break;

        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* ============================================================================
 * RunCommandQueue - Process all rendering commands
 * ============================================================================ */

static int GEM_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                void *vertices, size_t vertsize)
{
    GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;
    SDL_Surface *surface;

    if (!data) {
        return SDL_SetError("GEM: Renderer data is NULL");
    }

    if (!GEM_AcquireWindowSurface(data)) {
        return SDL_SetError("GEM: Could not acquire window surface");
    }

    surface = data->window_surface;
    if (!surface) {
        return SDL_SetError("GEM: Window surface is NULL");
    }

    /* Reset dirty tracking for this frame */
    data->num_dirty_rects   = 0;
    data->force_full_update = SDL_FALSE;

    while (cmd) {
        switch (cmd->command) {
            case SDL_RENDERCMD_SETVIEWPORT:
            case SDL_RENDERCMD_SETDRAWCOLOR:
                break;

            case SDL_RENDERCMD_SETCLIPRECT:
                if (cmd->data.cliprect.enabled) {
                    SDL_SetClipRect(surface, &cmd->data.cliprect.rect);
                } else {
                    SDL_SetClipRect(surface, NULL);
                }
                break;

            case SDL_RENDERCMD_CLEAR: {
                Uint32 color = SDL_MapRGBA(surface->format,
                                           cmd->data.color.r,
                                           cmd->data.color.g,
                                           cmd->data.color.b,
                                           cmd->data.color.a);
                SDL_FillRect(surface, NULL, color);
                data->force_full_update = SDL_TRUE;
                data->surface_dirty = SDL_TRUE;
                break;
            }

            case SDL_RENDERCMD_DRAW_POINTS: {
                const SDL_FPoint *points;
                int count, i;
                Uint32 color;
                int min_x, min_y, max_x, max_y;

                if (!vertices || cmd->data.draw.first >= vertsize) break;

                points = (const SDL_FPoint *)((const Uint8 *)vertices + cmd->data.draw.first);
                count  = (int)cmd->data.draw.count;

                if (count <= 0) break;

                color = SDL_MapRGBA(surface->format,
                                    cmd->data.draw.r, cmd->data.draw.g,
                                    cmd->data.draw.b, cmd->data.draw.a);

                min_x = max_x = (int)points[0].x;
                min_y = max_y = (int)points[0].y;

                for (i = 0; i < count; i++) {
                    SDL_Rect pixel;
                    int px = (int)points[i].x;
                    int py = (int)points[i].y;

                    pixel.x = px; pixel.y = py;
                    pixel.w = 1;  pixel.h = 1;
                    SDL_FillRect(surface, &pixel, color);

                    if (px < min_x) min_x = px;
                    if (px > max_x) max_x = px;
                    if (py < min_y) min_y = py;
                    if (py > max_y) max_y = py;
                }

                {
                    SDL_Rect dirty;
                    dirty.x = min_x; dirty.y = min_y;
                    dirty.w = max_x - min_x + 1;
                    dirty.h = max_y - min_y + 1;
                    GEM_AddDirtyRect(data, &dirty);
                }

                data->surface_dirty = SDL_TRUE;
                break;
            }

            case SDL_RENDERCMD_DRAW_LINES: {
                const SDL_FPoint *points;
                int count, i;
                Uint32 color;
                int min_x, min_y, max_x, max_y;

                if (!vertices || cmd->data.draw.first >= vertsize) break;

                points = (const SDL_FPoint *)((const Uint8 *)vertices + cmd->data.draw.first);
                count  = (int)cmd->data.draw.count;

                if (count <= 0) break;

                color = SDL_MapRGBA(surface->format,
                                    cmd->data.draw.r, cmd->data.draw.g,
                                    cmd->data.draw.b, cmd->data.draw.a);

                if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);

                min_x = max_x = (int)points[0].x;
                min_y = max_y = (int)points[0].y;

                for (i = 0; i < count - 1; i++) {
                    int x0 = (int)points[i].x;
                    int y0 = (int)points[i].y;
                    int x1 = (int)points[i + 1].x;
                    int y1 = (int)points[i + 1].y;
                    int lmx = (x0 < x1) ? x0 : x1;
                    int lmy = (y0 < y1) ? y0 : y1;
                    int lMx = (x0 > x1) ? x0 : x1;
                    int lMy = (y0 > y1) ? y0 : y1;

                    GEM_DrawLine(surface, x0, y0, x1, y1, color);

                    if (lmx < min_x) min_x = lmx;
                    if (lmy < min_y) min_y = lmy;
                    if (lMx > max_x) max_x = lMx;
                    if (lMy > max_y) max_y = lMy;
                }

                if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);

                {
                    SDL_Rect dirty;
                    dirty.x = min_x; dirty.y = min_y;
                    dirty.w = max_x - min_x + 1;
                    dirty.h = max_y - min_y + 1;
                    GEM_AddDirtyRect(data, &dirty);
                }

                data->surface_dirty = SDL_TRUE;
                break;
            }

            case SDL_RENDERCMD_FILL_RECTS: {
                const SDL_FRect *rects;
                int count, i;
                Uint32 color;

                if (!vertices || cmd->data.draw.first >= vertsize) break;

                rects = (const SDL_FRect *)((const Uint8 *)vertices + cmd->data.draw.first);
                count = (int)cmd->data.draw.count;
                color = SDL_MapRGBA(surface->format,
                                    cmd->data.draw.r, cmd->data.draw.g,
                                    cmd->data.draw.b, cmd->data.draw.a);

                for (i = 0; i < count; i++) {
                    SDL_Rect r;
                    r.x = (int)rects[i].x; r.y = (int)rects[i].y;
                    r.w = (int)rects[i].w; r.h = (int)rects[i].h;
                    SDL_FillRect(surface, &r, color);
                    GEM_AddDirtyRect(data, &r);
                }

                data->surface_dirty = SDL_TRUE;
                break;
            }

            case SDL_RENDERCMD_COPY: {
                GEM_TextureData *texdata;
                SDL_Rect verts[2]; /* srcrect and dstrect */

                if (!cmd->data.draw.texture) break;

                texdata = (GEM_TextureData *)cmd->data.draw.texture->driverdata;
                if (!texdata || !texdata->surface) break;

                if (!vertices || cmd->data.draw.first >= vertsize) break;

                SDL_memcpy(verts, (const Uint8 *)vertices + cmd->data.draw.first,
                           2 * sizeof(SDL_Rect));

                GEM_OptimizedTextureCopy(texdata->surface, &verts[0],
                                        surface, &verts[1]);

                GEM_AddDirtyRect(data, &verts[1]);
                data->surface_dirty = SDL_TRUE;
                break;
            }

            case SDL_RENDERCMD_GEOMETRY:
            case SDL_RENDERCMD_NO_OP:
            default:
                break;
        }

        cmd = cmd->next;
    }

    if (data->num_dirty_rects > 1) {
        GEM_MergeDirtyRects(data);
    }

    return 0;
}

/* ============================================================================
 * Renderer Creation
 * ============================================================================ */

static int GEM_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, Uint32 flags)
{
    GEM_RenderData *data;
    int w, h;

    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) {
        return SDL_SetError("GEM: Invalid window dimensions");
    }

    data = (GEM_RenderData *)SDL_calloc(1, sizeof(GEM_RenderData));
    if (!data) {
        return SDL_OutOfMemory();
    }

    /* SDL_calloc zeroes memory - only set non-zero fields */
    data->window       = window;
    data->window_w     = w;
    data->window_h     = h;
    data->vsync_enabled = (flags & SDL_RENDERER_PRESENTVSYNC) ? SDL_TRUE : SDL_FALSE;

    renderer->WindowEvent       = GEM_WindowEvent;
    renderer->GetOutputSize     = GEM_GetOutputSize;
    renderer->CreateTexture     = GEM_CreateTexture;
    renderer->UpdateTexture     = GEM_UpdateTexture;
    renderer->LockTexture       = GEM_LockTexture;
    renderer->UnlockTexture     = GEM_UnlockTexture;
    renderer->SetTextureScaleMode = GEM_SetTextureScaleMode;
    renderer->SetRenderTarget   = GEM_SetRenderTarget;
    renderer->QueueSetViewport  = GEM_QueueSetViewport;
    renderer->QueueSetDrawColor = GEM_QueueSetDrawColor;
    renderer->QueueDrawPoints   = GEM_QueueDrawPoints;
    renderer->QueueDrawLines    = GEM_QueueDrawLines;
    renderer->QueueFillRects    = GEM_QueueFillRects;
    renderer->QueueCopy         = GEM_QueueCopy;
    renderer->QueueCopyEx       = GEM_QueueCopyEx;
    renderer->QueueGeometry     = GEM_QueueGeometry;
    renderer->RunCommandQueue   = GEM_RunCommandQueue;
    renderer->RenderReadPixels  = GEM_RenderReadPixels;
    renderer->RenderPresent     = GEM_RenderPresent;
    renderer->DestroyTexture    = GEM_DestroyTexture;
    renderer->DestroyRenderer   = GEM_DestroyRenderer;

    renderer->info.name = "gem";
    renderer->info.flags = SDL_RENDERER_SOFTWARE;
    if (data->vsync_enabled) {
        renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    renderer->info.num_texture_formats    = 5;
    renderer->info.texture_formats[0]     = SDL_PIXELFORMAT_RGB332;
    renderer->info.texture_formats[1]     = SDL_PIXELFORMAT_RGB565;
    renderer->info.texture_formats[2]     = SDL_PIXELFORMAT_RGB888;
    renderer->info.texture_formats[3]     = SDL_PIXELFORMAT_ARGB8888;
    renderer->info.texture_formats[4]     = SDL_PIXELFORMAT_BGRA8888;
    renderer->info.max_texture_width      = 4096;
    renderer->info.max_texture_height     = 4096;

    renderer->driverdata = data;

    /* Best-effort: warm surface cache so CreateTexture can detect native format.
     * May fail if called before the window framebuffer is fully initialized.
     * Textures created before the first successful acquire fall back gracefully
     * to same-format storage (memcpy path, no LUT conversion). */
    // GEM_AcquireWindowSurface(data);

    return 0;
}

/* ============================================================================
 * Texture Management
 * ============================================================================ */

static int GEM_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GEM_TextureData *data;
    GEM_RenderData *renderdata = (GEM_RenderData *)renderer->driverdata;
    Uint32 surface_format;
    SDL_Surface *win_surf;

    /* Default: store in the format the app requested */
    surface_format = texture->format;

    /* If the window surface is available and uses a different (TrueColor) format,
     * store the texture in that native format so GEM_OptimizedTextureCopy always
     * hits the fast memcpy path instead of SDL_BlitScaled. */
    if (GEM_AcquireWindowSurface(renderdata)) {
        win_surf = renderdata->window_surface;
        // if (win_surf &&
        //     texture->format == SDL_PIXELFORMAT_RGB332 &&
        //     win_surf->format->format != SDL_PIXELFORMAT_RGB332) {
        if (win_surf && texture->format != win_surf->format->format) {        
            surface_format = win_surf->format->format;
        }
    }

    data = (GEM_TextureData *)SDL_calloc(1, sizeof(GEM_TextureData));
    if (!data) {
        return SDL_OutOfMemory();
    }

    data->surface = SDL_CreateRGBSurfaceWithFormat(0, texture->w, texture->h,
                                                   0, surface_format);
    if (!data->surface) {
        SDL_free(data);
        return SDL_SetError("GEM: Could not create texture surface");
    }

    data->src_format = texture->format;

    /* Allocate a lock buffer only when conversion is needed.
     * Sized for the full texture so any partial lock fits without realloc. */
    if (data->src_format != surface_format) {
        data->lock_buffer = SDL_malloc((size_t)texture->w * texture->h
                            * SDL_BYTESPERPIXEL(data->src_format));
        if (!data->lock_buffer) {
            SDL_FreeSurface(data->surface);
            SDL_free(data);
            return SDL_OutOfMemory();
        }
    }
    /* lock_rect is left zeroed; it is only valid while lock_buffer != NULL
     * and a lock is active. */

    texture->driverdata = data;
    return 0;
}

/* ============================================================================
 * GEM_UpdateTexture
 *
 * If the app supplies RGB332 pixels but the internal surface is TrueColor,
 * use LUT converters to pre-convert at upload time (fast path).
 * Otherwise fall through to a plain row-by-row memcpy.
 *
 * Both paths lock the surface if SDL_MUSTLOCK requires it.
 * C90: all declarations are at the top of their enclosing block.
 * ============================================================================ */

static int GEM_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *rect, const void *pixels, int pitch)
{
    GEM_TextureData *data = (GEM_TextureData *)texture->driverdata;
    SDL_Surface *surface  = data->surface;

    const Uint8 *src;
    Uint8 *dst_base;
    int converted = 0;

    if (SDL_MUSTLOCK(surface)) {
        if (SDL_LockSurface(surface) < 0) {
            return -1;
        }
    }

    /* dst_base must be derived after LockSurface: surface->pixels is only
     * guaranteed valid once the surface is locked. */
    src      = (const Uint8 *)pixels;
    dst_base = (Uint8 *)surface->pixels +
               (rect->y * surface->pitch) +
               (rect->x * surface->format->BytesPerPixel);

    /* Fast path: BGRA8888 -> any supported native format */
    if (data->src_format == SDL_PIXELFORMAT_BGRA8888) {
        switch (surface->format->format) {
            case SDL_PIXELFORMAT_RGB332:
                Atari_ConvertBGRA8888toRGB332(src, dst_base, rect->w, rect->h,
                                              pitch, surface->pitch);
                converted = 1;
                break;
            case SDL_PIXELFORMAT_RGB565:
            case SDL_PIXELFORMAT_RGB888:
            case SDL_PIXELFORMAT_ARGB8888:
                /* Falcon TrueColor: SDL handles BGRA8888->any correctly */
                SDL_ConvertPixels(rect->w, rect->h,
                                  SDL_PIXELFORMAT_BGRA8888, src, pitch,
                                  surface->format->format, dst_base, surface->pitch);
                converted = 1;
                break;
            default:
                break; /* fall through to memcpy */
        }
    }
    /* Fast path: RGB332 -> TrueColor using precomputed LUTs */
    else if (data->src_format == SDL_PIXELFORMAT_RGB332 &&
             surface->format->format != SDL_PIXELFORMAT_RGB332) {
        
        switch (surface->format->format) {
            case SDL_PIXELFORMAT_RGB565:
                Atari_ConvertRGB332toRGB565(src, (Uint16 *)dst_base, rect->w, rect->h, pitch, surface->pitch);
                converted = 1;
                break;
            case SDL_PIXELFORMAT_RGB888:
                Atari_ConvertRGB332toRGB888(src, dst_base, rect->w, rect->h, pitch, surface->pitch);
                converted = 1;
                break;
            case SDL_PIXELFORMAT_ARGB8888:
                Atari_ConvertRGB332toARGB8888(src, (Uint32 *)dst_base, rect->w, rect->h, pitch, surface->pitch);
                converted = 1;
                break;
        }
    }

    /* Fallback: plain memcpy for matching formats or unhandled conversions.
     * C90: declarations at top of block. */
    if (!converted) {
        const Uint8 *src;
        Uint8 *dst;
        int row;
        size_t row_bytes;

        src      = (const Uint8 *)pixels;
        dst      = (Uint8 *)surface->pixels +
                   rect->y * surface->pitch +
                   rect->x * surface->format->BytesPerPixel;
        row_bytes = (size_t)rect->w * surface->format->BytesPerPixel;

        for (row = 0; row < rect->h; ++row) {
            SDL_memcpy(dst, src, row_bytes);
            src += pitch;
            dst += surface->pitch;
        }
    }

    if (SDL_MUSTLOCK(surface)) {
        SDL_UnlockSurface(surface);
    }

    return 0;
}

/* ============================================================================
 * GEM_LockTexture
 *
 * When format conversion is needed (lock_buffer != NULL):
 *   - Record the locked rect
 *   - Return the lock buffer so the app writes RGB332 into it
 *   - Pitch = lock_rect.w (1 byte/pixel for RGB332)
 *
 * When no conversion is needed (lock_buffer == NULL):
 *   - Return a pointer directly into the surface, offset to rect's top-left
 *
 * Partial rects are fully supported in both paths.
 * ============================================================================ */

static int GEM_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                            const SDL_Rect *rect, void **pixels, int *pitch)
{
    GEM_TextureData *data = (GEM_TextureData *)texture->driverdata;
    (void)renderer;

    if (data->lock_buffer) {
        /* Store which region the app is locking; used by UnlockTexture. */
        if (rect) {
            data->lock_rect = *rect;
        } else {
            data->lock_rect.x = 0;
            data->lock_rect.y = 0;
            data->lock_rect.w = texture->w;
            data->lock_rect.h = texture->h;
        }

        *pixels = data->lock_buffer;
        *pitch  = data->lock_rect.w * SDL_BYTESPERPIXEL(data->src_format); /* RGB332: 1 byte/pixel, pitch = width */
        return 0;
    }

    /* Direct surface access - offset to rect's top-left if needed.
     * lock_rect is NOT updated here; it is only valid when lock_buffer != NULL. */
    if (rect) {
        *pixels = (Uint8 *)data->surface->pixels +
                  rect->y * data->surface->pitch +
                  rect->x * data->surface->format->BytesPerPixel;
    } else {
        *pixels = data->surface->pixels;
    }
    *pitch = data->surface->pitch;
    return 0;
}

/* ============================================================================
 * GEM_UnlockTexture
 *
 * If a lock buffer exists, convert its contents (RGB332) into the native
 * surface format using LUT converters, writing only the locked rect region.
 *
 * If no lock buffer, the app wrote directly to the surface - nothing to do.
 *
 * C90: src declared after early-return guard, inside inner block.
 * ============================================================================ */

static void GEM_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GEM_TextureData *data = (GEM_TextureData *)texture->driverdata;
    SDL_Surface *surface  = data->surface;
    (void)renderer;

    /* lock_rect is only valid when lock_buffer != NULL */
    if (!data->lock_buffer) {
        return;
    }

    {
        const Uint8 *src = (const Uint8 *)data->lock_buffer;
        Uint8 *dst_base = (Uint8 *)surface->pixels +
                            data->lock_rect.y * surface->pitch +
                            data->lock_rect.x * surface->format->BytesPerPixel;
        int src_pitch   = data->lock_rect.w * SDL_BYTESPERPIXEL(data->src_format);

        if (SDL_MUSTLOCK(surface)) {
            if (SDL_LockSurface(surface) < 0) return;
        }

        if (data->src_format == SDL_PIXELFORMAT_BGRA8888) {
            switch (surface->format->format) {
                case SDL_PIXELFORMAT_RGB332:
                    Atari_ConvertBGRA8888toRGB332(src, dst_base,
                                                    data->lock_rect.w, data->lock_rect.h,
                                                    src_pitch, surface->pitch);
                    break;
                default:
+                    /* Falcon TrueColor: SDL handles BGRA8888->any correctly */
+                    SDL_ConvertPixels(data->lock_rect.w, data->lock_rect.h,
+                                      SDL_PIXELFORMAT_BGRA8888, src, src_pitch,
+                                      surface->format->format, dst_base, surface->pitch);
                    break;
            }
        } else if (data->src_format == SDL_PIXELFORMAT_RGB332) {
            switch (surface->format->format) {
                case SDL_PIXELFORMAT_RGB565:
                    Atari_ConvertRGB332toRGB565(src, (Uint16 *)dst_base,
                                                data->lock_rect.w, data->lock_rect.h,
                                                src_pitch, surface->pitch);
                    break;
                case SDL_PIXELFORMAT_RGB888:
                    Atari_ConvertRGB332toRGB888(src, dst_base,
                                                data->lock_rect.w, data->lock_rect.h,
                                                src_pitch, surface->pitch);
                    break;
                case SDL_PIXELFORMAT_ARGB8888:
                    Atari_ConvertRGB332toARGB8888(src, (Uint32 *)dst_base,
                                                    data->lock_rect.w, data->lock_rect.h,
                                                    src_pitch, surface->pitch);
                    break;
                default:
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                "GEM: UnlockTexture: unknown surface format 0x%X - "
                                "frame will be corrupt",
                                (unsigned)surface->format->format);
                    break;
            }
        }

        if (SDL_MUSTLOCK(surface)) {
            SDL_UnlockSurface(surface);
        }
    }
}

static void GEM_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture,
                                     SDL_ScaleMode scaleMode)
{
    (void)renderer; (void)texture; (void)scaleMode;
}

static int GEM_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    (void)renderer;
    if (texture != NULL) {
        return SDL_Unsupported();
    }
    return 0;
}

static void GEM_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GEM_TextureData *data = (GEM_TextureData *)texture->driverdata;
    (void)renderer;

    if (data) {
        if (data->surface)     SDL_FreeSurface(data->surface);
        if (data->lock_buffer) SDL_free(data->lock_buffer);
        SDL_free(data);
        texture->driverdata = NULL;
    }
}

/* ============================================================================
 * Command Queue Stubs
 * ============================================================================ */

static int GEM_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer; (void)cmd;
    return 0;
}

static int GEM_QueueSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer; (void)cmd;
    return 0;
}

static int GEM_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                const SDL_FPoint *points, int count)
{
    SDL_FPoint *verts = (SDL_FPoint *)SDL_AllocateRenderVertices(renderer,
                                                                  count * sizeof(SDL_FPoint),
                                                                  0, &cmd->data.draw.first);
    if (!verts) return -1;
    SDL_memcpy(verts, points, count * sizeof(SDL_FPoint));
    return 0;
}

static int GEM_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                               const SDL_FPoint *points, int count)
{
    SDL_FPoint *verts = (SDL_FPoint *)SDL_AllocateRenderVertices(renderer,
                                                                  count * sizeof(SDL_FPoint),
                                                                  0, &cmd->data.draw.first);
    if (!verts) return -1;
    SDL_memcpy(verts, points, count * sizeof(SDL_FPoint));
    return 0;
}

static int GEM_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                               const SDL_FRect *rects, int count)
{
    SDL_FRect *verts = (SDL_FRect *)SDL_AllocateRenderVertices(renderer,
                                                                count * sizeof(SDL_FRect),
                                                                0, &cmd->data.draw.first);
    if (!verts) return -1;
    SDL_memcpy(verts, rects, count * sizeof(SDL_FRect));
    return 0;
}

static int GEM_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                         SDL_Texture *texture, const SDL_Rect *srcrect,
                         const SDL_FRect *dstrect)
{
    SDL_Rect *verts = (SDL_Rect *)SDL_AllocateRenderVertices(renderer,
                                                              2 * sizeof(SDL_Rect),
                                                              0, &cmd->data.draw.first);
    if (!verts) return -1;

    if (srcrect) {
        verts[0] = *srcrect;
    } else {
        verts[0].x = 0; verts[0].y = 0;
        verts[0].w = texture->w; verts[0].h = texture->h;
    }

    verts[1].x = (int)dstrect->x; verts[1].y = (int)dstrect->y;
    verts[1].w = (int)dstrect->w; verts[1].h = (int)dstrect->h;

    return 0;
}

static int GEM_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                            SDL_Texture *texture, const SDL_Rect *srcrect,
                            const SDL_FRect *dstrect, const double angle,
                            const SDL_FPoint *center, const SDL_RendererFlip flip,
                            float scale_x, float scale_y)
{
    (void)renderer; (void)cmd; (void)texture; (void)srcrect; (void)dstrect;
    (void)angle; (void)center; (void)flip; (void)scale_x; (void)scale_y;
    return 0;
}

static int GEM_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                              SDL_Texture *texture,
                              const float *xy, int xy_stride,
                              const SDL_Color *color, int color_stride,
                              const float *uv, int uv_stride,
                              int num_vertices, const void *indices,
                              int num_indices, int size_indices,
                              float scale_x, float scale_y)
{
    (void)renderer; (void)cmd; (void)texture;
    (void)xy; (void)xy_stride; (void)color; (void)color_stride;
    (void)uv; (void)uv_stride; (void)num_vertices; (void)indices;
    (void)num_indices; (void)size_indices; (void)scale_x; (void)scale_y;
    return SDL_Unsupported();
}

/* ============================================================================
 * Utilities
 * ============================================================================ */

static int GEM_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect,
                                 Uint32 format, void *pixels, int pitch)
{
    GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;
    SDL_Surface *surface;
    SDL_Rect safe_rect;
    Uint32 src_format;
    void *src_pixels;

    if (!data || !GEM_AcquireWindowSurface(data)) {
        return SDL_SetError("GEM: Could not acquire window surface");
    }

    surface = data->window_surface;
    if (!surface) {
        return SDL_SetError("GEM: Window surface is NULL");
    }

    safe_rect = *rect;
    if (safe_rect.x < 0) safe_rect.x = 0;
    if (safe_rect.y < 0) safe_rect.y = 0;
    if (safe_rect.x + safe_rect.w > surface->w) safe_rect.w = surface->w - safe_rect.x;
    if (safe_rect.y + safe_rect.h > surface->h) safe_rect.h = surface->h - safe_rect.y;
    if (safe_rect.w <= 0 || safe_rect.h <= 0) {
        return SDL_SetError("GEM: Invalid rect dimensions");
    }

    src_format = surface->format->format;
    src_pixels = (void *)((Uint8 *)surface->pixels +
                          safe_rect.y * surface->pitch +
                          safe_rect.x * surface->format->BytesPerPixel);

    /* SDL_ConvertPixels handles native->requested format correctly */
    return SDL_ConvertPixels(safe_rect.w, safe_rect.h,
                             src_format, src_pixels, surface->pitch,
                             format, pixels, pitch);
}

static void GEM_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
    GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;

    if (!data) return;

    if (event->event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        data->surface_acquired = SDL_FALSE;
        data->window_surface   = NULL;
        SDL_GetWindowSize(data->window, &data->window_w, &data->window_h);
        data->force_full_update = SDL_TRUE;
    }
}

static int GEM_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;

    if (!data) {
        return SDL_SetError("GEM: Renderer data is NULL");
    }

    if (w) *w = data->window_w;
    if (h) *h = data->window_h;
    return 0;
}

static void GEM_DestroyRenderer(SDL_Renderer *renderer)
{
    GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;

    if (data) {
        SDL_free(data);
        renderer->driverdata = NULL;
    }
}

/* ============================================================================
 * Render Driver Registration
 * ============================================================================ */

SDL_RenderDriver GEM_RenderDriver = {
    GEM_CreateRenderer,
    {
        "gem",
        SDL_RENDERER_SOFTWARE,
        5,
        {
            SDL_PIXELFORMAT_RGB332,
            SDL_PIXELFORMAT_RGB565,
            SDL_PIXELFORMAT_RGB888,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_PIXELFORMAT_BGRA8888
        },
        4096,
        4096
    }
};

#endif /* SDL_VIDEO_RENDER_GEM */