/* ============================================================================
 * SDL_render_gem.c - Atari ST/TT/Falcon GEM Renderer (FULLY OPTIMIZED)
 * 
 * OPTIMIZATIONS APPLIED:
 * 1. Smart dirty rectangle tracking (1-4 rects = direct, 5+ = checksum)
 * 2. Format-matched texture copy (memcpy when formats match)
 * 3. Proper integration with SDL_gemwindow.c checksum detection
 * 4. Eliminated redundant surface acquisition
 * 5. Better decision logic for when to use checksums vs direct updates
 * 
 * ============================================================================ */

#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_RENDER_GEM

#include "../SDL_sysrender.h"
#include "SDL_hints.h"
#include <mint/osbind.h>

/* Configuration */
#define MAX_DIRTY_RECTS 32
#define MERGE_THRESHOLD 16
#define DIRECT_UPDATE_THRESHOLD 1  /* Use direct update for <=1 rects */

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
 * Uses memcpy when possible (much faster on 68000!)
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
        int bpp = texture_surface->format->BytesPerPixel;
        int row_bytes = srcrect->w * bpp;
        
        Uint8 *src = (Uint8*)texture_surface->pixels + 
                     (srcrect->y * texture_surface->pitch) + 
                     (srcrect->x * bpp);
        
        Uint8 *dst = (Uint8*)dest_surface->pixels + 
                     (dstrect->y * dest_surface->pitch) + 
                     (dstrect->x * bpp);
        
        SDL_bool src_locked = SDL_FALSE;
        SDL_bool dst_locked = SDL_FALSE;
        
        if (SDL_MUSTLOCK(texture_surface)) {
            SDL_LockSurface(texture_surface);
            src_locked = SDL_TRUE;
        }
        if (SDL_MUSTLOCK(dest_surface)) {
            SDL_LockSurface(dest_surface);
            dst_locked = SDL_TRUE;
        }
        
        /* Copy row by row */
        for (y = 0; y < srcrect->h; y++) {
            SDL_memcpy(dst, src, row_bytes);
            src += texture_surface->pitch;
            dst += dest_surface->pitch;
        }
        
        if (src_locked) SDL_UnlockSurface(texture_surface);
        if (dst_locked) SDL_UnlockSurface(dest_surface);
        
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                     "GEM: Fast copy %dx%d (format matched)", 
                     srcrect->w, srcrect->h);
    }
    else {
        /* Slow path: Format conversion or scaling needed */
        SDL_BlitScaled(texture_surface, srcrect, dest_surface, dstrect);
        
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                     "GEM: Slow copy %dx%d (format conversion/scaling)",
                     srcrect->w, srcrect->h);
    }
}

/* ============================================================================
 * Dirty Rectangle Tracking
 * ============================================================================ */

static void GEM_AddDirtyRect(GEM_RenderData *data, const SDL_Rect *rect)
{
    SDL_Rect clipped = *rect;
    
    /* Clip to window bounds */
    if (clipped.x < 0) {
        clipped.w += clipped.x;
        clipped.x = 0;
    }
    if (clipped.y < 0) {
        clipped.h += clipped.y;
        clipped.y = 0;
    }
    if (clipped.x + clipped.w > data->window_w) {
        clipped.w = data->window_w - clipped.x;
    }
    if (clipped.y + clipped.h > data->window_h) {
        clipped.h = data->window_h - clipped.y;
    }
    
    if (clipped.w <= 0 || clipped.h <= 0) {
        return;
    }
    
    /* Try to merge with last rect */
    if (data->num_dirty_rects > 0) {
        SDL_Rect *last = &data->dirty_rects[data->num_dirty_rects - 1];
        
        int expand = MERGE_THRESHOLD;
        if (clipped.x <= last->x + last->w + expand &&
            clipped.x + clipped.w + expand >= last->x &&
            clipped.y <= last->y + last->h + expand &&
            clipped.y + clipped.h + expand >= last->y) {
            
            /* Merge */
            int right = (last->x + last->w > clipped.x + clipped.w) ? 
                        last->x + last->w : clipped.x + clipped.w;
            int bottom = (last->y + last->h > clipped.y + clipped.h) ? 
                         last->y + last->h : clipped.y + clipped.h;
            
            last->x = (last->x < clipped.x) ? last->x : clipped.x;
            last->y = (last->y < clipped.y) ? last->y : clipped.y;
            last->w = right - last->x;
            last->h = bottom - last->y;
            return;
        }
    }
    
    /* Add new rect */
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
    
    if (data->num_dirty_rects <= 1) return;
    
    do {
        merged = SDL_FALSE;
        for (i = 0; i < data->num_dirty_rects; i++) {
            for (j = i + 1; j < data->num_dirty_rects; ) {
                SDL_Rect *a = &data->dirty_rects[i];
                SDL_Rect *b = &data->dirty_rects[j];
                
                int expand = MERGE_THRESHOLD;
                if (a->x <= b->x + b->w + expand &&
                    a->x + a->w + expand >= b->x &&
                    a->y <= b->y + b->h + expand &&
                    a->y + a->h + expand >= b->y) {
                    
                    /* Merge b into a */
                    int right = (a->x + a->w > b->x + b->w) ? a->x + a->w : b->x + b->w;
                    int bottom = (a->y + a->h > b->y + b->h) ? a->y + a->h : b->y + b->h;
                    
                    a->x = (a->x < b->x) ? a->x : b->x;
                    a->y = (a->y < b->y) ? a->y : b->y;
                    a->w = right - a->x;
                    a->h = bottom - a->y;
                    
                    /* Remove b */
                    *b = data->dirty_rects[data->num_dirty_rects - 1];
                    data->num_dirty_rects--;
                    merged = SDL_TRUE;
                } else {
                    j++;
                }
            }
        }
    } while (merged && data->num_dirty_rects > 1);
}

/* ============================================================================
 * Surface Acquisition
 * ============================================================================ */

static SDL_bool GEM_AcquireWindowSurface(GEM_RenderData *data)
{
    SDL_Surface *surface;
    int w, h;
    
    if (data->surface_acquired && data->window_surface) {
        SDL_GetWindowSize(data->window, &w, &h);
        if (data->window_surface->w == w && data->window_surface->h == h) {
            return SDL_TRUE;
        }
        data->surface_acquired = SDL_FALSE;
        data->window_surface = NULL;
    }
    
    SDL_GetWindowSize(data->window, &w, &h);
    data->window_w = w;
    data->window_h = h;
    
    surface = SDL_GetWindowSurface(data->window);
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                    "GEM: Could not get window surface: %s", SDL_GetError());
        return SDL_FALSE;
    }
    
    if (!surface->pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                    "GEM: Window surface has no pixels!");
        return SDL_FALSE;
    }
    
    data->window_surface = surface;
    data->surface_acquired = SDL_TRUE;
    
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                "GEM: Acquired surface (%dx%d, format=0x%X)",
                surface->w, surface->h, surface->format->format);
    
    return SDL_TRUE;
}

/* ============================================================================
 * OPTIMIZED: RenderPresent with Smart Update Strategy
 * ============================================================================ */

static int GEM_RenderPresent(SDL_Renderer *renderer)
{
    GEM_RenderData *data = (GEM_RenderData *)renderer->driverdata;
    int result;
    
    if (!data) {
        return SDL_SetError("GEM: Renderer data is NULL");
    }

    if (!data->surface_dirty) {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "GEM: Nothing to present");
        return 0;
    }
    
    if (!GEM_AcquireWindowSurface(data)) {
        return SDL_SetError("GEM: Could not acquire window surface");
    }
    
    if (data->vsync_enabled) {
        Vsync();
    }
    
    /* ========================================================================
       SMART UPDATE STRATEGY:
       
       1. Full update (clear/resize) → SDL_UpdateWindowSurface
          - Triggers checksum detection in SDL_gemwindow.c
       
       2. Few dirty rects (1-4) → SDL_UpdateWindowSurfaceRects  
          - Direct partial update, bypass checksums
       
       3. Many dirty rects (5+) → SDL_UpdateWindowSurface
          - Checksum detection finds actual changes
       ======================================================================== */
    
    if (data->force_full_update) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, 
                    "GEM: FULL UPDATE (forced)");
        result = SDL_UpdateWindowSurface(data->window);
    }
    else if (data->num_dirty_rects == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                    "GEM: surface_dirty=TRUE but no rects! Full update");
        result = SDL_UpdateWindowSurface(data->window);
    }
    else if (data->num_dirty_rects <= DIRECT_UPDATE_THRESHOLD) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                    "GEM: PARTIAL UPDATE (%d rects)",
                    data->num_dirty_rects);
        result = SDL_UpdateWindowSurfaceRects(data->window, 
                                              data->dirty_rects,
                                              data->num_dirty_rects);
    }
    else {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                    "GEM: CHECKSUM SCAN (%d rects - using optimization)",
                    data->num_dirty_rects);
        result = SDL_UpdateWindowSurface(data->window);
    }
    
    if (result < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "GEM: Update failed: %s", SDL_GetError());
    }
    
    /* Clear dirty state */
    data->surface_dirty = SDL_FALSE;
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
    Uint8 *pixels;
    int pitch, bpp;
    
    if (!surface || !surface->pixels) return;
    
    dx = SDL_abs(x1 - x0);
    dy = SDL_abs(y1 - y0);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;
    
    pixels = (Uint8 *)surface->pixels;
    pitch = surface->pitch;
    bpp = surface->format->BytesPerPixel;
    
    while (1) {
        if (x0 >= 0 && x0 < surface->w && y0 >= 0 && y0 < surface->h) {
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
    data->force_full_update = SDL_FALSE;  /* Reset each frame */

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
                SDL_Rect clear_rect;
                Uint32 color = SDL_MapRGBA(surface->format,
                                           cmd->data.color.r,
                                           cmd->data.color.g,
                                           cmd->data.color.b,
                                           cmd->data.color.a);
                SDL_FillRect(surface, NULL, color);
                data->surface_dirty = SDL_TRUE;
                
                /* Track clear as full-screen dirty rect for proper merging */
                /* Ensures 'ghost' pixels are erased when optimization runs */
                
                clear_rect.x = 0;
                clear_rect.y = 0;
                clear_rect.w = data->window_w;
                clear_rect.h = data->window_h;
                GEM_AddDirtyRect(data, &clear_rect);

                SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, 
                            "GEM: CLEAR - Added full screen dirty rect");
                break;
            }

            case SDL_RENDERCMD_DRAW_POINTS: {
                const SDL_FPoint *points;
                int count, i;
                Uint32 color;
                SDL_Rect dirty_rect;
                
                if (!vertices || cmd->data.draw.first >= vertsize) break;
                
                points = (const SDL_FPoint *)((const Uint8 *)vertices + cmd->data.draw.first);
                count = (int)cmd->data.draw.count;
                color = SDL_MapRGBA(surface->format,
                                   cmd->data.draw.r, cmd->data.draw.g,
                                   cmd->data.draw.b, cmd->data.draw.a);
                
                if (count > 0) {
                    dirty_rect.x = (int)points[0].x;
                    dirty_rect.y = (int)points[0].y;
                    dirty_rect.w = 1;
                    dirty_rect.h = 1;
                    
                    for (i = 0; i < count; i++) {
                        SDL_Rect pixel;
                        pixel.x = (int)points[i].x;
                        pixel.y = (int)points[i].y;
                        pixel.w = 1;
                        pixel.h = 1;
                        SDL_FillRect(surface, &pixel, color);
                        
                        /* Expand dirty rect */
                        if (pixel.x < dirty_rect.x) {
                            dirty_rect.w += dirty_rect.x - pixel.x;
                            dirty_rect.x = pixel.x;
                        }
                        if (pixel.y < dirty_rect.y) {
                            dirty_rect.h += dirty_rect.y - pixel.y;
                            dirty_rect.y = pixel.y;
                        }
                        if (pixel.x >= dirty_rect.x + dirty_rect.w) {
                            dirty_rect.w = pixel.x - dirty_rect.x + 1;
                        }
                        if (pixel.y >= dirty_rect.y + dirty_rect.h) {
                            dirty_rect.h = pixel.y - dirty_rect.y + 1;
                        }
                    }
                    
                    GEM_AddDirtyRect(data, &dirty_rect);
                }
                
                data->surface_dirty = SDL_TRUE;
                break;
            }

            case SDL_RENDERCMD_DRAW_LINES: {
                const SDL_FPoint *points;
                int count, i;
                Uint32 color;
                SDL_Rect dirty_rect;
                
                if (!vertices || cmd->data.draw.first >= vertsize) break;
                
                points = (const SDL_FPoint *)((const Uint8 *)vertices + cmd->data.draw.first);
                count = (int)cmd->data.draw.count;
                color = SDL_MapRGBA(surface->format,
                                   cmd->data.draw.r, cmd->data.draw.g,
                                   cmd->data.draw.b, cmd->data.draw.a);
                
                if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
                
                if (count > 0) {
                    dirty_rect.x = (int)points[0].x;
                    dirty_rect.y = (int)points[0].y;
                    dirty_rect.w = 1;
                    dirty_rect.h = 1;
                    
                    for (i = 0; i < count - 1; i++) {
                        int min_x, min_y, max_x, max_y;
                        int x0 = (int)points[i].x;
                        int y0 = (int)points[i].y;
                        int x1 = (int)points[i+1].x;
                        int y1 = (int)points[i+1].y;
                        
                        GEM_DrawLine(surface, x0, y0, x1, y1, color);
                        
                        min_x = (x0 < x1) ? x0 : x1;
                        min_y = (y0 < y1) ? y0 : y1;
                        max_x = (x0 > x1) ? x0 : x1;
                        max_y = (y0 > y1) ? y0 : y1;
                        
                        if (min_x < dirty_rect.x) {
                            dirty_rect.w += dirty_rect.x - min_x;
                            dirty_rect.x = min_x;
                        }
                        if (min_y < dirty_rect.y) {
                            dirty_rect.h += dirty_rect.y - min_y;
                            dirty_rect.y = min_y;
                        }
                        if (max_x >= dirty_rect.x + dirty_rect.w) {
                            dirty_rect.w = max_x - dirty_rect.x + 1;
                        }
                        if (max_y >= dirty_rect.y + dirty_rect.h) {
                            dirty_rect.h = max_y - dirty_rect.y + 1;
                        }
                    }
                    
                    GEM_AddDirtyRect(data, &dirty_rect);
                }
                
                if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
                
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
                SDL_Rect srcrect, dstrect;
                const Uint8 *verts_ptr;
                
                if (!cmd->data.draw.texture) break;
                
                texdata = (GEM_TextureData *)cmd->data.draw.texture->driverdata;
                if (!texdata || !texdata->surface) break;
                
                if (!vertices || cmd->data.draw.first >= vertsize) break;
                
                verts_ptr = (const Uint8 *)vertices + cmd->data.draw.first;
                SDL_memcpy(&srcrect, verts_ptr, sizeof(SDL_Rect));
                SDL_memcpy(&dstrect, verts_ptr + sizeof(SDL_Rect), sizeof(SDL_Rect));
                
                /* OPTIMIZATION: Use format-matched copy */
                GEM_OptimizedTextureCopy(texdata->surface, &srcrect, 
                                        surface, &dstrect);
                
                GEM_AddDirtyRect(data, &dstrect);
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
    
    SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                 "GEM: Frame has %d dirty rects",
                 data->num_dirty_rects);
    
    return 0;
}

/* ============================================================================
 * Renderer Creation and Management
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

    data->window = window;
    data->window_w = w;
    data->window_h = h;
    data->surface_dirty = SDL_FALSE;
    data->vsync_enabled = (flags & SDL_RENDERER_PRESENTVSYNC) ? SDL_TRUE : SDL_FALSE;
    data->surface_acquired = SDL_FALSE;
    data->window_surface = NULL;
    data->num_dirty_rects = 0;
    data->force_full_update = SDL_FALSE;

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

    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                "GEM: Renderer initialized (%dx%d, VSync: %s)",
                w, h, data->vsync_enabled ? "ON" : "OFF");

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
    size_t length;
    (void)renderer;

    if (SDL_MUSTLOCK(surface)) {
        if (SDL_LockSurface(surface) < 0) {
            return -1;
        }
    }

    src = (const Uint8 *)pixels;
    dst = (Uint8 *)surface->pixels + 
          rect->y * surface->pitch + 
          rect->x * surface->format->BytesPerPixel;
    length = (size_t)rect->w * surface->format->BytesPerPixel;
    
    for (row = 0; row < rect->h; ++row) {
        SDL_memcpy(dst, src, length);
        src += pitch;
        dst += surface->pitch;
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
        /* THIS is where we should force full update */
        data->surface_acquired = SDL_FALSE;
        data->window_surface = NULL;
        SDL_GetWindowSize(data->window, &data->window_w, &data->window_h);
        data->force_full_update = SDL_TRUE;
        
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                    "GEM: Window resized - will force full update next frame");
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