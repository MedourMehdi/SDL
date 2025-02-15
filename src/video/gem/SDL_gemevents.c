#include "SDL_gemevents.h"
#include "SDL_gemvideo.h"
#include "SDL_gemwindow.h"
#include "SDL_events.h"
#include "../ataricommon/SDL_atarikeys.h"
#include <gem.h>

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

static void GEM_HandleMessage(_THIS, short *msg)
{
    SDL_Window *window;
    SDL_WindowData *win_data;
    SDL_Event event;

    /* Find window from handle */
    for (window = _this->windows; window != NULL; window = window->next) {
        win_data = (SDL_WindowData *)window->driverdata;
        if (win_data->handle == msg[3]) {
            break;
        }
    }

    if (!window) {
        return;
    }

    switch (msg[0]) {
        case WM_REDRAW:
            {
                /* Window needs redrawing */
                wind_update(BEG_UPDATE);
                graf_mouse(M_OFF, NULL);

                /* Generate expose event */
                SDL_memset(&event, 0, sizeof(event));
                event.type = SDL_WINDOWEVENT;
                event.window.event = SDL_WINDOWEVENT_EXPOSED;
                event.window.windowID = window->id;
                SDL_PushEvent(&event);

                graf_mouse(M_ON, NULL);
                wind_update(END_UPDATE);
            }
            break;

        case WM_TOPPED:
        case WM_CLOSED:
        case WM_MOVED:
        case WM_SIZED:
        case WM_FULLED:
            /* Handle other window events */
            SDL_memset(&event, 0, sizeof(event));
            event.type = SDL_WINDOWEVENT;
            event.window.windowID = window->id;
            
            switch (msg[0]) {
                case WM_TOPPED:
                    event.window.event = SDL_WINDOWEVENT_SHOWN;
                    break;
                case WM_CLOSED:
                    event.window.event = SDL_WINDOWEVENT_CLOSE;
                    break;
                case WM_MOVED:
                    event.window.event = SDL_WINDOWEVENT_MOVED;
                    event.window.data1 = msg[4];
                    event.window.data2 = msg[5];
                    break;
                case WM_SIZED:
                    event.window.event = SDL_WINDOWEVENT_RESIZED;
                    event.window.data1 = msg[6];
                    event.window.data2 = msg[7];
                    break;
                case WM_FULLED:
                    event.window.event = SDL_WINDOWEVENT_MAXIMIZED;
                    break;
            }
            SDL_PushEvent(&event);
            break;
    }
}

void GEM_PumpEvents(_THIS)
{
    short event_mask, mx, my, buttons, kstate, key_state;
    short msg[8];
    SDL_Event event;
    unsigned long interval = 0;  /* No timer delay */

    /* Handle events */
    event_mask = MU_MESAG | MU_KEYBD | MU_BUTTON | MU_M1;
    
    while (evnt_multi(event_mask,
                      0x03, 0x03, 0x01,   /* Mouse button state */
                      0, 0, 0, 0, 0,      /* Mouse rectangle */
                      0, 0, 0, 0, 0,      /* Second mouse rectangle */
                      msg,                 /* Message buffer */
                      interval,            /* Timer delay */
                      &mx, &my,           /* Mouse position */
                      &buttons,           /* Button state */
                      &kstate, 0,           /* Key state */
                      &key_state)) {      /* Key scan code */

        /* Handle GEM messages first */
        if (msg[0]) {  /* If there's a message */
            SDL_Window *window;
            SDL_WindowData *win_data;

            /* Find window from handle */
            for (window = _this->windows; window != NULL; window = window->next) {
                win_data = (SDL_WindowData *)window->driverdata;
                if (win_data->handle == msg[3]) {
                    break;
                }
            }

            if (window) {
                switch (msg[0]) {
                    case WM_REDRAW:
                        {
                            /* Window needs redrawing */
                            wind_update(BEG_UPDATE);
                            graf_mouse(M_OFF, NULL);

                            /* Generate expose event */
                            SDL_memset(&event, 0, sizeof(event));
                            event.type = SDL_WINDOWEVENT;
                            event.window.event = SDL_WINDOWEVENT_EXPOSED;
                            event.window.windowID = window->id;
                            SDL_PushEvent(&event);

                            graf_mouse(M_ON, NULL);
                            wind_update(END_UPDATE);
                        }
                        break;

                    case WM_MOVED:
                        /* Window moved */
                        SDL_memset(&event, 0, sizeof(event));
                        event.type = SDL_WINDOWEVENT;
                        event.window.event = SDL_WINDOWEVENT_MOVED;
                        event.window.windowID = window->id;
                        event.window.data1 = msg[4];  /* New X position */
                        event.window.data2 = msg[5];  /* New Y position */
                        SDL_PushEvent(&event);
                        break;

                    case WM_TOPPED:
                        /* Window brought to front */
                        SDL_memset(&event, 0, sizeof(event));
                        event.type = SDL_WINDOWEVENT;
                        event.window.event = SDL_WINDOWEVENT_SHOWN;
                        event.window.windowID = window->id;
                        SDL_PushEvent(&event);
                        break;

                    case WM_CLOSED:
                        /* Window close button clicked */
                        SDL_memset(&event, 0, sizeof(event));
                        event.type = SDL_WINDOWEVENT;
                        event.window.event = SDL_WINDOWEVENT_CLOSE;
                        event.window.windowID = window->id;
                        SDL_PushEvent(&event);
                        break;
                }
            }
        }

        /* Handle mouse movement */
        if (mx != mouse_x || my != mouse_y) {
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

            for (i = 0; i < 3; i++) {
                if (changed & (1 << i)) {
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
