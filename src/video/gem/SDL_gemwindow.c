#include "SDL_gemwindow.h"
#include "SDL_gemvideo.h"
#include <mint/sysbind.h>
#include <gem.h>

#include "SDL_render.h"
// #include "../../render/software/SDL_render_sw_c.h"

int GEM_CreateWindow(_THIS, SDL_Window *window)
{
    struct SDL_VideoData *video = (struct SDL_VideoData *)_this->driverdata;
    SDL_WindowData *data;
    short wx, wy;
    int window_attr;
    GRECT work, ext;

    printf("DEBUG: GEM_CreateWindow called for window ID %d\n", window->id);

    /* Allocate window data */
    data = (SDL_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }

    window->driverdata = data;

    /* Set up window attributes */
    window_attr = NAME | MOVER;
    if (!(window->flags & SDL_WINDOW_BORDERLESS)) {
        window_attr |= CLOSER | FULLER | SIZER;
    }

    /* Calculate window position */
    wx = (window->x < 0) ? video->work_x : window->x;
    wy = (window->y < 0) ? video->work_y : window->y;
    
    /* Set desired work area */
    work.g_x = wx;
    work.g_y = wy;
    work.g_w = window->w;
    work.g_h = window->h;

    /* Create GEM window */
    wind_calc(WC_BORDER, window_attr, work.g_x, work.g_y, work.g_w, work.g_h,
             &ext.g_x, &ext.g_y, &ext.g_w, &ext.g_h);

    data->handle = wind_create(window_attr, ext.g_x, ext.g_y, ext.g_w, ext.g_h);
    if (data->handle < 0) {
        SDL_free(data);
        return SDL_SetError("Could not create GEM window");
    }
    wind_set_str(data->handle, WF_NAME, window->title);

    /* Create framebuffer immediately */
    // Determine pixel format and calculate pitch
    switch (video->planes) {
        case 24:  // 24bpp
            data->buffer = SDL_malloc(window->h * (window->w * 3));
            break;
        case 32: // 32bpp
            data->buffer = SDL_malloc(window->h * (window->w * 4));
            break;
        case 16: // 16bpp
        default:
            data->buffer = SDL_malloc(window->h * ((window->w * 16 + 31) & ~31) / 8);
            break;
    }    
    // data->buffer = SDL_malloc(window->h * ((window->w * 16 + 31) & ~31) / 8);
    if (!data->buffer) {
        wind_delete(data->handle);
        SDL_free(data);
        return SDL_OutOfMemory();
    }

    /* Store window data */
    wind_get_grect(data->handle, WF_WORKXYWH, &work);
    data->work_x = work.g_x;
    data->work_y = work.g_y;
    data->work_w = work.g_w;
    data->work_h = work.g_h;

    /* Open the window */
    wind_open(data->handle, ext.g_x, ext.g_y, ext.g_w, ext.g_h);

    return 0;
}

int GEM_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format,
    void **pixels, int *pitch)
{
    struct SDL_VideoData *video = (struct SDL_VideoData *)_this->driverdata;
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;

    if (!data) {
        return SDL_SetError("Window data not found");
    }

    // Determine pixel format and calculate pitch
    switch (video->planes) {
        case 24:  // 24bpp
            *format = SDL_PIXELFORMAT_RGB888;
            *pitch = window->w * 3; // 3 bytes per pixel
            data->buffer = SDL_malloc(window->h * (*pitch));
            break;
        case 32: // 32bpp
            *format = SDL_PIXELFORMAT_ARGB8888;
            *pitch = window->w * 4; // 4 bytes per pixel
            data->buffer = SDL_malloc(window->h * (*pitch));
            break;
        case 16: // 16bpp
        default:
            *format = SDL_PIXELFORMAT_RGB565;
            *pitch = ((window->w * 16 + 31) & ~31) / 8; // 2 bytes per pixel
            data->buffer = SDL_malloc(window->h * (*pitch));
            break;
    }
    // *format = SDL_PIXELFORMAT_RGB565;
    // *pitch = ((window->w * 16 + 31) & ~31) / 8;
    *pixels = data->buffer;

    return 0;
}

int GEM_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_WindowData *wind_data = (SDL_WindowData *)window->driverdata;
    struct SDL_VideoData *video = (struct SDL_VideoData *)_this->driverdata;
    MFDB src, dst = {0};
    short pxy[8];
    GRECT work;
    GRECT rect;

    printf("DEBUG: UpdateWindowFramebuffer called with %d rects\n", numrects);

    if (!wind_data || !wind_data->buffer) {
        return SDL_SetError("No window data or buffer");
    }

    /* Begin window update */
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);

    /* Get work area */
    wind_get_grect(wind_data->handle, WF_WORKXYWH, &work);

    /* Set up source MFDB */
    src.fd_addr = wind_data->buffer;
    src.fd_w = window->w;
    src.fd_h = window->h;
    src.fd_wdwidth = (window->w + 15) / 16;
    src.fd_stand = 0;
    src.fd_nplanes = video->planes;

    /* Get first rectangle to redraw */
    wind_get_grect(wind_data->handle, WF_FIRSTXYWH, &rect);

    while (rect.g_w && rect.g_h) {
        /* For each update rectangle */
        for (int i = 0; i < numrects; ++i) {
            GRECT update_rect;
            GRECT clip_rect;
            
            /* Convert SDL rect to GRECT */
            update_rect.g_x = work.g_x + rects[i].x;
            update_rect.g_y = work.g_y + rects[i].y;
            update_rect.g_w = rects[i].w;
            update_rect.g_h = rects[i].h;

            /* Calculate intersection with redraw rectangle */
            if (rc_intersect(&rect, &update_rect)) {
                /* Convert back to source coordinates */
                clip_rect.g_x = update_rect.g_x - work.g_x;
                clip_rect.g_y = update_rect.g_y - work.g_y;
                clip_rect.g_w = update_rect.g_w;
                clip_rect.g_h = update_rect.g_h;

                /* Ensure we don't exceed source buffer bounds */
                if (clip_rect.g_x < 0) {
                    clip_rect.g_w += clip_rect.g_x;
                    clip_rect.g_x = 0;
                }
                if (clip_rect.g_y < 0) {
                    clip_rect.g_h += clip_rect.g_y;
                    clip_rect.g_y = 0;
                }
                if (clip_rect.g_x + clip_rect.g_w > window->w) {
                    clip_rect.g_w = window->w - clip_rect.g_x;
                }
                if (clip_rect.g_y + clip_rect.g_h > window->h) {
                    clip_rect.g_h = window->h - clip_rect.g_y;
                }

                if (clip_rect.g_w > 0 && clip_rect.g_h > 0) {
                    /* Set up VDI coordinates */
                    pxy[0] = clip_rect.g_x;                    /* source x */
                    pxy[1] = clip_rect.g_y;                    /* source y */
                    pxy[2] = clip_rect.g_x + clip_rect.g_w - 1; /* source x2 */
                    pxy[3] = clip_rect.g_y + clip_rect.g_h - 1; /* source y2 */
                    pxy[4] = update_rect.g_x;                  /* dest x */
                    pxy[5] = update_rect.g_y;                  /* dest y */
                    pxy[6] = update_rect.g_x + update_rect.g_w - 1; /* dest x2 */
                    pxy[7] = update_rect.g_y + update_rect.g_h - 1; /* dest y2 */

                    /* Draw the clipped region */
                    vro_cpyfm(video->vdi_handle, S_ONLY, pxy, &src, &dst);
                }
            }
        }
        /* Get next rectangle */
        wind_get_grect(wind_data->handle, WF_NEXTXYWH, &rect);
    }

    /* End window update */
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);

    return 0;
}

void GEM_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    void *buffer = SDL_GetWindowData(window, "SDL_WindowFramebuffer");
    printf("DEBUG: GEM_DestroyWindowFramebuffer called for window %d\n", window->id);
    
    if (buffer) {
        printf("  Freeing framebuffer at %p\n", buffer);
        SDL_free(buffer);
        SDL_SetWindowData(window, "SDL_WindowFramebuffer", NULL);
    } else {
        printf("  No framebuffer found to destroy\n");
    }
}

void GEM_DestroyWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    
    if (!data) {
        printf("DEBUG: DestroyWindow called but no driver data exists\n");
        return;
    }
    
    printf("DEBUG: Destroying window with handle %d\n", data->handle);

    /* Close and delete GEM window */
    wind_close(data->handle);
    wind_delete(data->handle);

    /* Free window buffer if we created one */
    if (!data->fullscreen) {
        SDL_free(data->buffer);
    }

    SDL_free(data);
    window->driverdata = NULL;
}

void GEM_SetWindowPosition(_THIS, SDL_Window *window)
{
    SDL_WindowData *wind_data = (SDL_WindowData *)window->driverdata;
    GRECT curr;

    printf("\n--> GEM_SetWindowPosition Debug:\n");
    printf("Requested position: x=%d, y=%d\n", window->x, window->y);

    if (!wind_data || !wind_data->handle) {
        printf("Error: Invalid window data or handle\n");
        return;
    }

    /* Get current window area to preserve size */
    wind_get_grect(wind_data->handle, WF_CURRXYWH, &curr);
    printf("Current window: x=%d, y=%d, w=%d, h=%d\n",
           curr.g_x, curr.g_y, curr.g_w, curr.g_h);
    
    /* Update position while maintaining size */
    wind_set(wind_data->handle, WF_CURRXYWH, 
             window->x, window->y, 
             curr.g_w, curr.g_h);

    /* Verify the change */
    wind_get_grect(wind_data->handle, WF_CURRXYWH, &curr);
    printf("After move: x=%d, y=%d, w=%d, h=%d\n",
           curr.g_x, curr.g_y, curr.g_w, curr.g_h);

    /* Update work area */
    wind_get_grect(wind_data->handle, WF_WORKXYWH, 
                   (GRECT *)&wind_data->work_x);
    printf("New work area: x=%d, y=%d\n",
           wind_data->work_x, wind_data->work_y);
}

void GEM_ShowWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        GRECT rect;
        wind_get_grect(data->handle, WF_CURRXYWH, &rect);
        wind_open(data->handle, rect.g_x, rect.g_y, rect.g_w, rect.g_h);
    }
}

void GEM_HideWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        wind_close(data->handle);
    }
}

void GEM_RaiseWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        wind_set(data->handle, WF_TOP, 0, 0, 0, 0);
    }
}

void GEM_MaximizeWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        GRECT work, full;
        /* Get maximum work area */
        wind_get_grect(data->handle, WF_FULLXYWH, &full);
        /* Calculate work area from full size */
        wind_calc(WC_WORK, data->handle, 
                 full.g_x, full.g_y, full.g_w, full.g_h,
                 &work.g_x, &work.g_y, &work.g_w, &work.g_h);
        /* Set new size */
        wind_set(data->handle, WF_CURRXYWH, 
                work.g_x, work.g_y, work.g_w, work.g_h);
    }
}

void GEM_MinimizeWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        wind_set(data->handle, WF_ICONIFY, 0, 0, 0, 0);
    }
}

void GEM_RestoreWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        /* If iconified, restore */
        wind_set(data->handle, WF_UNICONIFY, 0, 0, 0, 0);
        /* Raise to top */
        wind_set(data->handle, WF_TOP, 0, 0, 0, 0);
    }
}

void GEM_SetWindowSize(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        GRECT curr;
        wind_get_grect(data->handle, WF_CURRXYWH, &curr);
        wind_set(data->handle, WF_CURRXYWH,
                curr.g_x, curr.g_y,
                window->w, window->h);
    }
}

void GEM_SetWindowBordered(_THIS, SDL_Window *window, SDL_bool bordered)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        int new_attr = NAME | MOVER;  /* Basic attributes */
        if (bordered) {
            new_attr |= CLOSER | FULLER | SIZER;
        }
        wind_set(data->handle, WF_NEWDESK, new_attr, 0, 0, 0);
    }
}

void GEM_SetWindowResizable(_THIS, SDL_Window *window, SDL_bool resizable)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        short int curr_attr;
        wind_get(data->handle, WF_NEWDESK, &curr_attr, 0, 0, 0);
        
        if (resizable) {
            curr_attr |= SIZER;
        } else {
            curr_attr &= ~SIZER;
        }
        
        wind_set(data->handle, WF_NEWDESK, curr_attr, 0, 0, 0);
    }
}

void GEM_SetWindowMinimumSize(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        wind_set(data->handle, WF_MINXYWH,
                0, 0,  /* Position doesn't matter for min size */
                window->min_w, window->min_h);
    }
}

void GEM_SetWindowMaximumSize(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    if (data && data->handle >= 0) {
        GRECT curr, full;
        
        /* Get full window size */
        wind_get_grect(data->handle, WF_FULLXYWH, &full);
        wind_get_grect(data->handle, WF_CURRXYWH, &curr);
        
        /* Ensure window doesn't exceed full size */
        if (curr.g_w > full.g_w || curr.g_h > full.g_h) {
            wind_set(data->handle, WF_CURRXYWH,
                    curr.g_x, curr.g_y,
                    SDL_min(curr.g_w, full.g_w),
                    SDL_min(curr.g_h, full.g_h));
        }
    }
}
