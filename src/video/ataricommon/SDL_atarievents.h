#ifndef SDL_atarievents_h_
#define SDL_atarievents_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "SDL_events.h"

extern void ATARI_PumpEvents(_THIS);
extern void ATARI_InitEvents(_THIS);
extern void ATARI_QuitEvents(_THIS);

#endif /* SDL_atarievents_h_ */
