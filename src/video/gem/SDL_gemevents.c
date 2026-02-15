/* ============================================
   FILE: src/video/gem/SDL_gemevents.c
   GEM event handling - FINAL PRODUCTION VERSION
   ============================================ */

#include "../../SDL_internal.h"
#include "SDL_gemvideo.h"
#include "SDL_gemkeys.h"

#ifdef SDL_VIDEO_DRIVER_GEM

/* 
 * CRITICAL: 60 frames @ 60Hz = 1000ms (1 second)
 * Long enough for diagonal movement (UP+LEFT) to work,
 * short enough to not feel laggy on release
 */
#define KEY_RELEASE_TIMEOUT 60
#define TIMER_MS 17  /* 1000/60 = 16.67ms, rounded to 17 */

/* GEM event variables */
static short mx = 0, my = 0;
static short last_mx = -1, last_my = -1;
static short local_x, local_y;
static Uint8 last_button_state = 0;
static short mb = 0;
static short mc = 0;
static short kstate, key_state;
static short msg[8];
static SDL_Window *window;
static SDL_WindowData *win_data;
static short gem_events;

/* Keyboard state tracking */
static unsigned char key_state_map[128] = {0};
static unsigned char key_frame_count[128] = {0};  /* Frames since last GEM report */
static unsigned char any_keybd_this_frame = 0;
static Uint16 last_mod_state = 0;

static short scan;
static Uint8 atari_scan;
static SDL_Scancode scancode;
static char ascii_char[2];
static Uint16 current_mod_state;
static Uint16 mod_changed;

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
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "IsModifierKey called with scan %d", scan);
    return (scan == 0x2A || scan == 0x36 ||  /* LSHIFT, RSHIFT */
            scan == 0x1D ||                   /* LCTRL */
            scan == 0x38);                    /* LALT */
}

void GEM_PumpEvents(_THIS)
{
    /* Poll GEM events */
    gem_events = mt_evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER,
               0x101, 3, (~mb) & 3,
               0, 0, 0, 0, 0,
               0, 0, 0, 0, 0,
               msg, TIMER_MS,
               &mx, &my, &mb, &kstate, &key_state, &mc, sdl_global_aes);
               
    if(!gem_events) {
        return;
    }

    any_keybd_this_frame = 0;
    /* ==== Handle keyboard input ==== */
    if (gem_events & MU_KEYBD) {

        atari_scan = (Uint8)((key_state >> 8) & 0xFF);
        scancode = ATARI_MapScancode((int)atari_scan);
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM scan code %d, SDL scan code %d", atari_scan, scancode);

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
                    /* Send key down event */
                    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Sending key down: %d", scancode);
                    SDL_SendKeyboardKey(SDL_PRESSED, scancode);
                    
                    /* Handle text input */
                    ascii_char[0] = (char)(key_state & 0xFF);
                    ascii_char[1] = '\0';
                    if (ascii_char[0] >= 32 && ascii_char[0] <= 126) {
                        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Sending text: '%s'", ascii_char);
                        SDL_SendKeyboardText(ascii_char);
                    }
                }
            }
        }
    }
    
    /* Unified release logic: Age all pressed keys every frame */
    /* Notes: 
       1. If key was just pressed above, count was reset to 0. It becomes 1 here (safe).
       2. Threshold increased to 8 frames (~130ms) to bridge the gap in Atari auto-repeats.
    */
    for (scan = 0x02; scan <= 0x53; scan++) {
        if (key_state_map[scan]) {
            key_frame_count[scan]++;
            
            if (key_frame_count[scan] > KEY_RELEASE_TIMEOUT) {
                scancode = ATARI_MapScancode(scan);
                SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Sending key release: %d", scancode);
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
    if (gem_events & MU_MESAG) {
        window = _this->windows;
        while(window) {
            win_data = (SDL_WindowData *)window->driverdata;
            if (win_data->handle == msg[3]) {
                SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM event for window ID %d, MSG ID %d", msg[3], msg[0]);
                break;
            }
            window = window->next;
        }

        if(!window) {
            SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM event for unknown window ID %d", msg[3]);
            return;
        }

        // win_data = (SDL_WindowData *)window->driverdata;

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
                    win_data->win_x = msg[4];
                    win_data->win_y = msg[5];
                    mt_wind_calc(WC_WORK, win_data->win_type, 
                        win_data->win_x, win_data->win_y, win_data->win_w, win_data->win_h,
                        &win_data->work_x, &win_data->work_y, &win_data->work_w, &win_data->work_h, sdl_global_aes);
                    SDL_SetWindowPosition(window, win_data->work_x, win_data->work_y);
                    // SDL_SendWindowEvent(window, SDL_WINDOWEVENT_MOVED, win_data->work_x, win_data->work_y);
                }
                break;
            case WM_SIZED:
                if (win_data && msg[3] == win_data->handle){
                    SDL_SetWindowSize(window, msg[6], msg[7]);
                    // SDL_SendWindowEvent(window, SDL_WINDOWEVENT_SIZE_CHANGED, msg[6], msg[7]);
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
                    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "GEM redraw for window ID %d: x=%d y=%d w=%d h=%d", msg[3], sdl_rect.x, sdl_rect.y, sdl_rect.w, sdl_rect.h);
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
    if (gem_events & MU_BUTTON) {
        SDL_Window *mouse_window = NULL;
        /* Find window by mouse coordinates, not msg[3] (which is garbage for MU_BUTTON) */
        window = _this->windows;
        while(window) {
            win_data = (SDL_WindowData *)window->driverdata;
            if (mx >= window->x && mx < window->x + window->w &&
                my >= window->y && my < window->y + window->h) {
                mouse_window = window;
                break;
            }
            window = window->next;
        }
        
        if (mouse_window) {
            local_x = mx - window->x;
            local_y = my - window->y;
            
            if (SDL_GetMouseFocus() != mouse_window) {
                SDL_Window *prev = SDL_GetMouseFocus();
                if (prev) {
                    if (last_button_state & 0x01) SDL_SendMouseButton(prev, 0, SDL_RELEASED, SDL_BUTTON_LEFT);
                    if (last_button_state & 0x02) SDL_SendMouseButton(prev, 0, SDL_RELEASED, SDL_BUTTON_RIGHT);
                }
                last_button_state = 0;
                SDL_SetMouseFocus(mouse_window);
            }
            
            if (mx != last_mx || my != last_my) {
                SDL_SendMouseMotion(mouse_window, 0, 0, local_x, local_y);
                last_mx = mx;
                last_my = my;
            }
            
            if (mb != last_button_state) {
                if ((mb & 0x01) && !(last_button_state & 0x01))
                    SDL_SendMouseButton(mouse_window, 0, SDL_PRESSED, SDL_BUTTON_LEFT);
                else if (!(mb & 0x01) && (last_button_state & 0x01))
                    SDL_SendMouseButton(mouse_window, 0, SDL_RELEASED, SDL_BUTTON_LEFT);
                
                if ((mb & 0x02) && !(last_button_state & 0x02))
                    SDL_SendMouseButton(mouse_window, 0, SDL_PRESSED, SDL_BUTTON_RIGHT);
                else if (!(mb & 0x02) && (last_button_state & 0x02))
                    SDL_SendMouseButton(mouse_window, 0, SDL_RELEASED, SDL_BUTTON_RIGHT);
                
                last_button_state = mb;
            }
        } else {
            if (SDL_GetMouseFocus()) {
                SDL_Window *prev = SDL_GetMouseFocus();
                if (last_button_state & 0x01) SDL_SendMouseButton(prev, 0, SDL_RELEASED, SDL_BUTTON_LEFT);
                if (last_button_state & 0x02) SDL_SendMouseButton(prev, 0, SDL_RELEASED, SDL_BUTTON_RIGHT);
                last_button_state = 0;
                SDL_SetMouseFocus(NULL);
            }
        }
    }
}


#endif /* SDL_VIDEO_DRIVER_GEM */