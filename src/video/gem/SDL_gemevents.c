/* ============================================
   FILE: src/video/gem/SDL_gemevents.c
   GEM event handling - FINAL PRODUCTION VERSION
   ============================================ */

#include "../../SDL_internal.h"
#include "SDL_gemvideo.h"
#include "../ataricommon/SDL_atarikeys.h"

#ifdef SDL_VIDEO_DRIVER_GEM

/* GEM event variables */
static short mx = 0, my = 0; /* Mouse coordinates */
static short last_mb = 0; /* Last mouse button clicked */
static short mb = 0; /* Mouse button clicked */
static short mc = 0; /* Mouse click count */
// static short ms = 0; /* Mouse button state tested by default */
static short kstate, key_state;
static short msg[8];
static short gem_events;
static SDL_Event event;
static SDL_Window *window;
static SDL_WindowData *win_data;

void GEM_InitEvents(_THIS)
{
    return;
}

void GEM_QuitEvents(_THIS)
{
    /* Nothing to clean up */
}

void GEM_PumpEvents(_THIS)
{
    gem_events = mt_evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_M1 | MU_TIMER,
                //    256 | 1, 3, ms,
                   0x101, 3, (~mb) & 3,
                   0, 0, 0, 0, 0,
                   0, 0, 0, 0, 0,
                   msg, 1L,
                   &mx, &my, &mb, &kstate, &key_state, &mc, sdl_global_aes);
    if (!gem_events) {
        return;
    }

    /* ==== Handle GEM messages ==== */
    if (gem_events & MU_MESAG) {
        for (window = _this->windows; window != NULL; window = window->next) {
            win_data = (SDL_WindowData *)window->driverdata;
            if (win_data->handle == msg[3]) {
                printf("Found matching window: id=%d, handle=%d, message %d\n", window->id, win_data->handle, msg[0]);
                break;
            }
        }
        if (!window) {
            SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM event for invalid window ID %d, aes handle %d, message %d", msg[3], msg[3], msg[0]);
            return;  /* Ignore zombie events */
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM event %d for window ID %d", msg[0], msg[3]);
        // win_data = (SDL_WindowData *)window->driverdata;
        // if (!win_data || win_data->handle != msg[3]) {
        //     return;  /* Stale handle */
        // }

        switch (msg[0]) {
            /* Fuller box clicked - let SDL handle state */
            case WM_FULLED:
                if (win_data && msg[3] == win_data->handle){
                    /* Toggle maximize state FIRST to prevent race condition */
                    win_data->is_maximized = !win_data->is_maximized;
                    if (win_data->is_maximized) {
                        SDL_MaximizeWindow(window); /* SDL calls GEM_MaximizeWindow */
                    } else {
                        SDL_RestoreWindow(window);  /* SDL calls GEM_RestoreWindow */
                    }
                    /* SDL sends MAXIMIZED/RESTORED events automatically - don't send here! */
                }
                break;
                    
            case WM_ICONIFY:
                if (win_data && msg[3] == win_data->handle) SDL_MinimizeWindow(window);
                /* SDL sends MINIMIZED event automatically */
                break;
                    
            case WM_UNICONIFY:
                if (win_data && msg[3] == win_data->handle) SDL_RestoreWindow(window);
                /* SDL sends RESTORED event automatically */
                break;
                    
            case WM_CLOSED:
                if (win_data && msg[3] == win_data->handle) SDL_SendWindowEvent(window, SDL_WINDOWEVENT_CLOSE, 0, 0);
                break;

            /* ==== RULE 1: External changes → Update SDL state + notify ==== */
            
            /* Window was moved externally by window manager */
            case WM_MOVED:
            if (win_data && msg[3] == win_data->handle){
                SDL_SetWindowPosition(window, msg[4], msg[5]);
                SDL_SendWindowEvent(window, SDL_WINDOWEVENT_MOVED, msg[4], msg[5]);
            }
            break;
                    
            /* Window was resized externally by window manager */
            case WM_SIZED:
                if (win_data && msg[3] == win_data->handle){
                    SDL_SetWindowSize(window, msg[6], msg[7]);
                    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_SIZE_CHANGED, msg[6], msg[7]);
                }
                break;
                    
            /* Window gained focus */
            case WM_TOPPED:
                if (win_data && msg[3] == win_data->handle) {
                    mt_wind_set(msg[3], WF_TOP, 0, 0, 0, 0, sdl_global_aes);
                    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_FOCUS_GAINED, 0, 0);  
                    // /* FIX: Force redraw when gaining focus */
                    // SDL_SendWindowEvent(window, SDL_WINDOWEVENT_EXPOSED, 0, 0);
                }
                break;
            case WM_UNTOPPED:
                if (win_data && msg[3] == win_data->handle) {
                    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_FOCUS_LOST, 0, 0);
                }
                break;
            /* Window needs redraw - NEVER call VDI functions here! */
            // case WM_REDRAW:
            //     if (win_data && msg[3] == win_data->handle) {
            //         /* Store the clipping rectangle provided by GEM msg[4-7] */
            //         win_data->gem_clip_rect.g_x = msg[4];
            //         win_data->gem_clip_rect.g_y = msg[5];
            //         win_data->gem_clip_rect.g_w = msg[6];
            //         win_data->gem_clip_rect.g_h = msg[7];
                    
            //         /* Mark this window as needing a GEM-style redraw */
            //         win_data->in_gem_redraw = SDL_TRUE;
            //         printf("WM_REDRAW: clip_rect=(%d,%d,%d,%d)\n", 
            //             msg[4], msg[5], msg[6], msg[7]);
            //         /* Tell SDL to call UpdateWindowFramebuffer */
            //         SDL_SendWindowEvent(window, SDL_WINDOWEVENT_EXPOSED, 0, 0);
            //     }
            //     break;
case WM_REDRAW:
    if (win_data && msg[3] == win_data->handle) {
        GRECT work;
        SDL_Rect sdl_rect;
        
        /* Get window work area */
        mt_wind_get_grect(win_data->handle, WF_WORKXYWH, &work, sdl_global_aes);
        
        /* Convert GEM screen coordinates to window-relative SDL coordinates */
        sdl_rect.x = msg[4] - work.g_x;
        sdl_rect.y = msg[5] - work.g_y;
        sdl_rect.w = msg[6];
        sdl_rect.h = msg[7];
        
        /* Clamp to window bounds */
        if (sdl_rect.x < 0) {
            sdl_rect.w += sdl_rect.x;
            sdl_rect.x = 0;
        }
        if (sdl_rect.y < 0) {
            sdl_rect.h += sdl_rect.y;
            sdl_rect.y = 0;
        }
        if (sdl_rect.x + sdl_rect.w > window->w) {
            sdl_rect.w = window->w - sdl_rect.x;
        }
        if (sdl_rect.y + sdl_rect.h > window->h) {
            sdl_rect.h = window->h - sdl_rect.y;
        }
        
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                    "WM_REDRAW: GEM rect=(%d,%d,%d,%d) -> SDL rect=(%d,%d,%d,%d)", 
                    msg[4], msg[5], msg[6], msg[7],
                    sdl_rect.x, sdl_rect.y, sdl_rect.w, sdl_rect.h);
        
        /* Store for use in UpdateWindowFramebuffer */
        win_data->gem_clip_rect.g_x = msg[4];
        win_data->gem_clip_rect.g_y = msg[5];
        win_data->gem_clip_rect.g_w = msg[6];
        win_data->gem_clip_rect.g_h = msg[7];
        win_data->in_gem_redraw = SDL_TRUE;
        
        /* Tell SDL to redraw this specific rectangle */
        if (window->surface && sdl_rect.w > 0 && sdl_rect.h > 0) {
            SDL_UpdateWindowSurfaceRects(window, &sdl_rect, 1);
        }
    }
    break;
            default:
                SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Unhandled GEM message %d for window ID %d", msg[0], msg[3]);
                break;
        }
    }

    if (gem_events & MU_BUTTON)
    {
        int x = mx - window->x;
        int y = my - window->y;
        if (x >= 0 && x < window->w && y >= 0 && y < window->h) {
            SDL_SendMouseMotion(window, 0, 0, x, y);
        }
        switch (mb)
        {
            case 0:
                break;
            case 1:
                SDL_SendMouseButton(window, 0, SDL_PRESSED, SDL_BUTTON_LEFT);
                break;
            case 2:
                SDL_SendMouseButton(window, 0, SDL_PRESSED, SDL_BUTTON_RIGHT);
                break;
            default:
                break;
        }

			if (mb != last_mb) {
				for (short i = 0; i < 2; i++) {
					short curbutton, prevbutton;

					curbutton = mb & (1 << i);
					prevbutton = last_mb & (1 << i);
			
					if (curbutton && !prevbutton) {
						printf("Mouse: button %d pressed\n", i);
                        SDL_SendMouseButton(window, 0, SDL_RELEASED, SDL_BUTTON_LEFT);
                        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Mouse: button %d pressed\n", i);
					}
					if (!curbutton && prevbutton) {
						printf("Mouse: button %d released\n", i);
                        SDL_SendMouseButton(window, 0, SDL_RELEASED, SDL_BUTTON_RIGHT);
                        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Mouse: button %d released\n", i);
					}
				}
				last_mb = mb;
			}

    // if (last_mb != mb)
    // {
    //     if (last_mb == 1)
    //     {
    //         SDL_SendMouseButton(window, 0, SDL_RELEASED, SDL_BUTTON_LEFT);
    //     }
    //     else if (last_mb == 2)
    //     {
    //         SDL_SendMouseButton(window, 0, SDL_RELEASED, SDL_BUTTON_RIGHT);
    //     }
        
    // }
}

    if(gem_events & MU_KEYBD){
        /* ==== Keyboard handling ==== */
        if (key_state) {
            Uint16 scancode = (key_state >> 8) & 0xFF;
            
            SDL_zero(event);
            event.key.keysym.scancode = ATARI_MapScancode(scancode);
            event.key.keysym.sym = ATARI_MapKey(scancode);
            event.key.keysym.mod = ATARI_ModState();
            event.type = (kstate & K_RSHIFT) ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.state = (kstate & K_RSHIFT) ? SDL_PRESSED : SDL_RELEASED;
            SDL_PushEvent(&event);
        }
    }
}

#endif /* SDL_VIDEO_DRIVER_GEM */