/* ============================================ *
 * SDL_atarikeys.h  *
 * Keyboard mapping header                      *
 * ============================================ */
#ifndef SDL_gemkeys_h_
#define SDL_gemkeys_h_

#include "SDL_scancode.h"
#include "SDL_keycode.h"

extern SDL_Scancode ATARI_MapScancode(int scancode);
extern SDL_Keycode ATARI_MapKey(int scancode);
extern Uint16 ATARI_ModState(void);

#endif /* SDL_gemkeys_h_ */