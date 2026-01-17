/* ============================================
   FILE: src/video/gem/SDL_gemevents.c
   GEM event handling - FINAL PRODUCTION VERSION
   ============================================ */

#include "../../SDL_internal.h"
#include "SDL_gemvideo.h"
#include "../ataricommon/SDL_atarikeys.h"

#ifdef SDL_VIDEO_DRIVER_GEM

/* GEM event variables */
static short mx = 0, my = 0;
static short last_mb = 0;
static short mb = 0;
static short mc = 0;
static short kstate, key_state;
static short msg[8];
static short gem_events;
static SDL_Window *window;
static SDL_WindowData *win_data;

/* Keyboard state tracking - ISO C90 compliant */
static unsigned char key_state_map[128] = {0};
static unsigned char key_frame_count[128] = {0};  /* Frames since last GEM report */
static unsigned char any_keybd_this_frame = 0;
static Uint16 last_mod_state = 0;

void GEM_InitEvents(_THIS)
{
    int i;
    for (i = 0; i < 128; i++) {
        key_state_map[i] = 0;
        key_frame_count[i] = 0;
    }
    any_keybd_this_frame = 0;
    last_mod_state = 0;
}

void GEM_QuitEvents(_THIS)
{
    /* Nothing to clean up */
}

/* Helper: Check if scan code is a modifier */
static int IsModifierKey(int scan)
{
    return (scan == 0x2A || scan == 0x36 ||  /* LSHIFT, RSHIFT */
            scan == 0x1D ||                   /* LCTRL */
            scan == 0x38);                    /* LALT */
}

void GEM_PumpEvents(_THIS)
{
    int scan;
    Uint8 atari_scan;
    SDL_Scancode scancode;
    char ascii_char[2];
    Uint16 current_mod_state;
    Uint16 mod_changed;
    int x, y;
    short i;
    short curbutton, prevbutton;
    
    /* Reset frame tracking */
    any_keybd_this_frame = 0;
    
    /* Poll GEM events - EXACTLY your working pattern */
    gem_events = mt_evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER,
               0x101, 3, (~mb) & 3,
               0, 0, 0, 0, 0,
               0, 0, 0, 0, 0,
               msg, 0L,
               &mx, &my, &mb, &kstate, &key_state, &mc, sdl_global_aes);

    if (!gem_events) {
        return;
    }

    /* ==== Handle keyboard input ==== */
    if (gem_events & MU_KEYBD) {
        atari_scan = (Uint8)((key_state >> 8) & 0xFF);
        scancode = ATARI_MapScancode((int)atari_scan);
        
        if (scancode != SDL_SCANCODE_UNKNOWN) {
            /* Mark that we got keyboard activity */
            any_keybd_this_frame = 1;
            
            /* Reset frame counter for this key */
            key_frame_count[atari_scan] = 0;
            
            /* If key wasn't pressed, send PRESSED */
            if (!key_state_map[atari_scan]) {
                key_state_map[atari_scan] = 1;
                
                /* Only send non-modifiers here - modifiers handled by Kbshift() */
                if (!IsModifierKey(atari_scan)) {
                    SDL_SendKeyboardKey(SDL_PRESSED, scancode);
                    
                    /* Handle text input */
                    ascii_char[0] = (char)(key_state & 0xFF);
                    ascii_char[1] = '\0';
                    if (ascii_char[0] >= 32 && ascii_char[0] <= 126) {
                        SDL_SendKeyboardText(ascii_char);
                    }
                }
            }
        }
    }
    
    /* ==== Handle key releases - THE CRITICAL FIX ==== */
    /* Only release keys if NO keyboard events this frame */
    if (!any_keybd_this_frame) {
        for (scan = 0x02; scan <= 0x53; scan++) {
            if (key_state_map[scan]) {
                key_frame_count[scan]++;
                
                /* Release after ~30-40ms of no GEM reports (3-4 frames) */
                /* This catches releases without being too slow */
                if (key_frame_count[scan] > 3) {
                    scancode = ATARI_MapScancode(scan);
                    SDL_SendKeyboardKey(SDL_RELEASED, scancode);
                    key_state_map[scan] = 0;
                    key_frame_count[scan] = 0;
                }
            }
        }
    } else {
        /* GEM reported something - don't increment counters */
        /* Keys not in the MU_KEYBD event will be released next frame */
        for (scan = 0x02; scan <= 0x53; scan++) {
            if (key_state_map[scan] && key_frame_count[scan] > 0) {
                /* This key was missing from the MU_KEYBD event = released */
                scancode = ATARI_MapScancode(scan);
                SDL_SendKeyboardKey(SDL_RELEASED, scancode);
                key_state_map[scan] = 0;
                key_frame_count[scan] = 0;
            }
        }
    }
    
    /* ==== Handle modifier state changes (Kbshift() is GEM-safe) ==== */
    current_mod_state = ATARI_ModState();
    mod_changed = last_mod_state ^ current_mod_state;
    
    if (mod_changed) {
        if (mod_changed & KMOD_LSHIFT) {
            SDL_SendKeyboardKey((current_mod_state & KMOD_LSHIFT) ? 
                               SDL_PRESSED : SDL_RELEASED, SDL_SCANCODE_LSHIFT);
        }
        if (mod_changed & KMOD_RSHIFT) {
            SDL_SendKeyboardKey((current_mod_state & KMOD_RSHIFT) ? 
                               SDL_PRESSED : SDL_RELEASED, SDL_SCANCODE_RSHIFT);
        }
        if (mod_changed & KMOD_CTRL) {
            SDL_SendKeyboardKey((current_mod_state & KMOD_CTRL) ? 
                               SDL_PRESSED : SDL_RELEASED, SDL_SCANCODE_LCTRL);
        }
        if (mod_changed & KMOD_ALT) {
            SDL_SendKeyboardKey((current_mod_state & KMOD_ALT) ? 
                               SDL_PRESSED : SDL_RELEASED, SDL_SCANCODE_LALT);
        }
    }
    last_mod_state = current_mod_state;

    /* ==== Handle GEM messages ==== */
    /* ... rest of your working code unchanged ... */
    if (gem_events & MU_MESAG) {
        for (window = _this->windows; window != NULL; window = window->next) {
            win_data = (SDL_WindowData *)window->driverdata;
            if (win_data->handle == msg[3]) {
                break;
            }
        }
        if (!window) {
            SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM event for invalid window ID %d", msg[3]);
            return;
        }
        /* ... switch(msg[0]) unchanged ... */
        switch (msg[0]) {
            case WM_FULLED:
                if (win_data && msg[3] == win_data->handle){
                    win_data->is_maximized = !win_data->is_maximized;
                    if (win_data->is_maximized) {
                        SDL_MaximizeWindow(window);
                    } else {
                        SDL_RestoreWindow(window);
                    }
                }
                break;
            case WM_ICONIFY:
                if (win_data && msg[3] == win_data->handle) SDL_MinimizeWindow(window);
                break;
            case WM_UNICONIFY:
                if (win_data && msg[3] == win_data->handle) SDL_RestoreWindow(window);
                break;
            case WM_CLOSED:
                if (win_data && msg[3] == win_data->handle) SDL_SendWindowEvent(window, SDL_WINDOWEVENT_CLOSE, 0, 0);
                break;
            case WM_MOVED:
                if (win_data && msg[3] == win_data->handle){
                    SDL_SetWindowPosition(window, msg[4], msg[5]);
                    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_MOVED, msg[4], msg[5]);
                }
                break;
            case WM_SIZED:
                if (win_data && msg[3] == win_data->handle){
                    SDL_SetWindowSize(window, msg[6], msg[7]);
                    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_SIZE_CHANGED, msg[6], msg[7]);
                }
                break;
            case WM_TOPPED:
                if (win_data && msg[3] == win_data->handle) {
                    mt_wind_set(msg[3], WF_TOP, 0, 0, 0, 0, sdl_global_aes);
                    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_FOCUS_GAINED, 0, 0);
                }
                break;
            case WM_UNTOPPED:
                if (win_data && msg[3] == win_data->handle) {
                    SDL_SendWindowEvent(window, SDL_WINDOWEVENT_FOCUS_LOST, 0, 0);
                }
                break;
            case WM_REDRAW:
                if (win_data && msg[3] == win_data->handle) {
                    GRECT work;
                    SDL_Rect sdl_rect;
                    
                    mt_wind_get_grect(win_data->handle, WF_WORKXYWH, &work, sdl_global_aes);
                    
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

    /* ==== Handle mouse events ==== */
    if (gem_events & MU_BUTTON)
    {
        /* Find window by mouse coordinates, not msg[3] (which is garbage for MU_BUTTON) */
        for (window = _this->windows; window != NULL; window = window->next) {
            if (mx >= window->x && mx < window->x + window->w &&
                my >= window->y && my < window->y + window->h) {
                break;
            }
        }
        if (!window) {
            return;
        }
        x = mx - window->x;
        y = my - window->y;

        if (x >= 0 && x < window->w && y >= 0 && y < window->h) {
            SDL_SendMouseMotion(window, 0, 0, x, y);
        }
        
        // switch (mb)
        // {
        //     case 0:
        //         break;
        //     case 1:
        //         SDL_SendMouseButton(window, 0, SDL_PRESSED, SDL_BUTTON_LEFT);
        //         break;
        //     case 2:
        //         SDL_SendMouseButton(window, 0, SDL_PRESSED, SDL_BUTTON_RIGHT);
        //         break;
        // }

        if (mb != last_mb) {
            for (i = 0; i < 2; i++) {
                curbutton = mb & (1 << i);
                prevbutton = last_mb & (1 << i);
        
                if (curbutton && !prevbutton) {
                    SDL_SendMouseButton(window, 0, SDL_PRESSED, (i == 0) ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT);
                }
                if (!curbutton && prevbutton) {
                    SDL_SendMouseButton(window, 0, SDL_RELEASED, (i == 0) ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT);
                }
            }
            last_mb = mb;
        }
    }
}

#endif /* SDL_VIDEO_DRIVER_GEM */