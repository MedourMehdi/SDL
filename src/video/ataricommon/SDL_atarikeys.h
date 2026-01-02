/* ============================================ *
 * FILE: src/video/ataricommon/SDL_atarikeys.h  *
 * Keyboard mapping header                      *
 * ============================================ */
#ifndef SDL_atarikeys_h_
#define SDL_atarikeys_h_

#include "../../SDL_internal.h"
#include "../../events/SDL_events_c.h"

extern SDL_Scancode ATARI_MapScancode(int scancode);
extern SDL_Keycode ATARI_MapKey(int scancode);
extern Uint16 ATARI_ModState(void);

#endif /* SDL_atarikeys_h_ */