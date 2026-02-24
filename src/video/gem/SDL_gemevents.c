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
   SDL_gemevents.c – GEM event pump for SDL2
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Responsibilities: AES event loop (evnt_multi), keyboard and mouse event
   translation, window message dispatch (WM_REDRAW, WM_MOVED, WM_SIZED,
   WM_CLOSED), and modifier state tracking via Kbshift().

   C90 compliant.
   ============================================================================ */

#include "../../SDL_internal.h"
#include "SDL_gemvideo.h"
#include "SDL_gemkeys.h"

#ifdef SDL_VIDEO_DRIVER_GEM

/* Release timeout for ALL keys.
 * AES autorepeat fires every ~30-50 ms.  4 frames @ 17 ms = 68 ms gives a
 * comfortable margin without perceptible input lag on release. */
#define KEY_RELEASE_TIMEOUT  4
#define TIMER_MS            17

/* ============================================
   Persistent state (survives between pump calls)
   ============================================ */

/* Mouse */
static short last_mx           = -1;
static short last_my           = -1;
static Uint8 last_button_state = 0;
static short mb                = 0;

/* Keyboard */
static Uint16 last_mod_state = 0;

/* key_state_map[scan]   = 1 when SDL has been told this key is PRESSED.
 * key_frame_count[scan] = frames since the last make code for this key.
 * Indexed by Atari scan code (0x01-0x7F, max < 128).
 * Used for ALL non-modifier keys so simultaneous presses work correctly. */
static unsigned char key_state_map[128]   = {0};
static unsigned char key_frame_count[128] = {0};

/* ============================================
   IsModifierKey
   NOTE: no SDL_LogDebug - called in the hot keyboard path.
   ============================================ */
static int IsModifierKey(SDL_Scancode sc)
{
    return (sc == SDL_SCANCODE_LSHIFT ||
            sc == SDL_SCANCODE_RSHIFT ||
            sc == SDL_SCANCODE_LCTRL  ||
            sc == SDL_SCANCODE_LALT);
}

/* ============================================
   GEM_InitEvents / GEM_QuitEvents
   ============================================ */
void GEM_InitEvents(_THIS)
{
    SDL_memset(key_state_map,   0, sizeof(key_state_map));
    SDL_memset(key_frame_count, 0, sizeof(key_frame_count));
    last_mod_state    = 0;
    last_button_state = 0;
    last_mx           = -1;
    last_my           = -1;
    mb                = 0;
}

void GEM_QuitEvents(_THIS)
{
    /* Nothing to clean up */
}

/* ============================================
   HandleKeyboard
   Called on each MU_KEYBD event (always a make code under GEM).

   ALL keys use the same timeout-based hold detection so that any
   combination of keys can be held simultaneously.  Text input is
   emitted on the first make code only (no duplicate characters on
   autorepeat).  RELEASED is always fired by AgeGameKeys() on timeout.
   ============================================ */
static void HandleKeyboard(short key_state_word)
{
    Uint8        atari_scan;
    SDL_Scancode scancode;

    atari_scan = (Uint8)((key_state_word >> 8) & 0xFF);

    if (atari_scan == 0 || atari_scan >= 128) {
        return;
    }

    scancode = ATARI_MapScancode((int)atari_scan);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return;
    }

    if (IsModifierKey(scancode)) {
        return;
    }

    /* Reset the aging counter — proves this key is still physically held.
     * This is the make code; RELEASED will be sent by AgeGameKeys() once
     * no further make codes arrive within KEY_RELEASE_TIMEOUT frames. */
    key_frame_count[atari_scan] = 0;

    if (!key_state_map[atari_scan]) {
        /* First make code: send PRESSED and text (if printable). */
        char ascii_char[2];
        key_state_map[atari_scan] = 1;
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: key down scan=0x%02X sdl=%d", (int)atari_scan, (int)scancode);
        SDL_SendKeyboardKey(SDL_PRESSED, scancode);

        ascii_char[0] = (char)(key_state_word & 0xFF);
        ascii_char[1] = '\0';
        if (ascii_char[0] >= 32 && ascii_char[0] <= 126) {
            SDL_SendKeyboardText(ascii_char);
        }
        /* Autorepeat make codes are silently absorbed: no duplicate text,
         * no duplicate PRESSED events, counter reset above keeps key alive. */
    }
}

/* ============================================
   AgeGameKeys
   Run every pump cycle — the only way to detect key release under GEM.
   Now covers ALL keys, not just movement keys.
   ============================================ */
static void AgeGameKeys(void)
{
    int          sc;
    SDL_Scancode scancode;

    for (sc = 0x01; sc < 128; sc++) {
        if (!key_state_map[sc]) {
            continue;
        }

        key_frame_count[sc]++;

        if (key_frame_count[sc] > KEY_RELEASE_TIMEOUT) {
            scancode = ATARI_MapScancode(sc);
            SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                         "GEM: game key release timeout scan=0x%02X sdl=%d",
                         sc, (int)scancode);
            SDL_SendKeyboardKey(SDL_RELEASED, scancode);
            key_state_map[sc]   = 0;
            key_frame_count[sc] = 0;
        }
    }
}

/* ============================================
   HandleModifiers
   Polls Kbshift() (safe TOS system variable, not hardware).
   ============================================ */
static void HandleModifiers(void)
{
    Uint16 current_mod;
    Uint16 changed;

    current_mod = ATARI_ModState();
    changed     = last_mod_state ^ current_mod;

    if (changed) {
        if (changed & KMOD_LSHIFT) {
            SDL_SendKeyboardKey(
                (current_mod & KMOD_LSHIFT) ? SDL_PRESSED : SDL_RELEASED,
                SDL_SCANCODE_LSHIFT);
        }
        if (changed & KMOD_RSHIFT) {
            SDL_SendKeyboardKey(
                (current_mod & KMOD_RSHIFT) ? SDL_PRESSED : SDL_RELEASED,
                SDL_SCANCODE_RSHIFT);
        }
        if (changed & KMOD_CTRL) {
            SDL_SendKeyboardKey(
                (current_mod & KMOD_CTRL) ? SDL_PRESSED : SDL_RELEASED,
                SDL_SCANCODE_LCTRL);
        }
        if (changed & KMOD_ALT) {
            SDL_SendKeyboardKey(
                (current_mod & KMOD_ALT) ? SDL_PRESSED : SDL_RELEASED,
                SDL_SCANCODE_LALT);
        }
        last_mod_state = current_mod;
    }
}

/* ============================================
   HandleMessage
   FIXED: returns int instead of bare 'return' inside the pump,
   so mouse events are never dropped on unknown window handles.
   ============================================ */
static int HandleMessage(_THIS, const short *msg)
{
    SDL_Window     *window;
    SDL_WindowData *win_data;

    window = _this->windows;
    while (window) {
        win_data = (SDL_WindowData *)window->driverdata;
        if (win_data->handle == msg[3]) {
            break;
        }
        window = window->next;
    }

    if (!window) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                     "GEM: message %d for unknown handle %d",
                     (int)msg[0], (int)msg[3]);
        return 0;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                 "GEM: message %d handle %d", (int)msg[0], (int)msg[3]);

    switch (msg[0]) {

        case WM_FULLED:
            win_data->is_maximized = !win_data->is_maximized;
            if (win_data->is_maximized) {
                SDL_MaximizeWindow(window);
            } else {
                SDL_RestoreWindow(window);
            }
            break;

        case WM_ICONIFY:
            SDL_MinimizeWindow(window);
            break;

        case WM_UNICONIFY:
            SDL_RestoreWindow(window);
            break;

        case WM_CLOSED:
            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_CLOSE, 0, 0);
            break;

        case WM_MOVED:
            win_data->win_x = msg[4];
            win_data->win_y = msg[5];
            mt_wind_calc(WC_WORK, win_data->win_type,
                         win_data->win_x, win_data->win_y,
                         win_data->win_w,  win_data->win_h,
                         &win_data->work_x, &win_data->work_y,
                         &win_data->work_w, &win_data->work_h,
                         sdl_global_aes);
            SDL_SetWindowPosition(window, win_data->work_x, win_data->work_y);
            break;

        case WM_SIZED: {
            /* FIXED: convert border rect to work area before SDL call */
            short work_x, work_y, work_w, work_h;
            mt_wind_calc(WC_WORK, win_data->win_type,
                         msg[4], msg[5], msg[6], msg[7],
                         &work_x, &work_y, &work_w, &work_h,
                         sdl_global_aes);
            win_data->win_x  = msg[4];
            win_data->win_y  = msg[5];
            win_data->win_w  = msg[6];
            win_data->win_h  = msg[7];
            win_data->work_x = work_x;
            win_data->work_y = work_y;
            win_data->work_w = work_w;
            win_data->work_h = work_h;
            SDL_SetWindowSize(window, work_w, work_h);
            break;
        }

        case WM_TOPPED:
            mt_wind_set(msg[3], WF_TOP, 0, 0, 0, 0, sdl_global_aes);
            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_FOCUS_GAINED, 0, 0);
            SDL_SetKeyboardFocus(window);
            break;

        case WM_UNTOPPED:
            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_FOCUS_LOST, 0, 0);
            SDL_SetKeyboardFocus(NULL);
            break;

        case WM_REDRAW: {
            GRECT    work;
            SDL_Rect sdl_rect;

            mt_wind_get_grect(win_data->handle, WF_WORKXYWH,
                              &work, sdl_global_aes);

            sdl_rect.x = msg[4] - work.g_x;
            sdl_rect.y = msg[5] - work.g_y;
            sdl_rect.w = msg[6];
            sdl_rect.h = msg[7];

            if (sdl_rect.x < 0) { sdl_rect.w += sdl_rect.x; sdl_rect.x = 0; }
            if (sdl_rect.y < 0) { sdl_rect.h += sdl_rect.y; sdl_rect.y = 0; }
            if (sdl_rect.x + sdl_rect.w > window->w)
                sdl_rect.w = window->w - sdl_rect.x;
            if (sdl_rect.y + sdl_rect.h > window->h)
                sdl_rect.h = window->h - sdl_rect.y;

            SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                         "GEM: redraw handle=%d x=%d y=%d w=%d h=%d",
                         (int)msg[3],
                         sdl_rect.x, sdl_rect.y, sdl_rect.w, sdl_rect.h);

            if (window->surface && sdl_rect.w > 0 && sdl_rect.h > 0) {
                SDL_UpdateWindowSurfaceRects(window, &sdl_rect, 1);
            }
            break;
        }

        default:
            SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
                         "GEM: unhandled message %d handle=%d",
                         (int)msg[0], (int)msg[3]);
            break;
    }

    return 1;
}

/* ============================================
   HandleMouse
   ============================================ */
static void HandleMouse(_THIS, short mx, short my, short new_mb)
{
    SDL_Window     *window;
    SDL_Window     *mouse_window;
    short           local_x, local_y;

    mouse_window = NULL;
    window = _this->windows;
    while (window) {
        if (mx >= window->x && mx < window->x + window->w &&
            my >= window->y && my < window->y + window->h) {
            mouse_window = window;
            break;
        }
        window = window->next;
    }

    if (mouse_window) {
        local_x = mx - mouse_window->x;
        local_y = my - mouse_window->y;

        if (SDL_GetMouseFocus() != mouse_window) {
            SDL_Window *prev = SDL_GetMouseFocus();
            if (prev) {
                if (last_button_state & 0x01)
                    SDL_SendMouseButton(prev, 0, SDL_RELEASED, SDL_BUTTON_LEFT);
                if (last_button_state & 0x02)
                    SDL_SendMouseButton(prev, 0, SDL_RELEASED, SDL_BUTTON_RIGHT);
            }
            last_button_state = 0;
            SDL_SetMouseFocus(mouse_window);
        }

        if (mx != last_mx || my != last_my) {
            SDL_SendMouseMotion(mouse_window, 0, 0, local_x, local_y);
            last_mx = mx;
            last_my = my;
        }

        if (new_mb != (short)last_button_state) {
            if ((new_mb & 0x01) && !(last_button_state & 0x01))
                SDL_SendMouseButton(mouse_window, 0, SDL_PRESSED,  SDL_BUTTON_LEFT);
            else if (!(new_mb & 0x01) && (last_button_state & 0x01))
                SDL_SendMouseButton(mouse_window, 0, SDL_RELEASED, SDL_BUTTON_LEFT);

            if ((new_mb & 0x02) && !(last_button_state & 0x02))
                SDL_SendMouseButton(mouse_window, 0, SDL_PRESSED,  SDL_BUTTON_RIGHT);
            else if (!(new_mb & 0x02) && (last_button_state & 0x02))
                SDL_SendMouseButton(mouse_window, 0, SDL_RELEASED, SDL_BUTTON_RIGHT);

            last_button_state = (Uint8)new_mb;
        }
    } else {
        if (SDL_GetMouseFocus()) {
            SDL_Window *prev = SDL_GetMouseFocus();
            if (last_button_state & 0x01)
                SDL_SendMouseButton(prev, 0, SDL_RELEASED, SDL_BUTTON_LEFT);
            if (last_button_state & 0x02)
                SDL_SendMouseButton(prev, 0, SDL_RELEASED, SDL_BUTTON_RIGHT);
            last_button_state = 0;
            SDL_SetMouseFocus(NULL);
        }
    }
}

/* ============================================
   GEM_PumpEvents
   ============================================ */
void GEM_PumpEvents(_THIS)
{
    short mx, my, new_mb;
    short kstate, key_state_word, mc;
    short msg[8];
    short gem_events;

    /*
     * MU_TIMER keeps the pump ticking every TIMER_MS ms so
     * AgeGameKeys() runs regularly even when no events arrive.
     */
    gem_events = mt_evnt_multi(
        MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER,
        0x101, 3, (~mb) & 3,
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 0,
        msg, TIMER_MS,
        &mx, &my, &new_mb, &kstate, &key_state_word, &mc,
        sdl_global_aes);

    /*
     * Per-frame work — runs every cycle regardless of GEM events.
     *
     * AgeGameKeys:        detect key release via timeout (game keys).
     * HandleModifiers:    poll Kbshift() for Shift/Ctrl/Alt state.
     * SDL_JoystickUpdate: read Xbios_joystick and fire hat/button events.
     *                     The XBIOS vector writes it asynchronously at
     *                     interrupt level; this turns it into SDL events.
     */
    AgeGameKeys();
    HandleModifiers();
    SDL_JoystickUpdate();

    if (!gem_events) {
        return;
    }

    mb = new_mb;

    if (gem_events & MU_KEYBD) {
        HandleKeyboard(key_state_word);
    }

    if (gem_events & MU_MESAG) {
        HandleMessage(_this, msg);
    }

    if (gem_events & MU_BUTTON) {
        HandleMouse(_this, mx, my, new_mb);
    }
}

#endif /* SDL_VIDEO_DRIVER_GEM */