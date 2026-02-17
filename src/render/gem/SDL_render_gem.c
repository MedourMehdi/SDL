/* ============================================================================
 * SDL_render_gem.c - Atari ST/TT/Falcon GEM Renderer (OPTIMIZED)
 * 
 * OPTIMIZATIONS APPLIED:
 * 1. Smart dirty rectangle tracking (1-4 rects = direct, 5+ = checksum)
 * 2. Format-matched texture copy (memcpy when formats match)
 * 3. Proper integration with SDL_gemwindow.c checksum detection
 * 4. Eliminated redundant surface acquisition and double GetWindowSize calls
 * 5. Better decision logic for when to use checksums vs direct updates
 * 6. Memory access optimizations: bit flags, cached locals, precomputed edges
 * 7. Removed redundant zero-initialization after SDL_calloc
 * 8. Simplified control flow and reduced branching in hot paths
 * 9. Iteration limit on merge loop to prevent O(n²) worst case
 * 10. C90 compliant
 * ============================================================================ */

#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_RENDER_GEM

#include "../SDL_sysrender.h"
#include "SDL_hints.h"
#include <mint/osbind.h>

/* Configuration */
#define MAX_DIRTY_RECTS 32
#define MERGE_THRESHOLD 16
#define DIRECT_UPDATE_THRESHOLD 4  /* Use direct update for <=4 rects */
#define MAX_MERGE_ITERATIONS 5     /* Prevent O(n²) worst case */

/* Private Data Structures */
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
} GEM_TextureData;

/* Forward Declarations */
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
 * OPTIMIZED: Format-Matched Texture Copy
 * Uses memcpy when possible, bit flags instead of separate bools
 * ============================================================================ */

static void GEM_OptimizedTextureCopy(SDL_Surface *texture_surface,
                                     const SDL_Rect *srcrect,
                                     SDL_Surface *dest_surface,
                                     SDL_Rect *dstrect)
{
    /* Check if we can use fast path */
    if (texture_surface->format->format == dest_surface->format->format &&
        srcrect->w == dstrect->w && 
        srcrect->h == dstrect->h) {
        
        /* Fast path: Direct memcpy */
        int y;
        const int bpp = texture_surface->format->BytesPerPixel;
        const int row_bytes = srcrect->w * bpp;
        const int src_pitch = texture_surface->pitch;
        const int dst_pitch = dest_surface->pitch;
        Uint8 *src, *dst;
        int lock_mask = 0;  /* bit 0 = src locked, bit 1 = dst locked */
        
        /* Lock both at once if needed */
        if (SDL_MUSTLOCK(texture_surface)) {
            SDL_LockSurface(texture_surface);
            lock_mask |= 1;
        }
        if (SDL_MUSTLOCK(dest_surface)) {
            SDL_LockSurface(dest_surface);
            lock_mask |= 2;
        }
        
        /* Compute pointers after locking (pixels may change) */
        src = (Uint8*)texture_surface->pixels + 
              (srcrect->y * src_pitch) + (srcrect->x * bpp);
        dst = (Uint8*)dest_surface->pixels + 
              (dstrect->y * dst_pitch) + (dstrect->x * bpp);
        
        /* Copy row by row */
        for (y = 0; y < srcrect->h; y++) {
            SDL_memcpy(dst, src, row_bytes);
            src += src_pitch;
            dst += dst_pitch;
        }
        
        /* Unlock in reverse order */
        if (lock_mask & 2) SDL_UnlockSurface(dest_surface);
        if (lock_mask & 1) SDL_UnlockSurface(texture_surface);
    }
    else {
        /* Slow path: Format conversion or scaling needed */
        SDL_BlitScaled(texture_surface, srcrect, dest_surface, dstrect);
    }
}

/* ============================================================================
 * Dirty Rectangle Tracking
 * OPTIMIZED: Precompute right/bottom edges, simpler merge logic
 * ============================================================================ */

static void GEM_AddDirtyRect(GEM_RenderData *data, const SDL_Rect *rect)
{
    SDL_Rect clipped;
    SDL_Rect *last;
    int new_right, new_bottom, last_right, last_bottom;
    int min_x, min_y, max_x, max_y;
    
    /* Clip to window bounds - batch the comparisons */
    clipped.x = (rect->x < 0) ? 0 : rect->x;
    clipped.y = (rect->y < 0) ? 0 : rect->y;
    clipped.w = rect->w + ((rect->x < 0) ? rect->x : 0);
    clipped.h = rect->h + ((rect->y < 0) ? rect->y : 0);
    
    new_right = clipped.x + clipped.w;
    new_bottom = clipped.y + clipped.h;
    
    if (new_right > data->window_w) clipped.w = data->window_w - clipped.x;
    if (new_bottom > data->window_h) clipped.h = data->window_h - clipped.y;
    
    if (clipped.w <= 0 || clipped.h <= 0) {
        return;
    }
    
    /* Try to merge with last rect */
    if (data->num_dirty_rects > 0) {
        last = &data->dirty_rects[data->num_dirty_rects - 1];
        last_right = last->x + last->w;
        last_bottom = last->y + last->h;
        
        /* Check if rects are close enough to merge (within MERGE_THRESHOLD) */
        if (clipped.x <= last_right + MERGE_THRESHOLD &&
            new_right + MERGE_THRESHOLD >= last->x &&
            clipped.y <= last_bottom + MERGE_THRESHOLD &&
            new_bottom + MERGE_THRESHOLD >= last->y) {
            
            /* Merge: compute union */
            min_x = (last->x < clipped.x) ? last->x : clipped.x;
            min_y = (last->y < clipped.y) ? last->y : clipped.y;
            max_x = (last_right > new_right) ? last_right : new_right;
            max_y = (last_bottom > new_bottom) ? last_bottom : new_bottom;
            
            last->x = min_x;
            last->y = min_y;
            last->w = max_x - min_x;
            last->h = max_y - min_y;
            return;
        }
    }
    
    /* Add new rect if space available */
    if (data->num_dirty_rects < MAX_DIRTY_RECTS) {
        data->dirty_rects[data->num_dirty_rects++] = clipped;
    } else {
        /* Too many rects - force full update */
        data->force_full_update = SDL_TRUE;
    }
}

static void GEM_MergeDirtyRects(GEM_RenderData *data)
{
    int i, j;
    SDL_bool merged;
    int iterations = 0;
    
    if (data->num_dirty_rects <= 1) return;
    
    do {
        merged = SDL_FALSE;
        iterations++;
        
        for (i = 0; i < data->num_dirty_rects; i++) {
            for (j = i + 1; j < data->num_dirty_rects; ) {
                SDL_Rect *a = &data->dirty_rects[i];
                SDL_Rect *b = &data->dirty_rects[j];
                int a_right = a->x + a->w;
                int a_bottom = a->y + a->h;
                int b_right = b->x + b->w;
                int b_bottom = b->y + b->h;
                int min_x, min_y, max_x, max_y;
                
                /* Check overlap/proximity */
                if (a->x <= b_right + MERGE_THRESHOLD &&
                    a_right + MERGE_THRESHOLD >= b->x &&
                    a->y <= b_bottom + MERGE_THRESHOLD &&
                    a_bottom + MERGE_THRESHOLD >= b->y) {
                    
                    /* Merge b into a */
                    min_x = (a->x < b->x) ? a->x : b->x;
                    min_y = (a->y < b->y) ? a->y : b->y;
                    max_x = (a_right > b_right) ? a_right : b_right;
                    max_y = (a_bottom > b_bottom) ? a_bottom : b_bottom;
                    
                    a->x = min_x;
                    a->y = min_y;
                    a->w = max_x - min_x;
                    a->h = max_y - min_y;
                    
                    /* Remove b */
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
 * OPTIMIZED: Removed double GetWindowSize call, simplified logic
 * ============================================================================ */

static SDL_bool GEM_AcquireWindowSurface(GEM_RenderData *data)
{
    SDL_Surface *surface;
    int w, h;
    
    /* Quick path: already have valid surface */
    if (data->surface_acquired && data->window_surface) {
        return SDL_TRUE;
    }
    
    /* Slow path: need to acquire surface */
    SDL_GetWindowSize(data->window, &w, &h);
    data->window_w = w;
    data->window_h = h;
    
    surface = SDL_GetWindowSurface(data->window);
    if (!surface) {
        return SDL_FALSE;
    }
    
    if (!surface->pixels) {
        return SDL_FALSE;
    }
    
    data->window_surface = surface;
    data->surface_acquired = SDL_TRUE;
    
    return SDL_TRUE;
}

/* ============================================================================
 * OPTIMIZED: RenderPresent - Simplified control flow
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
    
    /* Smart update strategy: partial vs full update */
    if (data->num_dirty_rects > 0 && 
        data->num_dirty_rects <= DIRECT_UPDATE_THRESHOLD &&
        !data->force_full_update) {
        /* Direct partial update */
        result = SDL_UpdateWindowSurfaceRects(data->window, 
                                              data->dirty_rects,
                                              data->num_dirty_rects);
    } else {
        /* Full update or checksum scan */
        result = SDL_UpdateWindowSurface(data->window);
    }
    
    /* Clear dirty state */
    data->surface_dirty = SDL_FALSE;
    data->num_dirty_rects = 0;
    data->force_full_update = SDL_FALSE;
    
    return result;
}

/* ============================================================================
 * Bresenham Line Drawing
 * OPTIMIZED: Cached const locals
 * ============================================================================ */

static void GEM_DrawLine(SDL_Surface *surface, int x0, int y0, int x1, int y1, 
                         Uint32 color)
{
    int dx, dy, sx, sy, err, e2;
    const int pitch = surface->pitch;
    const int bpp = surface->format->BytesPerPixel;
    const int w = surface->w;
    const int h = surface->h;
    Uint8 *pixels = (Uint8 *)surface->pixels;
    
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
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* ============================================================================
 * RunCommandQueue - Process all rendering commands
 * OPTIMIZED: Simplified dirty rect tracking for points/lines
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
    data->num_dirty_rects = 0;
    data->force_full_update = SDL_FALSE;

    /* Process commands */
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
                count = (int)cmd->data.draw.count;
                
                if (count <= 0) break;
                
                color = SDL_MapRGBA(surface->format,
                                   cmd->data.draw.r, cmd->data.draw.g,
                                   cmd->data.draw.b, cmd->data.draw.a);
                
                /* Initialize bounds from first point */
                min_x = max_x = (int)points[0].x;
                min_y = max_y = (int)points[0].y;
                
                for (i = 0; i < count; i++) {
                    SDL_Rect pixel;
                    int px = (int)points[i].x;
                    int py = (int)points[i].y;
                    
                    pixel.x = px;
                    pixel.y = py;
                    pixel.w = 1;
                    pixel.h = 1;
                    SDL_FillRect(surface, &pixel, color);
                    
                    /* Update bounds */
                    if (px < min_x) min_x = px;
                    if (px > max_x) max_x = px;
                    if (py < min_y) min_y = py;
                    if (py > max_y) max_y = py;
                }
                
                /* Add single dirty rect covering all points */
                {
                    SDL_Rect dirty;
                    dirty.x = min_x;
                    dirty.y = min_y;
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
                count = (int)cmd->data.draw.count;
                
                if (count <= 0) break;
                
                color = SDL_MapRGBA(surface->format,
                                   cmd->data.draw.r, cmd->data.draw.g,
                                   cmd->data.draw.b, cmd->data.draw.a);
                
                if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
                
                /* Initialize bounds */
                min_x = max_x = (int)points[0].x;
                min_y = max_y = (int)points[0].y;
                
                for (i = 0; i < count - 1; i++) {
                    int x0 = (int)points[i].x;
                    int y0 = (int)points[i].y;
                    int x1 = (int)points[i+1].x;
                    int y1 = (int)points[i+1].y;
                    int line_min_x, line_min_y, line_max_x, line_max_y;
                    
                    GEM_DrawLine(surface, x0, y0, x1, y1, color);
                    
                    line_min_x = (x0 < x1) ? x0 : x1;
                    line_min_y = (y0 < y1) ? y0 : y1;
                    line_max_x = (x0 > x1) ? x0 : x1;
                    line_max_y = (y0 > y1) ? y0 : y1;
                    
                    if (line_min_x < min_x) min_x = line_min_x;
                    if (line_min_y < min_y) min_y = line_min_y;
                    if (line_max_x > max_x) max_x = line_max_x;
                    if (line_max_y > max_y) max_y = line_max_y;
                }
                
                if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
                
                /* Add single dirty rect */
                {
                    SDL_Rect dirty;
                    dirty.x = min_x;
                    dirty.y = min_y;
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
                    r.x = (int)rects[i].x;
                    r.y = (int)rects[i].y;
                    r.w = (int)rects[i].w;
                    r.h = (int)rects[i].h;
                    SDL_FillRect(surface, &r, color);
                    GEM_AddDirtyRect(data, &r);
                }
                
                data->surface_dirty = SDL_TRUE;
                break;
            }

            case SDL_RENDERCMD_COPY: {
                GEM_TextureData *texdata;
                SDL_Rect verts[2];  /* srcrect and dstrect */
                
                if (!cmd->data.draw.texture) break;
                
                texdata = (GEM_TextureData *)cmd->data.draw.texture->driverdata;
                if (!texdata || !texdata->surface) break;
                
                if (!vertices || cmd->data.draw.first >= vertsize) break;
                
                /* Copy both rects at once */
                SDL_memcpy(verts, (const Uint8 *)vertices + cmd->data.draw.first, 
                           2 * sizeof(SDL_Rect));
                
                /* OPTIMIZATION: Use format-matched copy */
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
    
    /* Merge dirty rects to reduce overhead */
    GEM_MergeDirtyRects(data);
    
    return 0;
}

/* ============================================================================
 * Renderer Creation and Management
 * OPTIMIZED: Removed redundant zero-initialization after SDL_calloc
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
    data->window = window;
    data->window_w = w;
    data->window_h = h;
    data->vsync_enabled = (flags & SDL_RENDERER_PRESENTVSYNC) ? SDL_TRUE : SDL_FALSE;
    
    /* All other fields are already zero/NULL/FALSE from SDL_calloc */

    renderer->WindowEvent = GEM_WindowEvent;
    renderer->GetOutputSize = GEM_GetOutputSize;
    renderer->CreateTexture = GEM_CreateTexture;
    renderer->UpdateTexture = GEM_UpdateTexture;
    renderer->LockTexture = GEM_LockTexture;
    renderer->UnlockTexture = GEM_UnlockTexture;
    renderer->SetTextureScaleMode = GEM_SetTextureScaleMode;
    renderer->SetRenderTarget = GEM_SetRenderTarget;
    renderer->QueueSetViewport = GEM_QueueSetViewport;
    renderer->QueueSetDrawColor = GEM_QueueSetDrawColor;
    renderer->QueueDrawPoints = GEM_QueueDrawPoints;
    renderer->QueueDrawLines = GEM_QueueDrawLines;
    renderer->QueueFillRects = GEM_QueueFillRects;
    renderer->QueueCopy = GEM_QueueCopy;
    renderer->QueueCopyEx = GEM_QueueCopyEx;
    renderer->QueueGeometry = GEM_QueueGeometry;
    renderer->RunCommandQueue = GEM_RunCommandQueue;
    renderer->RenderReadPixels = GEM_RenderReadPixels;
    renderer->RenderPresent = GEM_RenderPresent;
    renderer->DestroyTexture = GEM_DestroyTexture;
    renderer->DestroyRenderer = GEM_DestroyRenderer;

    renderer->info.name = "gem";
    renderer->info.flags = SDL_RENDERER_SOFTWARE;
    if (data->vsync_enabled) {
        renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    renderer->info.num_texture_formats = 4;
    renderer->info.texture_formats[0] = SDL_PIXELFORMAT_RGB332;
    renderer->info.texture_formats[1] = SDL_PIXELFORMAT_RGB565;
    renderer->info.texture_formats[2] = SDL_PIXELFORMAT_RGB888;
    renderer->info.texture_formats[3] = SDL_PIXELFORMAT_ARGB8888;
    renderer->info.max_texture_width = 4096;
    renderer->info.max_texture_height = 4096;

    renderer->driverdata = data;

    return 0;
}

/* Texture Management */
static int GEM_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GEM_TextureData *data;
    (void)renderer;

    data = (GEM_TextureData *)SDL_calloc(1, sizeof(GEM_TextureData));
    if (!data) {
        return SDL_OutOfMemory();
    }

    data->surface = SDL_CreateRGBSurfaceWithFormat(0, texture->w, texture->h,
                                                   0, texture->format);
    if (!data->surface) {
        SDL_free(data);
        return SDL_SetError("GEM: Could not create texture surface");
    }

    texture->driverdata = data;
    return 0;
}

static int GEM_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *rect, const void *pixels, int pitch)
{
    GEM_TextureData *data = (GEM_TextureData *)texture->driverdata;
    SDL_Surface *surface = data->surface;
    const Uint8 *src;
    Uint8 *dst;
    int row;
    const size_t length = (size_t)rect->w * surface->format->BytesPerPixel;
    const int dst_pitch = surface->pitch;
    
    (void)renderer;

    if (SDL_MUSTLOCK(surface)) {
        if (SDL_LockSurface(surface) < 0) {
            return -1;
        }
    }

    src = (const Uint8 *)pixels;
    dst = (Uint8 *)surface->pixels + 
          rect->y * dst_pitch + 
          rect->x * surface->format->BytesPerPixel;
    
    for (row = 0; row < rect->h; ++row) {
        SDL_memcpy(dst, src, length);
        src += pitch;
        dst += dst_pitch;
    }

    if (SDL_MUSTLOCK(surface)) {
        SDL_UnlockSurface(surface);
    }

    return 0;
}

static int GEM_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                            const SDL_Rect *rect, void **pixels, int *pitch)
{
    GEM_TextureData *data = (GEM_TextureData *)texture->driverdata;
    (void)renderer;
    (void)rect;

    *pixels = (void *)data->surface->pixels;
    *pitch = data->surface->pitch;
    
    return 0;
}

static void GEM_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    (void)renderer;
    (void)texture;
}

static void GEM_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture,
                                     SDL_ScaleMode scaleMode)
{
    (void)renderer;
    (void)texture;
    (void)scaleMode;
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
        if (data->surface) {
            SDL_FreeSurface(data->surface);
        }
        SDL_free(data);
        texture->driverdata = NULL;
    }
}

/* Command Queue Stubs */
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
        verts[0].x = 0;
        verts[0].y = 0;
        verts[0].w = texture->w;
        verts[0].h = texture->h;
    }

    verts[1].x = (int)dstrect->x;
    verts[1].y = (int)dstrect->y;
    verts[1].w = (int)dstrect->w;
    verts[1].h = (int)dstrect->h;

    return 0;
}

static int GEM_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                            SDL_Texture *texture, const SDL_Rect *srcrect,
                            const SDL_FRect *dstrect, const double angle,
                            const SDL_FPoint *center, const SDL_RendererFlip flip,
                            float scale_x, float scale_y)
{
    (void)renderer; (void)texture; (void)srcrect; (void)dstrect;
    (void)angle; (void)center; (void)flip; (void)scale_x; (void)scale_y; (void)cmd;
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
    (void)renderer; (void)texture; (void)xy; (void)xy_stride;
    (void)color; (void)color_stride; (void)uv; (void)uv_stride;
    (void)num_vertices; (void)indices; (void)num_indices; (void)size_indices;
    (void)scale_x; (void)scale_y; (void)cmd;
    return SDL_Unsupported();
}

/* Utilities */
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
    if (safe_rect.x + safe_rect.w > surface->w) {
        safe_rect.w = surface->w - safe_rect.x;
    }
    if (safe_rect.y + safe_rect.h > surface->h) {
        safe_rect.h = surface->h - safe_rect.y;
    }
    if (safe_rect.w <= 0 || safe_rect.h <= 0) {
        return SDL_SetError("GEM: Invalid rect dimensions");
    }
    
    src_format = surface->format->format;
    src_pixels = (void *)((Uint8 *)surface->pixels +
                          safe_rect.y * surface->pitch +
                          safe_rect.x * surface->format->BytesPerPixel);
    
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
        data->window_surface = NULL;
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

/* Render Driver Registration */
SDL_RenderDriver GEM_RenderDriver = {
    GEM_CreateRenderer,
    {
        "gem",
        SDL_RENDERER_SOFTWARE,
        4,
        {
            SDL_PIXELFORMAT_RGB332,
            SDL_PIXELFORMAT_RGB565,
            SDL_PIXELFORMAT_RGB888,
            SDL_PIXELFORMAT_ARGB8888
        },
        4096,
        4096
    }
};

#endif /* SDL_VIDEO_RENDER_GEM */