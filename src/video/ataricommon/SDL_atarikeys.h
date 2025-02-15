#ifndef SDL_atarikeys_h_
#define SDL_atarikeys_h_

#include "../../SDL_internal.h"
#include "SDL_scancode.h"
#include "SDL_keycode.h"

/* Atari key scancodes */
#define ATARI_SCANCODE_ESC      0x01
#define ATARI_SCANCODE_1        0x02
#define ATARI_SCANCODE_2        0x03
#define ATARI_SCANCODE_3        0x04
#define ATARI_SCANCODE_4        0x05
#define ATARI_SCANCODE_5        0x06
#define ATARI_SCANCODE_6        0x07
#define ATARI_SCANCODE_7        0x08
#define ATARI_SCANCODE_8        0x09
#define ATARI_SCANCODE_9        0x0A
#define ATARI_SCANCODE_0        0x0B
#define ATARI_SCANCODE_MINUS    0x0C
#define ATARI_SCANCODE_EQUALS   0x0D
#define ATARI_SCANCODE_BACKSP   0x0E
#define ATARI_SCANCODE_TAB      0x0F
#define ATARI_SCANCODE_Q        0x10
#define ATARI_SCANCODE_W        0x11
#define ATARI_SCANCODE_E        0x12
#define ATARI_SCANCODE_R        0x13
#define ATARI_SCANCODE_T        0x14
#define ATARI_SCANCODE_Y        0x15
#define ATARI_SCANCODE_U        0x16
#define ATARI_SCANCODE_I        0x17
#define ATARI_SCANCODE_O        0x18
#define ATARI_SCANCODE_P        0x19
#define ATARI_SCANCODE_LBRACKET 0x1A
#define ATARI_SCANCODE_RBRACKET 0x1B
#define ATARI_SCANCODE_RETURN   0x1C
#define ATARI_SCANCODE_CONTROL  0x1D
#define ATARI_SCANCODE_A        0x1E
#define ATARI_SCANCODE_S        0x1F
#define ATARI_SCANCODE_D        0x20
#define ATARI_SCANCODE_F        0x21
#define ATARI_SCANCODE_G        0x22
#define ATARI_SCANCODE_H        0x23
#define ATARI_SCANCODE_J        0x24
#define ATARI_SCANCODE_K        0x25
#define ATARI_SCANCODE_L        0x26
#define ATARI_SCANCODE_SEMICOL  0x27
#define ATARI_SCANCODE_QUOTE    0x28
#define ATARI_SCANCODE_BACKQUOTE 0x29
#define ATARI_SCANCODE_LSHIFT   0x2A
#define ATARI_SCANCODE_BACKSLASH 0x2B
#define ATARI_SCANCODE_Z        0x2C
#define ATARI_SCANCODE_X        0x2D
#define ATARI_SCANCODE_C        0x2E
#define ATARI_SCANCODE_V        0x2F
#define ATARI_SCANCODE_B        0x30
#define ATARI_SCANCODE_N        0x31
#define ATARI_SCANCODE_M        0x32
#define ATARI_SCANCODE_COMMA    0x33
#define ATARI_SCANCODE_PERIOD   0x34
#define ATARI_SCANCODE_SLASH    0x35
#define ATARI_SCANCODE_RSHIFT   0x36
#define ATARI_SCANCODE_ALTGR    0x38
#define ATARI_SCANCODE_SPACE    0x39
#define ATARI_SCANCODE_CAPSLOCK 0x3A
#define ATARI_SCANCODE_F1       0x3B
#define ATARI_SCANCODE_F2       0x3C
#define ATARI_SCANCODE_F3       0x3D
#define ATARI_SCANCODE_F4       0x3E
#define ATARI_SCANCODE_F5       0x3F
#define ATARI_SCANCODE_F6       0x40
#define ATARI_SCANCODE_F7       0x41
#define ATARI_SCANCODE_F8       0x42
#define ATARI_SCANCODE_F9       0x43
#define ATARI_SCANCODE_F10      0x44

/* Function prototypes */
extern SDL_Scancode ATARI_MapScancode(int scancode);
extern SDL_Keycode ATARI_MapKey(int scancode);
extern Uint16 ATARI_ModState(void);

#endif /* SDL_atarikeys_h_ */
