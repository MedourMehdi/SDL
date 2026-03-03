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
   C90 compliant.

   Geometry commit rules
   ---------------------
   Every size change MUST go through SDL_SetWindowSize() so SDL runs
   SDL_OnWindowResized(): DestroyWindowFramebuffer + CreateWindowFramebuffer.
   Bypassing this leaves the framebuffer at the old size → bus error on 68k.

   GEM_SetWindowSize() is the single point that calls GEM_CommitBorderRect().
   It must NEVER early-exit based on data->work_w/h alone because callers
   like GEM_RestoreWindow pre-populate data->work_* with the target geometry
   before SDL_SetWindowSize() is called.  If the size happens to be equal
   (e.g. restoring to the same size as before maximize) but the POSITION
   changed, the early exit would silently skip the wind_set and the AES
   window would stay at the wrong position.

   Rule: GEM_SetWindowSize() always calls GEM_CommitBorderRect().
         The size-equality guard is removed.  Redundant AES calls on equal
         geometry are harmless; a missed call leaves the window wrong.

   Position-only changes (no size delta, no realloc) call
   GEM_CommitBorderRect() directly via GEM_SetWindowPosition().
   ============================================================================ */

#include "SDL_gemvideo.h"
#include "mt_gemx.h"

#ifdef SDL_VIDEO_DRIVER_GEM

/* ============================================================
   Internal helpers
   ============================================================ */

static void GEM_WorkFromBorder(SDL_WindowData *data)
{
    mt_wind_calc(WC_WORK, data->win_type,
                 data->win_x, data->win_y, data->win_w, data->win_h,
                 &data->work_x, &data->work_y,
                 &data->work_w, &data->work_h,
                 sdl_global_aes);
}

static void GEM_BorderFromWork(SDL_WindowData *data)
{
    mt_wind_calc(WC_BORDER, data->win_type,
                 data->work_x, data->work_y, data->work_w, data->work_h,
                 &data->win_x, &data->win_y, &data->win_w, &data->win_h,
                 sdl_global_aes);
}

/*
 * GEM_CommitBorderRect
 * Issues a single wind_set(WF_CURRXYWH) from data->win_*.
 * Never touches window->w/h — size changes must go through SDL_SetWindowSize.
 */
static void GEM_CommitBorderRect(SDL_WindowData *data)
{
    mt_wind_set(data->handle, WF_CURRXYWH,
                data->win_x, data->win_y, data->win_w, data->win_h,
                sdl_global_aes);
}

 /*
 * GEM_ApplyGeometry
 * Syncs AES border rect with current data->work_* and commits.
 */
static void GEM_ApplyGeometry(SDL_WindowData *data)
{
    GEM_BorderFromWork(data);
    GEM_CommitBorderRect(data);
}

/*
 * GEM_LoadRestoreRect
 * Populates data->win_* and data->work_* from a border rect.
 * Call before SDL_SetWindowSize so both position and size are correct
 * when GEM_SetWindowSize re-enters GEM_CommitBorderRect.
 */
static void GEM_LoadRestoreRect(SDL_WindowData *data, const GRECT *r)
{
    data->win_x = r->g_x;
    data->win_y = r->g_y;
    data->win_w = r->g_w;
    data->win_h = r->g_h;
    GEM_WorkFromBorder(data);
}

static short GEM_WinTypeFromFlags(const SDL_Window *window)
{
    short t;
    if (window->flags & SDL_WINDOW_BORDERLESS) {
        t = 0;
    } else {
        t = NAME | CLOSER | MOVER | ICONIFIER;
        if (window->flags & SDL_WINDOW_RESIZABLE)
            t |= SIZER | FULLER;
    }
    return t;
}

/* ============================================================
   GEM_ReopenWindow
   Creates new handle BEFORE closing old one (AES limit ~8 handles).
   On wind_create failure the old handle is still alive — leave it.
   ============================================================ */
static short GEM_ReopenWindow(SDL_WindowData *data, const SDL_Window *window,
                               short new_type,
                               short max_x, short max_y, short max_w, short max_h,
                               short bx, short by, short bw, short bh)
{
    short new_handle = mt_wind_create(new_type,
                                      max_x, max_y, max_w, max_h,
                                      sdl_global_aes);
    if (new_handle < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: wind_create failed (AES handle limit?)");
        return -1;
    }

    mt_wind_close(data->handle,  sdl_global_aes);
    mt_wind_delete(data->handle, sdl_global_aes);

    data->handle   = new_handle;
    data->win_type = new_type;
    data->win_x    = bx;
    data->win_y    = by;
    data->win_w    = bw;
    data->win_h    = bh;

    mt_wind_set_str(data->handle, WF_NAME,
                    window->title ? window->title : "SDL2", sdl_global_aes);
    mt_wind_open(data->handle, bx, by, bw, bh, sdl_global_aes);

    GEM_WorkFromBorder(data);
    return new_handle;
}

/* ============================================================
   GEM_SetWindowTitle
   ============================================================ */
void GEM_SetWindowTitle(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    if (data && data->handle >= 0)
        mt_wind_set_str(data->handle, WF_NAME,
                        window->title ? window->title : "SDL2",
                        sdl_global_aes);
}

/* ============================================================
   GEM_CreateWindow
   ============================================================ */
int GEM_CreateWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_VideoData  *vdata = (SDL_VideoData *)this->driverdata;
    SDL_WindowData *data;
    int x, y, w, h;

    data = (SDL_WindowData *)SDL_calloc(1, sizeof(SDL_WindowData));
    if (!data) return SDL_OutOfMemory();

    data->win_type = GEM_WinTypeFromFlags(window);

    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);

    if (x == SDL_WINDOWPOS_UNDEFINED) x = 50;
    if (y == SDL_WINDOWPOS_UNDEFINED) y = 50;
    if (x == SDL_WINDOWPOS_CENTERED)
        x = vdata->work_x + (vdata->work_w - w) / 2;
    if (y == SDL_WINDOWPOS_CENTERED)
        y = vdata->work_y + (vdata->work_h - h) / 2;

    if (window->flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        data->win_type = 0;
        data->win_x    = vdata->desk_x;
        data->win_y    = vdata->desk_y;
        data->win_w    = vdata->desk_w;
        data->win_h    = vdata->desk_h;
        data->work_x   = vdata->desk_x;
        data->work_y   = vdata->desk_y;
        data->work_w   = vdata->desk_w;
        data->work_h   = vdata->desk_h;
        data->state_flags |= GEM_STATE_FULLSCREEN;
    } else {
        data->work_x = (short)x;
        data->work_y = (short)y;
        data->work_w = (short)w;
        data->work_h = (short)h;
        GEM_BorderFromWork(data);
    }

    /* max rect = desktop work area so WF_FULLXYWH is never clamped. */
    data->handle = mt_wind_create(data->win_type,
                                  vdata->work_x, vdata->work_y,
                                  vdata->work_w, vdata->work_h,
                                  sdl_global_aes);
    if (data->handle < 0) {
        SDL_free(data);
        return SDL_SetError("wind_create failed");
    }

    if (!(data->state_flags & GEM_STATE_FULLSCREEN) &&
        (window->flags & SDL_WINDOW_MAXIMIZED)) {
        /* WF_FULLXYWH returns a BORDER rect — convert via WC_WORK. */
        GRECT full;
        data->restore_rect.g_x = data->win_x;
        data->restore_rect.g_y = data->win_y;
        data->restore_rect.g_w = data->win_w;
        data->restore_rect.g_h = data->win_h;
        mt_wind_get_grect(data->handle, WF_FULLXYWH, &full, sdl_global_aes);
        GEM_LoadRestoreRect(data, &full);
        data->state_flags |= GEM_STATE_MAXIMIZED;
    }

    mt_wind_set_str(data->handle, WF_NAME,
                    window->title ? window->title : "SDL2", sdl_global_aes);
    mt_wind_open(data->handle,
                 data->win_x, data->win_y,
                 data->win_w, data->win_h,
                 sdl_global_aes);

    window->driverdata = data;
    SDL_SetKeyboardFocus(window);
    return 0;
}

/* ============================================================
   GEM_DestroyWindow
   ============================================================ */
void GEM_DestroyWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    if (data) {
        if (data->handle >= 0) {
            mt_wind_close(data->handle,  sdl_global_aes);
            mt_wind_delete(data->handle, sdl_global_aes);
        }
        SDL_free(data);
        window->driverdata = NULL;
    }
}

/* ============================================================
   GEM_SetWindowPosition
   Position-only: no size change, no framebuffer realloc needed.
   Early-exit removed to keep win_* in sync after GEM_ReopenWindow.
   ============================================================ */
void GEM_SetWindowPosition(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;

    if (!data || data->handle < 0) return;

    data->work_x = (short)window->x;
    data->work_y = (short)window->y;

    GEM_ApplyGeometry(data);
}

/* ============================================================
   GEM_SetWindowSize
   Single point for all AES wind_set size commits.
   data->win_* & work_* must be fully pre-populated by the caller
   (via GEM_LoadRestoreRect or direct assignment) before SDL calls
   this, so the wind_set moves the window to the correct position
   AND size atomically.

   NO early-exit on equal size: the position may have changed even
   when w/h are unchanged (e.g. restore to same size at different
   position).  A redundant wind_set is harmless; a skipped one is not.
   ============================================================ */
void GEM_SetWindowSize(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;

    if (!data || data->handle < 0) return;

    data->work_w = (short)window->w;
    data->work_h = (short)window->h;

    GEM_ApplyGeometry(data);
}

/* ============================================================
   GEM_ShowWindow / GEM_HideWindow / GEM_RaiseWindow
   ============================================================ */
void GEM_ShowWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    if (data && data->handle >= 0)
        mt_wind_open(data->handle,
                     data->win_x, data->win_y,
                     data->win_w, data->win_h,
                     sdl_global_aes);
}

void GEM_HideWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    if (data && data->handle >= 0)
        mt_wind_close(data->handle, sdl_global_aes);
}

void GEM_RaiseWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;
    if (data && data->handle >= 0)
        mt_wind_set(data->handle, WF_TOP, 0, 0, 0, 0, sdl_global_aes);
}

/* ============================================================
   GEM_MaximizeWindow
   GEM_LoadRestoreRect pre-populates win_* & work_* from WF_FULLXYWH
   before SDL_SetWindowSize so GEM_SetWindowSize commits the correct
   full geometry (position + size) in one wind_set.
   ============================================================ */
void GEM_MaximizeWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    GRECT full;
    (void)this;

    if (!data || data->handle < 0) return;
    if (data->state_flags & GEM_STATE_MAXIMIZED) return;

    if (data->state_flags & GEM_STATE_ICONIFIED) {
        mt_wind_set(data->handle, WF_UNICONIFY,
                    data->restore_rect.g_x, data->restore_rect.g_y,
                    data->restore_rect.g_w, data->restore_rect.g_h,
                    sdl_global_aes);
        data->state_flags &= (Uint8)~GEM_STATE_ICONIFIED;
    } else if (!(data->state_flags & GEM_STATE_FULLSCREEN)) {
        mt_wind_get_grect(data->handle, WF_CURRXYWH,
                          &data->restore_rect, sdl_global_aes);
    }

    mt_wind_get_grect(data->handle, WF_FULLXYWH, &full, sdl_global_aes);
    data->state_flags &= (Uint8)~(GEM_STATE_ICONIFIED | GEM_STATE_FULLSCREEN);
    data->state_flags |= GEM_STATE_MAXIMIZED;

    GEM_LoadRestoreRect(data, &full);
    SDL_SetWindowSize(window, (int)data->work_w, (int)data->work_h);
    /* Send RESTORED first so SDL clears SDL_WINDOW_MAXIMIZED from
     * window->flags — otherwise a subsequent SDL_MaximizeWindow call
     * is swallowed by SDL's own guard before reaching the driver. */
    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESTORED, 0, 0);
    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_MAXIMIZED, 0, 0);
}

/* ============================================================
   GEM_MinimizeWindow
   NOTE: -1,-1,-1,-1 to WF_ICONIFY relies on AES auto-placement.
   Works on MagiC and XaAES; not guaranteed on bare TOS AES.
   ============================================================ */
void GEM_MinimizeWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;

    if (!data || data->handle < 0) return;
    if (data->state_flags & GEM_STATE_ICONIFIED) return;

    if (!(data->state_flags & (GEM_STATE_MAXIMIZED | GEM_STATE_FULLSCREEN)))
        mt_wind_get_grect(data->handle, WF_CURRXYWH,
                          &data->restore_rect, sdl_global_aes);

    data->state_flags &= (Uint8)~(GEM_STATE_MAXIMIZED | GEM_STATE_FULLSCREEN);
    mt_wind_set(data->handle, WF_ICONIFY, -1, -1, -1, -1, sdl_global_aes);
    data->state_flags |= GEM_STATE_ICONIFIED;

    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_MINIMIZED, 0, 0);
}

/* ============================================================
   GEM_RestoreWindow
   GEM_LoadRestoreRect pre-populates win_* & work_* (both position
   and size) before SDL_SetWindowSize so GEM_SetWindowSize commits
   the complete target geometry in one wind_set.
   ============================================================ */
void GEM_RestoreWindow(SDL_VideoDevice *this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    (void)this;

    if (!data || data->handle < 0) return;
    if (!(data->state_flags & (GEM_STATE_ICONIFIED | GEM_STATE_MAXIMIZED))) return;

    if (data->state_flags & GEM_STATE_ICONIFIED) {
        mt_wind_set(data->handle, WF_UNICONIFY,
                    data->restore_rect.g_x, data->restore_rect.g_y,
                    data->restore_rect.g_w, data->restore_rect.g_h,
                    sdl_global_aes);
        data->state_flags &= (Uint8)~GEM_STATE_ICONIFIED;
    } else {
        data->state_flags &= (Uint8)~GEM_STATE_MAXIMIZED;
    }

    GEM_LoadRestoreRect(data, &data->restore_rect);
    SDL_SetWindowSize(window, (int)data->work_w, (int)data->work_h);
    /* Send RESTORED so SDL clears SDL_WINDOW_MAXIMIZED/MINIMIZED from
     * window->flags, keeping SDL's state in sync with ours. */
    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESTORED, 0, 0);
}

/* ============================================================
   GEM_SetWindowFullscreen
   Both enter and exit paths call SDL_SetWindowSize after
   GEM_LoadRestoreRect / GEM_ReopenWindow so the framebuffer is
   always reallocated to the correct final size.
   ============================================================ */
void GEM_SetWindowFullscreen(SDL_VideoDevice *this, SDL_Window *window,
                              SDL_VideoDisplay *display, SDL_bool fullscreen)
{
    SDL_VideoData  *vdata = (SDL_VideoData *)this->driverdata;
    SDL_WindowData *data  = (SDL_WindowData *)window->driverdata;
    (void)display;

    if (!data || data->handle < 0) return;

    if (fullscreen) {
        if (data->state_flags & GEM_STATE_FULLSCREEN) return;

        /* Stash maximized flag BEFORE clearing state so the exit path
         * can detect it correctly when fullscreen is later toggled off. */
        data->state_flags &= (Uint8)~GEM_STATE_WAS_MAXIMIZED;
        if (data->state_flags & GEM_STATE_MAXIMIZED) data->state_flags |= GEM_STATE_WAS_MAXIMIZED;

        if (!(data->state_flags & (GEM_STATE_MAXIMIZED | GEM_STATE_ICONIFIED)))
            mt_wind_get_grect(data->handle, WF_CURRXYWH,
                              &data->restore_rect, sdl_global_aes);

        data->state_flags &= (Uint8)~(GEM_STATE_ICONIFIED | GEM_STATE_MAXIMIZED);

        if (GEM_ReopenWindow(data, window, 0,
                             vdata->desk_x, vdata->desk_y,
                             vdata->desk_w, vdata->desk_h,
                             vdata->desk_x, vdata->desk_y,
                             vdata->desk_w, vdata->desk_h) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                         "GEM_SetWindowFullscreen: failed to recreate window");
            return;
        }

        data->state_flags |= (Uint8)GEM_STATE_FULLSCREEN;
        /* GEM_ReopenWindow already populated data->win_* & work_*. */
        SDL_SetWindowSize(window, (int)data->work_w, (int)data->work_h);

    } else {
        short    restore_type;
        short    bx, by, bw, bh;
        GRECT    saved_restore;

        /* Capture whether we need to return to maximized before clearing flags */
        SDL_bool was_maximized = (data->state_flags & GEM_STATE_WAS_MAXIMIZED)
                                  ? SDL_TRUE : SDL_FALSE;

        if (!(data->state_flags & GEM_STATE_FULLSCREEN)) return;

        saved_restore = data->restore_rect;

        restore_type = GEM_WinTypeFromFlags(window);

        if (was_maximized) {
            mt_wind_calc(WC_BORDER, restore_type,
                         vdata->work_x, vdata->work_y,
                         vdata->work_w, vdata->work_h,
                         &bx, &by, &bw, &bh, sdl_global_aes);
        } else {
            bx = saved_restore.g_x;
            by = saved_restore.g_y;
            bw = saved_restore.g_w;
            bh = saved_restore.g_h;
        }

        if (GEM_ReopenWindow(data, window, restore_type,
                             vdata->work_x, vdata->work_y,
                             vdata->work_w, vdata->work_h,
                             bx, by, bw, bh) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                         "GEM_SetWindowFullscreen: failed to recreate window");
            return;
        }

        data->state_flags &= (Uint8)~(GEM_STATE_FULLSCREEN | GEM_STATE_ICONIFIED | GEM_STATE_WAS_MAXIMIZED);

        if (was_maximized) {
            /* Reopen already set work area; just update flags and notify SDL */
            data->state_flags |= (Uint8)GEM_STATE_MAXIMIZED;
            SDL_SetWindowSize(window, (int)data->work_w, (int)data->work_h);
            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_MAXIMIZED, 0, 0);
        } else {
            SDL_SetWindowSize(window, (int)data->work_w, (int)data->work_h);
        }
    }
}

/* ============================================================
   GEM_SetWindowBordered / GEM_SetWindowResizable
   ============================================================ */
void GEM_SetWindowBordered(SDL_VideoDevice *this, SDL_Window *window,
                            SDL_bool bordered)
{
    SDL_VideoData  *vdata = (SDL_VideoData *)this->driverdata;
    SDL_WindowData *data  = (SDL_WindowData *)window->driverdata;
    short new_type;
    short bx, by, bw, bh;

    if (!data || data->handle < 0) return;

    new_type = bordered ? GEM_WinTypeFromFlags(window) : 0;
    if (new_type == data->win_type) return;

    mt_wind_calc(WC_BORDER, new_type,
                 data->work_x, data->work_y, data->work_w, data->work_h,
                 &bx, &by, &bw, &bh, sdl_global_aes);

    GEM_ReopenWindow(data, window, new_type,
                     vdata->work_x, vdata->work_y,
                     vdata->work_w, vdata->work_h,
                     bx, by, bw, bh);
}

void GEM_SetWindowResizable(SDL_VideoDevice *this, SDL_Window *window,
                             SDL_bool resizable)
{
    SDL_VideoData  *vdata = (SDL_VideoData *)this->driverdata;
    SDL_WindowData *data  = (SDL_WindowData *)window->driverdata;
    short new_type;
    short bx, by, bw, bh;

    if (!data || data->handle < 0) return;
    if (window->flags & SDL_WINDOW_BORDERLESS) return;

    new_type = data->win_type;
    if (resizable) new_type |=  (short)(SIZER | FULLER);
    else           new_type &= (short)~(SIZER | FULLER);
    if (new_type == data->win_type) return;

    mt_wind_calc(WC_BORDER, new_type,
                 data->work_x, data->work_y, data->work_w, data->work_h,
                 &bx, &by, &bw, &bh, sdl_global_aes);

    GEM_ReopenWindow(data, window, new_type,
                     vdata->work_x, vdata->work_y,
                     vdata->work_w, vdata->work_h,
                     bx, by, bw, bh);
}

/* ============================================================
   No-ops — constraints enforced in WM_SIZED handler
   ============================================================ */
void GEM_SetWindowMinimumSize(SDL_VideoDevice *this, SDL_Window *window)
{ (void)this; (void)window; }

void GEM_SetWindowMaximumSize(SDL_VideoDevice *this, SDL_Window *window)
{ (void)this; (void)window; }

int GEM_GetWindowDisplayIndex(SDL_VideoDevice *this, SDL_Window *window)
{ (void)this; (void)window; return 0; }

#endif /* SDL_VIDEO_DRIVER_GEM */