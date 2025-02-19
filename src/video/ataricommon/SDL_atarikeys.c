#include "SDL_keyboard.h"
#include "SDL_atarikeys.h"
#include <mint/sysbind.h>

/* Lookup table for Atari scancodes to SDL scancodes */
static const SDL_Scancode atari_scancode_table[] = {
    SDL_SCANCODE_UNKNOWN,      /* 0x00 */
    SDL_SCANCODE_ESCAPE,       /* 0x01 */
    SDL_SCANCODE_1,           /* 0x02 */
    SDL_SCANCODE_2,           /* 0x03 */
    SDL_SCANCODE_3,           /* 0x04 */
    SDL_SCANCODE_4,           /* 0x05 */
    SDL_SCANCODE_5,           /* 0x06 */
    SDL_SCANCODE_6,           /* 0x07 */
    SDL_SCANCODE_7,           /* 0x08 */
    SDL_SCANCODE_8,           /* 0x09 */
    SDL_SCANCODE_9,           /* 0x0A */
    SDL_SCANCODE_0,           /* 0x0B */
    SDL_SCANCODE_MINUS,       /* 0x0C */
    SDL_SCANCODE_EQUALS,      /* 0x0D */
    SDL_SCANCODE_BACKSPACE,   /* 0x0E */
    SDL_SCANCODE_TAB,         /* 0x0F */
    SDL_SCANCODE_Q,           /* 0x10 */
    SDL_SCANCODE_W,           /* 0x11 */
    SDL_SCANCODE_E,           /* 0x12 */
    SDL_SCANCODE_R,           /* 0x13 */
    SDL_SCANCODE_T,           /* 0x14 */
    SDL_SCANCODE_Y,           /* 0x15 */
    SDL_SCANCODE_U,           /* 0x16 */
    SDL_SCANCODE_I,           /* 0x17 */
    SDL_SCANCODE_O,           /* 0x18 */
    SDL_SCANCODE_P,           /* 0x19 */
    SDL_SCANCODE_LEFTBRACKET, /* 0x1A */
    SDL_SCANCODE_RIGHTBRACKET,/* 0x1B */
    SDL_SCANCODE_RETURN,      /* 0x1C */
    SDL_SCANCODE_LCTRL,       /* 0x1D */
    SDL_SCANCODE_A,           /* 0x1E */
    SDL_SCANCODE_S,           /* 0x1F */
    SDL_SCANCODE_D,           /* 0x20 */
    SDL_SCANCODE_F,           /* 0x21 */
    SDL_SCANCODE_G,           /* 0x22 */
    SDL_SCANCODE_H,           /* 0x23 */
    SDL_SCANCODE_J,           /* 0x24 */
    SDL_SCANCODE_K,           /* 0x25 */
    SDL_SCANCODE_L,           /* 0x26 */
    SDL_SCANCODE_SEMICOLON,   /* 0x27 */
    SDL_SCANCODE_APOSTROPHE,  /* 0x28 */
    SDL_SCANCODE_GRAVE,       /* 0x29 */
    SDL_SCANCODE_LSHIFT,      /* 0x2A */
    SDL_SCANCODE_BACKSLASH,   /* 0x2B */
    SDL_SCANCODE_Z,           /* 0x2C */
    SDL_SCANCODE_X,           /* 0x2D */
    SDL_SCANCODE_C,           /* 0x2E */
    SDL_SCANCODE_V,           /* 0x2F */
    SDL_SCANCODE_B,           /* 0x30 */
    SDL_SCANCODE_N,           /* 0x31 */
    SDL_SCANCODE_M,           /* 0x32 */
    SDL_SCANCODE_COMMA,       /* 0x33 */
    SDL_SCANCODE_PERIOD,      /* 0x34 */
    SDL_SCANCODE_SLASH,       /* 0x35 */
    SDL_SCANCODE_RSHIFT,      /* 0x36 */
    SDL_SCANCODE_UNKNOWN,     /* 0x37 */
    SDL_SCANCODE_RALT,        /* 0x38 */
    SDL_SCANCODE_SPACE,       /* 0x39 */
    SDL_SCANCODE_CAPSLOCK,    /* 0x3A */
    SDL_SCANCODE_F1,          /* 0x3B */
    SDL_SCANCODE_F2,          /* 0x3C */
    SDL_SCANCODE_F3,          /* 0x3D */
    SDL_SCANCODE_F4,          /* 0x3E */
    SDL_SCANCODE_F5,          /* 0x3F */
    SDL_SCANCODE_F6,          /* 0x40 */
    SDL_SCANCODE_F7,          /* 0x41 */
    SDL_SCANCODE_F8,          /* 0x42 */
    SDL_SCANCODE_F9,          /* 0x43 */
    SDL_SCANCODE_F10,         /* 0x44 */
};

SDL_Scancode ATARI_MapScancode(int scancode)
{
    if (scancode < 0 || scancode >= SDL_arraysize(atari_scancode_table)) {
        return SDL_SCANCODE_UNKNOWN;
    }
    return atari_scancode_table[scancode];
}

SDL_Keycode ATARI_MapKey(int scancode)
{
    SDL_Scancode code = ATARI_MapScancode(scancode);
    return SDL_GetKeyFromScancode(code);
}

Uint16 ATARI_ModState(void)
{
    Uint16 mod = KMOD_NONE;
    Uint8 kbshift = Kbshift(-1);

    if (kbshift & 0x01) mod |= KMOD_LSHIFT;
    if (kbshift & 0x02) mod |= KMOD_RSHIFT;
    if (kbshift & 0x04) mod |= KMOD_CTRL;
    if (kbshift & 0x08) mod |= KMOD_ALT;
    if (kbshift & 0x10) mod |= KMOD_CAPS;

    return mod;
}

// Uint16 ATARI_ModState(void)
// {
//     Uint16 mod = KMOD_NONE;
//     Uint16 kstate;

//     kstate = Kbshift(-1);

//     if (kstate & 0x03) mod |= KMOD_SHIFT;    /* Right or Left shift */
//     if (kstate & 0x04) mod |= KMOD_CTRL;     /* Control */
//     if (kstate & 0x08) mod |= KMOD_ALT;      /* Alternate */
//     if (kstate & 0x10) mod |= KMOD_CAPS;     /* Caps Lock */

//     return mod;
// }
