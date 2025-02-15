#ifndef SDL_config_atari_h_
#define SDL_config_atari_h_

#include "SDL_platform.h"

/* Enable the stub haptic driver (src/haptic/dummy/\*.c) */
#define SDL_HAPTIC_DISABLED 1

/* Enable the stub HIDAPI */
#define SDL_HIDAPI_DISABLED 1

/* Enable the stub sensor driver (src/sensor/dummy/\*.c) */
#define SDL_SENSOR_DISABLED 1

/* Enable the stub shared object loader (src/loadso/dummy/\*.c) */
#define SDL_LOADSO_DISABLED 1

/* Video driver flags */
#define SDL_VIDEO_DRIVER_ATARI 1
#define SDL_VIDEO_DRIVER_GEM 1
#define SDL_AUDIO_DRIVER_ATARI 1
#define SDL_INPUT_ATARI 1
#endif /* SDL_config_atari_h_ */