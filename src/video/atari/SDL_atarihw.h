#ifndef SDL_atarihw_h_
#define SDL_atarihw_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

/* Atari hardware registers */
#define ST_SHIFTER_REGS    0xFF8200L
#define TT_SHIFTER_REGS    0xFF8400L
#define F30_VIDEL_REGS     0xFF8280L

/* Video modes */
#define ST_LOW             0x0000
#define ST_MEDIUM          0x0100
#define ST_HIGH            0x0200
#define TT_LOW             0x0700
#define F30_VIDEL          0x1000

/* Screen resolutions */
#define ST_LOW_RES_WIDTH   320
#define ST_LOW_RES_HEIGHT  200
#define ST_MED_RES_WIDTH   640
#define ST_MED_RES_HEIGHT  200
#define ST_HIGH_RES_WIDTH  640
#define ST_HIGH_RES_HEIGHT 400
#define TT_LOW_RES_WIDTH   320
#define TT_LOW_RES_HEIGHT  480
#define F30_MAX_WIDTH      768
#define F30_MAX_HEIGHT     480

/* Function prototypes */
extern int ATARI_InitHardware(_THIS);
extern void ATARI_QuitHardware(_THIS);
extern int ATARI_SetVideoMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
extern void ATARI_SaveVideoMode(_THIS);
extern void ATARI_RestoreVideoMode(_THIS);

#define ATARI_ALIGN_LONG(addr) ((addr + 3) & ~3)

#endif /* SDL_atarihw_h_ */
