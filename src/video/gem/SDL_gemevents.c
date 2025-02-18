#include "SDL_gemevents.h"
#include "SDL_gemvideo.h"
#include "SDL_gemwindow.h"
#include "SDL_events.h"
#include "../ataricommon/SDL_atarikeys.h"
#include <gem.h>

int SDL_SendWindowEvent(SDL_Window *window, Uint8 windowevent,
    int data1, int data2);

static short int mouse_buttons = 0;
static short int mouse_x = 0;
static short int mouse_y = 0;

void GEM_InitEvents(_THIS)
{
    /* Initialize mouse */
    graf_mouse(M_ON, NULL);

    /* Set initial mouse position */
    graf_mkstate(&mouse_x, &mouse_y, &mouse_buttons, NULL);
}

void GEM_QuitEvents(_THIS)
{
    /* Nothing to do here */
}

void GEM_PumpEvents(_THIS)
{
    static int call_count = 0;

    short event_mask, mx, my, buttons, kstate, key_state;
    short msg[8];
    SDL_Event event;
    unsigned long interval = 0;  /* No timer delay */

    printf("GEM_PumpEvents called (%d)\n", ++call_count);

    /* Handle events */
    event_mask = MU_MESAG | MU_KEYBD | MU_BUTTON | MU_M1;
    printf("Event mask: 0x%04x\n", event_mask);

    if (evnt_multi(event_mask,
                      0x03, 0x03, 0x01,   /* Mouse button state */
                      0, 0, 0, 0, 0,      /* Mouse rectangle */
                      0, 0, 0, 0, 0,      /* Second mouse rectangle */
                      msg,                 /* Message buffer */
                      interval,            /* Timer delay */
                      &mx, &my,           /* Mouse position */
                      &buttons,           /* Button state */
                      &kstate, 0,           /* Key state */
                      &key_state)) {      /* Key scan code */
        
        printf("evnt_multi returned true\n");
        /* Handle GEM messages first */
        if (msg[0]) {  /* If there's a message */
            SDL_Window *window = NULL;
            SDL_WindowData *win_data = NULL;
            /* Find window from handle */
            for (window = _this->windows; window != NULL; window = window->next) {
                win_data = (SDL_WindowData *)window->driverdata;
                if (win_data->handle == msg[3]) {
                    printf("Found matching window: id=%d\n", window->id);
                    break;
                }
            }

            if (window) {
                switch (msg[0]) {
                    case WM_REDRAW:
                    {   
                        /* Verify this redraw is for our window */
                        if (win_data && msg[3] == win_data->handle) {
                            /* Let SDL handle the redraw through the standard exposure event */
                            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_EXPOSED, 0, 0);
                        }
                    }
                    break;
                    case WM_MOVED:
                    {
                        if (win_data && msg[3] == win_data->handle) {
                            printf("DEBUG: WM_MOVED received: x=%d, y=%d\n", msg[4], msg[5]);
                            /* Update internal position */
                            // window->x = msg[4];
                            // window->y = msg[5];
                            // SDL_SetWindowPosition(window, msg[4], msg[5]);
                            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_MOVED, msg[4], msg[5]);
                        }
                    }
                    break;
                    case WM_TOPPED:
                        wind_set(msg[3], WF_TOP, 0, 0, 0, 0);
                        printf("Processing WM_TOPPED\n");
                        SDL_memset(&event, 0, sizeof(event));
                        event.type = SDL_WINDOWEVENT;
                        event.window.event = SDL_WINDOWEVENT_SHOWN;
                        event.window.windowID = window->id;
                        SDL_PushEvent(&event);
                        break;
                
                    case WM_CLOSED:
                        /* Window close button clicked */
                        SDL_SendWindowEvent(window,
                            SDL_WINDOWEVENT_CLOSE, 0, 0);
                        break;
                
                    case WM_FULLED:
                        /* Window maximized/restored */
                        SDL_memset(&event, 0, sizeof(event));
                        event.type = SDL_WINDOWEVENT;
                        event.window.event = SDL_WINDOWEVENT_MAXIMIZED;
                        event.window.windowID = window->id;
                        SDL_PushEvent(&event);
                        break;
                
                    case WM_ICONIFY:
                        /* Window minimized */
                        SDL_memset(&event, 0, sizeof(event));
                        event.type = SDL_WINDOWEVENT;
                        event.window.event = SDL_WINDOWEVENT_MINIMIZED;
                        event.window.windowID = window->id;
                        SDL_PushEvent(&event);
                        break;
                
                    case WM_UNICONIFY:
                        /* Window restored from minimized state */
                        SDL_memset(&event, 0, sizeof(event));
                        event.type = SDL_WINDOWEVENT;
                        event.window.event = SDL_WINDOWEVENT_RESTORED;
                        event.window.windowID = window->id;
                        SDL_PushEvent(&event);
                        break;
                
                    case WM_SIZED:
                    {
                        if (win_data && msg[3] == win_data->handle) {
                        window->w = msg[6];
                        window->h = msg[7];
                        SDL_SendWindowEvent(window,
                            SDL_WINDOWEVENT_RESIZED, msg[6], msg[7]);
                        }
                    }
                    break;
                }
            }
        } else {
            printf("evnt_multi returned false\n");
        }

        /* Handle mouse movement */
        if (mx != mouse_x || my != mouse_y) {
            printf("Mouse moved: x=%d, y=%d (rel: %d,%d)\n", 
                mx, my, mx - mouse_x, my - mouse_y);
            SDL_memset(&event, 0, sizeof(event));
            event.type = SDL_MOUSEMOTION;
            event.motion.x = mx;
            event.motion.y = my;
            event.motion.xrel = mx - mouse_x;
            event.motion.yrel = my - mouse_y;
            SDL_PushEvent(&event);

            mouse_x = mx;
            mouse_y = my;
        }

        /* Handle mouse buttons */
        if (buttons != mouse_buttons) {
            int changed = buttons ^ mouse_buttons;
            int i;
            printf("Mouse buttons changed: old=%d new=%d\n", mouse_buttons, buttons);

            for (i = 0; i < 3; i++) {
                if (changed & (1 << i)) {
                    printf("Button %d %s\n", i + 1, 
                        (buttons & (1 << i)) ? "pressed" : "released");
                    SDL_memset(&event, 0, sizeof(event));
                    event.type = (buttons & (1 << i)) ? 
                                SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                    event.button.button = i + 1;
                    event.button.x = mx;
                    event.button.y = my;
                    SDL_PushEvent(&event);
                }
            }
            mouse_buttons = buttons;
        }

        /* Handle keyboard events */
        if (key_state) {
            printf("Keyboard event: scancode=%d, state=%s\n", 
                key_state, (kstate & K_RSHIFT) ? "pressed" : "released");
            SDL_memset(&event, 0, sizeof(event));
            event.key.keysym.scancode = ATARI_MapScancode(key_state);
            event.key.keysym.sym = ATARI_MapKey(key_state);
            event.key.keysym.mod = ATARI_ModState();
            
            if (kstate & K_RSHIFT) {
                event.type = SDL_KEYDOWN;
                event.key.state = SDL_PRESSED;
            } else {
                event.type = SDL_KEYUP;
                event.key.state = SDL_RELEASED;
            }
            
            SDL_PushEvent(&event);
        }
    }
}


// void GEM_PumpEvents(_THIS)
// {
//     short event_mask, mx, my, buttons, kstate, key_state;
//     short msg[8];
//     SDL_Event event;
//     unsigned long interval = 0;  /* No timer delay */
//     /* Handle events */
//     event_mask = MU_MESAG | MU_KEYBD | MU_BUTTON | MU_M1;
//     while (evnt_multi(event_mask,
//                       0x03, 0x03, 0x01,   /* Mouse button state */
//                       0, 0, 0, 0, 0,      /* Mouse rectangle */
//                       0, 0, 0, 0, 0,      /* Second mouse rectangle */
//                       msg,                 /* Message buffer */
//                       interval,            /* Timer delay */
//                       &mx, &my,           /* Mouse position */
//                       &buttons,           /* Button state */
//                       &kstate, 0,           /* Key state */
//                       &key_state)) {      /* Key scan code */
//         /* Handle mouse movement */
//         if (mx != mouse_x || my != mouse_y) {
//             SDL_memset(&event, 0, sizeof(event));
//             event.type = SDL_MOUSEMOTION;
//             event.motion.x = mx;
//             event.motion.y = my;
//             event.motion.xrel = mx - mouse_x;
//             event.motion.yrel = my - mouse_y;
//             SDL_PushEvent(&event);
//             mouse_x = mx;
//             mouse_y = my;
//         }
//         /* Handle mouse buttons */
//         if (buttons != mouse_buttons) {
//             int changed = buttons ^ mouse_buttons;
//             int i;
//             for (i = 0; i < 3; i++) {
//                 if (changed & (1 << i)) {
//                     SDL_memset(&event, 0, sizeof(event));
//                     event.type = (buttons & (1 << i)) ? 
//                                 SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
//                     event.button.button = i + 1;
//                     event.button.x = mx;
//                     event.button.y = my;
//                     SDL_PushEvent(&event);
//                 }
//             }
//             mouse_buttons = buttons;
//         }
//         /* Handle keyboard events */
//         if (key_state) {
//             SDL_memset(&event, 0, sizeof(event));
//             event.key.keysym.scancode = ATARI_MapScancode(key_state);
//             event.key.keysym.sym = ATARI_MapKey(key_state);
//             event.key.keysym.mod = ATARI_ModState();
//             if (kstate & K_RSHIFT) {
//                 event.type = SDL_KEYDOWN;
//                 event.key.state = SDL_PRESSED;
//             } else {
//                 event.type = SDL_KEYUP;
//                 event.key.state = SDL_RELEASED;
//             }
//             SDL_PushEvent(&event);
//         }
//         /* Handle GEM messages */
//         if (event_mask & MU_MESAG) {
//             GEM_HandleMessage(_this, msg);
//         }
//     }
// }
