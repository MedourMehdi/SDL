#include "SDL_atarievents.h"
#include "../ataricommon/SDL_atarikeys.h"
#include <mint/osbind.h>
#include <mint/ostruct.h>

/* For mouse functions */
#define XBIOS_MOUSEVEC    0x118
#define XBIOS_MOUSEB      0x11C
#define XBIOS_MOUSEX      0x11E
#define XBIOS_MOUSEY      0x120

/* Mouse state tracking */
static int mouse_x = 0;
static int mouse_y = 0;
static int mouse_buttons = 0;
static int old_mouse_x = 0;
static int old_mouse_y = 0;
static int old_mouse_buttons = 0;

/* IKBD states */
static Uint8 ikbd_keyboard[128];
// static Uint8 ikbd_mouseb = 0;

void ATARI_InitEvents(_THIS)
{
    /* Reset keyboard state */
    SDL_memset(ikbd_keyboard, 0, sizeof(ikbd_keyboard));

    /* Get initial mouse state using XBIOS */
    old_mouse_x = mouse_x = *((short *)XBIOS_MOUSEX);
    old_mouse_y = mouse_y = *((short *)XBIOS_MOUSEY);
    old_mouse_buttons = mouse_buttons = *((short *)XBIOS_MOUSEB);
}

void ATARI_QuitEvents(_THIS)
{
    /* Nothing to do */
}

void ATARI_PumpEvents(_THIS)
{
    SDL_Event event;
    int i, mousex, mousey, mouseb;
    // long key_state;

    /* Get mouse state using XBIOS */
    mousex = *((short *)XBIOS_MOUSEX);
    mousey = *((short *)XBIOS_MOUSEY);
    mouseb = *((short *)XBIOS_MOUSEB) & 0x03;  /* Only use first two buttons */
    
    /* Handle mouse movement */
    if (mousex != old_mouse_x || mousey != old_mouse_y) {
        SDL_memset(&event, 0, sizeof(event));
        event.type = SDL_MOUSEMOTION;
        event.motion.x = mousex;
        event.motion.y = mousey;
        event.motion.xrel = mousex - old_mouse_x;
        event.motion.yrel = mousey - old_mouse_y;
        SDL_PushEvent(&event);

        old_mouse_x = mousex;
        old_mouse_y = mousey;
    }

    /* Handle mouse buttons */
    if (mouseb != old_mouse_buttons) {
        for (i = 0; i < 2; i++) {  /* ST has 2 mouse buttons */
            if ((mouseb & (1 << i)) != (old_mouse_buttons & (1 << i))) {
                SDL_memset(&event, 0, sizeof(event));
                event.type = (mouseb & (1 << i)) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                event.button.button = i + 1;
                event.button.x = mousex;
                event.button.y = mousey;
                SDL_PushEvent(&event);
            }
        }
        old_mouse_buttons = mouseb;
    }

    /* Handle keyboard using BIOS */
    if (Bconstat(2)) {  /* Check if key is available */
        long key = Bconin(2);
        int scancode = (key >> 16) & 0xFF;
        int pressed = !(key & 0x80);

        if (pressed != ikbd_keyboard[scancode]) {
            SDL_memset(&event, 0, sizeof(event));
            event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.keysym.scancode = ATARI_MapScancode(scancode);
            event.key.keysym.sym = ATARI_MapKey(scancode);
            event.key.keysym.mod = ATARI_ModState();
            event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
            SDL_PushEvent(&event);

            ikbd_keyboard[scancode] = pressed;
        }
    }
}