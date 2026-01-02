/* ============================================
   FILE: src/video/gem/SDL_gemwindow.c
   GEM window management - FINAL PRODUCTION VERSION
   ============================================ */

#include "../../SDL_internal.h"
#include "SDL_gemvideo.h"
#include "../atari/SDL_atarivideo.h"

#ifdef SDL_VIDEO_DRIVER_GEM
/* GEM VDI stride alignment: round up to 16-pixel boundary in bits */
#define MFDB_STRIDE(w) (((w) + 15) & -16)

/* Calculate buffer dimensions based on color depth */
static void CalculateBufferSizes(int planes, int w, int h, int *pitch, int *chunky_size, int *planar_size)
{
    switch (planes) {
        case 1: case 2: case 4: case 8:
            *pitch = w;  /* Packed chunky */
            *planar_size = ((w + 15) / 16) * 2 * h * planes;
            break;
        case 16:
            *pitch = MFDB_STRIDE(w) * 2;  /* Padded chunky */
            *planar_size = MFDB_STRIDE(w) * h * 2;  /* Padded planar */
            break;
        case 24:
            *pitch = MFDB_STRIDE(w) * 3;
            *planar_size = MFDB_STRIDE(w) * h * 3;
            break;
        case 32:
            *pitch = MFDB_STRIDE(w) * 4;
            *planar_size = MFDB_STRIDE(w) * h * 4;
            break;
        default:
            *pitch = w * 4;  /* Safe default */
            *planar_size = MFDB_STRIDE(w) * h * 4;
            break;
    }
    *chunky_size = *pitch * h;
}

int GEM_CreateWindow(_THIS, SDL_Window *window)
{
    int w, h, x, y;
    SDL_WindowData *data = (SDL_WindowData *)SDL_calloc(1, sizeof(SDL_WindowData));
    if (!data) {
        return SDL_OutOfMemory();
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "1. GEM_CreateWindow called");

    data->win_type = NAME | CLOSER | MOVER;
    if (!(window->flags & SDL_WINDOW_BORDERLESS)) {
        data->win_type |= SIZER | FULLER;
    }

    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    data->work_x = x;
    data->work_y = y;
    data->work_w = w;
    data->work_h = h;
    printf("GEM: Requested window size %dx%d at %d,%d\n", 
        data->work_w, data->work_h, data->work_x, data->work_y);
    // SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "2. GEM_CreateWindow: requested size %dx%d at %d,%d",
    //     data->work_w, data->work_h, data->work_x, data->work_y);

    mt_wind_calc(WC_BORDER, data->win_type, 
            data->work_x, data->work_y, data->work_w, data->work_h,
            &data->win_x, &data->win_y, &data->win_w, &data->win_h, sdl_global_aes); 

    data->handle = mt_wind_create(data->win_type, 
                               data->win_x, data->win_y, data->win_w, data->win_h, sdl_global_aes);
    
    if (data->handle < 0) {
        SDL_free(data);
        return SDL_SetError("Can't create GEM window");
    }

    mt_wind_set_str(data->handle, WF_NAME, window->title ? window->title : "SDL2", sdl_global_aes);
    mt_wind_open(data->handle, data->win_x, data->win_y, data->win_w, data->win_h, sdl_global_aes);
    printf("GEM: Created window '%s' (handle %d) at %d,%d %dx%d\n", 
        window->title ? window->title : "SDL2 WINDOW",
        data->handle, data->win_x, data->win_y, data->win_w, data->win_h);
    /* Initialize tracking */
    data->last_w = data->work_w;
    data->last_h = data->work_h;
    data->is_maximized = SDL_FALSE;
    data->in_gem_redraw = SDL_FALSE;
    
    window->driverdata = data;
    return 0;
}

void GEM_DestroyWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_DestroyWindow called");
    if (data) {
        if (data->handle >= 0) {
            mt_wind_close(data->handle, sdl_global_aes);
            mt_wind_delete(data->handle, sdl_global_aes);
        }
        if (data->buffer) {
            SDL_free(data->buffer);
        }
        if (data->planar_buffer) {
            SDL_free(data->planar_buffer);
        }
        SDL_free(data);
        window->driverdata = NULL;
    }
}

int GEM_CreateWindowFramebuffer(_THIS, SDL_Window *window, 
                               Uint32 *format, void **pixels, int *pitch)
{
    SDL_VideoData *video = (SDL_VideoData *)_this->driverdata;
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    int w, h;
    int chunky_size, planar_size;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_CreateWindowFramebuffer called");
    if (!data) {
        return SDL_SetError("Window data not found");
    }

    SDL_GetWindowSize(window, &w, &h);
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Window size: %dx%d at position %d,%d", w, h, window->x, window->y);
    /* Free existing buffers to prevent memory leak */
    if (data->buffer) {
        SDL_free(data->buffer);
        data->buffer = NULL;
    }
    if (data->planar_buffer) {
        SDL_free(data->planar_buffer);
        data->planar_buffer = NULL;
    }

    CalculateBufferSizes(video->planes, w, h, pitch, &chunky_size, &planar_size);
    
    switch (video->planes) {
        case 1: case 2: case 4: case 8:
            *format = SDL_PIXELFORMAT_INDEX8;
            break;
        case 16:
            *format = SDL_PIXELFORMAT_RGB565;
            break;
        case 24:
            *format = SDL_PIXELFORMAT_RGB888;
            break;
        case 32:
            *format = SDL_PIXELFORMAT_ARGB8888;
            break;
    }

    data->buffer = SDL_malloc(chunky_size);
    if (!data->buffer) {
        return SDL_OutOfMemory();
    }
    /* SDL_memset(data->buffer, 0, chunky_size); */
    /* Buffer intentionally left uninitialized for performance. */
    
    data->planar_buffer = SDL_malloc(planar_size);
    if (!data->planar_buffer) {
        SDL_free(data->buffer);
        data->buffer = NULL;
        return SDL_OutOfMemory();
    }

    /* SDL_memset(data->planar_buffer, 0, planar_size); */
    
    data->planar_mfdb.fd_addr = data->planar_buffer;
    data->planar_mfdb.fd_w = w;
    data->planar_mfdb.fd_h = h;
    data->planar_mfdb.fd_wdwidth = (w + 15) / 16;
    data->planar_mfdb.fd_stand = 0;
    data->planar_mfdb.fd_nplanes = video->planes;
    data->planar_mfdb.fd_r1 = 0;
    data->planar_mfdb.fd_r2 = 0;
    data->planar_mfdb.fd_r3 = 0;
    
    data->buffer_pitch = *pitch;
    *pixels = data->buffer;
    
    /* Initialize size tracking */
    data->last_w = w;
    data->last_h = h;    
    return 0;
}

int GEM_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
                               const SDL_Rect *rects, int numrects)
{
    SDL_VideoData *video = (SDL_VideoData *)_this->driverdata;
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    int w, h;
    GRECT work;
    short vh;
    short todo[4];
    
    if (!data || !data->buffer || !data->planar_buffer) {
        return SDL_SetError("Framebuffer not initialized");
    }
    
    SDL_GetWindowSizeInPixels(window, &w, &h);

    // /* Handle resize detection (same as before) */
    // if (w != data->last_w || h != data->last_h) {
    //     // ... your existing resize code ...
    // }

    vh = video->vdi_handle;
    mt_wind_get_grect(data->handle, WF_WORKXYWH, &work, sdl_global_aes);
    
    mt_graf_mouse(M_OFF, 0L, sdl_global_aes);
    mt_wind_update(BEG_UPDATE, sdl_global_aes);
    
    /* Walk GEM rectangles */
    mt_wind_get(data->handle, WF_FIRSTXYWH, &todo[0], &todo[1], &todo[2], &todo[3], sdl_global_aes);
    
    while (todo[2] && todo[3]) {
        GRECT gem_rect = { todo[0], todo[1], todo[2], todo[3] };
        
        /* Clip to work area */
        if (rc_intersect(&work, &gem_rect)) {
            GRECT blit_rect;
            /* For each SDL dirty rect, check if it intersects with this GEM rect */
            for (int i = 0; i < numrects; i++) {
                SDL_Rect sdl_rect = rects[i];
                GRECT sdl_grect;
                
                /* Convert SDL rect to screen coordinates */
                sdl_grect.g_x = work.g_x + sdl_rect.x;
                sdl_grect.g_y = work.g_y + sdl_rect.y;
                sdl_grect.g_w = sdl_rect.w;
                sdl_grect.g_h = sdl_rect.h;
                
                /* Find intersection of SDL rect and GEM visible rect */
                blit_rect = gem_rect;
                if (rc_intersect(&sdl_grect, &blit_rect)) {
                    short pxy[8];
                    /* Convert the intersection area to planar format */
                    int src_x = blit_rect.g_x - work.g_x;
                    int src_y = blit_rect.g_y - work.g_y;
                    
                    /* Calculate source and destination pointers for partial conversion */
                    void *src_ptr = (char*)data->buffer + 
                                   (src_y * data->buffer_pitch) + 
                                   (src_x * (video->planes >> 3));
                    
                    void *dst_ptr = (char*)data->planar_buffer + 
                                   (src_y * data->planar_mfdb.fd_wdwidth * 2 * video->planes) +
                                   (src_x * (video->planes >> 3));
                    
                    /* Convert only the required rectangle */
                    switch (video->planes) {
                        case 1: case 2: case 4:
                            Atari_C2P_8to4(src_ptr, dst_ptr,
                                         blit_rect.g_w, blit_rect.g_h,
                                         data->buffer_pitch, 
                                         data->planar_mfdb.fd_wdwidth * 2);
                            break;
                        case 8:
                            Atari_C2P_8to8(src_ptr, dst_ptr,
                                         blit_rect.g_w, blit_rect.g_h,
                                         data->buffer_pitch,
                                         data->planar_mfdb.fd_wdwidth * 2);
                            break;
                        case 16: case 24: case 32: {
                            int row_bytes = blit_rect.g_w * (video->planes >> 3);
                            Atari_BlitFast(dst_ptr, src_ptr,
                                         row_bytes, blit_rect.g_h, 
                                         data->buffer_pitch);
                            break;
                        }
                    }
                    
                    /* Now blit to screen */
                    pxy[0] = src_x;
                    pxy[1] = src_y;
                    pxy[2] = pxy[0] + blit_rect.g_w - 1;
                    pxy[3] = pxy[1] + blit_rect.g_h - 1;
                    
                    pxy[4] = blit_rect.g_x;
                    pxy[5] = blit_rect.g_y;
                    pxy[6] = blit_rect.g_x + blit_rect.g_w - 1;
                    pxy[7] = blit_rect.g_y + blit_rect.g_h - 1;
                    
                    vro_cpyfm(vh, S_ONLY, pxy, &data->planar_mfdb, NULL);
                    
                }
            }
        }
        
        mt_wind_get(data->handle, WF_NEXTXYWH, &todo[0], &todo[1], &todo[2], &todo[3], sdl_global_aes);
    }
     
    mt_wind_update(END_UPDATE, sdl_global_aes);
    mt_graf_mouse(M_ON, 0L, sdl_global_aes);
    
    data->in_gem_redraw = SDL_FALSE;
    
    return 0;
}

void GEM_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_DestroyWindowFramebuffer called");
    if (!data) {
        return;
    }
    
    if (data->buffer) {
        SDL_free(data->buffer);
        data->buffer = NULL;
    }
    
    if (data->planar_buffer) {
        SDL_free(data->planar_buffer);
        data->planar_buffer = NULL;
    }
    
    SDL_memset(&data->planar_mfdb, 0, sizeof(MFDB));
}

/* ==== Window management functions ==== */

void GEM_SetWindowPosition(_THIS, SDL_Window *window)
{
    SDL_WindowData  *windata = (SDL_WindowData *)window->driverdata;
    SDL_VideoData   *viddata = (SDL_VideoData *)_this->driverdata;
    GRECT new_pos;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_SetWindowPosition called");

    if (!windata || windata->handle < 0) return;
    
    /* Validate coordinates */
    new_pos.g_x = SDL_clamp(window->x, viddata->work_x, viddata->work_x + viddata->work_w - windata->work_w);
    new_pos.g_y = SDL_clamp(window->y, viddata->work_y, viddata->work_y + viddata->work_h - windata->work_h);
    new_pos.g_w = windata->work_w;
    new_pos.g_h = windata->work_h;
    
    /* Perform the move */
    mt_wind_set(windata->handle, WF_CURRXYWH, 
             new_pos.g_x, new_pos.g_y, new_pos.g_w, new_pos.g_h, sdl_global_aes);

    /* Update SDL_WindowData's internal state */
    windata->work_x = new_pos.g_x;
    windata->work_y = new_pos.g_y;
    windata->work_w = new_pos.g_w;
    windata->work_h = new_pos.g_h;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_SetWindowPosition: work area set to %d,%d %dx%d",
        windata->work_x, windata->work_y, windata->work_w, windata->work_h);
    mt_wind_calc(WC_BORDER, windata->win_type, 
            windata->work_x, windata->work_y, windata->work_w, windata->work_h,
            &windata->win_x, &windata->win_y, &windata->win_w, &windata->win_h, sdl_global_aes);
}

void GEM_ShowWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    GRECT curr;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_ShowWindow called");
    if (data && data->handle >= 0) {
        mt_wind_get_grect(data->handle, WF_CURRXYWH, &curr, sdl_global_aes);
        mt_wind_open(data->handle, curr.g_x, curr.g_y, curr.g_w, curr.g_h, sdl_global_aes);
    }
}

void GEM_HideWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_HideWindow called");
    if (data && data->handle >= 0) {
        mt_wind_close(data->handle, sdl_global_aes);
    }
}

void GEM_RaiseWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_RaiseWindow called");
    if (data && data->handle >= 0) {
        mt_wind_set(data->handle, WF_TOP, 0, 0, 0, 0, sdl_global_aes);
    }
}

void GEM_MaximizeWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    GRECT full; GRECT work;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_MaximizeWindow called");
    if (!data || data->handle < 0) return;
    
    /* Save current position for restore */
    mt_wind_get_grect(data->handle, WF_CURRXYWH, &data->restore_rect, sdl_global_aes);
    
    /* Get and apply full size */
    mt_wind_get_grect(data->handle, WF_FULLXYWH, &full, sdl_global_aes);
    mt_wind_set(data->handle, WF_CURRXYWH, full.g_x, full.g_y, full.g_w, full.g_h, sdl_global_aes);
    mt_wind_get_grect(data->handle, WF_WORKXYWH, &work, sdl_global_aes);

    data->is_maximized = SDL_TRUE;
    
    /* Update SDL's internal state (no events - SDL sends them) */
    // window->x = full.g_x;
    // window->y = full.g_y;
    // window->w = full.g_w;
    // window->h = full.g_h;

    /* Update SDL with WORK AREA (client) dimensions, not border-inclusive */
    window->x = work.g_x;
    window->y = work.g_y;
    window->w = work.g_w;
    window->h = work.g_h;

    window->surface_valid = SDL_FALSE;
    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_SIZE_CHANGED, work.g_w, work.g_h);    
}

void GEM_MinimizeWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_MinimizeWindow called");
    if (data && data->handle >= 0) {
        mt_wind_set(data->handle, WF_ICONIFY, 0, 0, 0, 0, sdl_global_aes);
    }
}

void GEM_RestoreWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_RestoreWindow called");
    if (data && data->handle >= 0) {
        /* Restore from saved position */
        mt_wind_set(data->handle, WF_CURRXYWH,
                 data->restore_rect.g_x, data->restore_rect.g_y,
                 data->restore_rect.g_w, data->restore_rect.g_h, sdl_global_aes);
        
        data->is_maximized = SDL_FALSE;
        
        /* Update SDL's internal state */
        window->x = data->restore_rect.g_x;
        window->y = data->restore_rect.g_y;
        window->w = data->restore_rect.g_w;
        window->h = data->restore_rect.g_h;
        window->surface_valid = SDL_FALSE;
        SDL_SendWindowEvent(window, SDL_WINDOWEVENT_SIZE_CHANGED, window->w, window->h);
    }
}

void GEM_SetWindowBordered(_THIS, SDL_Window *window, SDL_bool bordered)
{
    /* Not implemented - would need to recreate window */
}

void GEM_SetWindowResizable(_THIS, SDL_Window *window, SDL_bool resizable)
{
    /* Not implemented - would need to recreate window */
}

void GEM_SetWindowSize(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    GRECT curr;
    int w, h;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM_SetWindowSize called");
    if (!data || data->handle < 0) return;
    
    SDL_GetWindowSize(window, &w, &h);
    mt_wind_get_grect(data->handle, WF_CURRXYWH, &curr, sdl_global_aes);
    mt_wind_set(data->handle, WF_CURRXYWH, curr.g_x, curr.g_y, w, h, sdl_global_aes);
}

void GEM_SetWindowMinimumSize(_THIS, SDL_Window *window)
{
    /* Not implemented */
}

void GEM_SetWindowMaximumSize(_THIS, SDL_Window *window)
{
    /* Not implemented */
}

#endif /* SDL_VIDEO_DRIVER_GEM */