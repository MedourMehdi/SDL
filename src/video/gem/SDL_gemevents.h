#ifndef SDL_gemevents_h_
#define SDL_gemevents_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include <gem.h>

extern void GEM_PumpEvents(_THIS);
extern void GEM_InitEvents(_THIS);
extern void GEM_QuitEvents(_THIS);

/* GEM event masks */
#define MU_KEYBD    0x0001      /* Keyboard event */
#define MU_BUTTON   0x0002      /* Mouse button event */
#define MU_M1       0x0004      /* Mouse movement */
#define MU_M2       0x0008      /* Mouse movement */
#define MU_MESAG    0x0010      /* GEM message */
#define MU_TIMER    0x0020      /* Timer event */

#endif /* SDL_gemevents_h_ */
