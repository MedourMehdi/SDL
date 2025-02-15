#include "../../SDL_internal.h"

#if SDL_JOYSTICK_ATARI

#include "SDL_sysjoystick.h"
#include "SDL_joystick.h"
#include <mint/osbind.h>

/* Atari joystick port definitions */
#define IKBD_JOY_PORT0    0xFFFC00  /* Port 0 data register */
#define IKBD_JOY_PORT1    0xFFFC02  /* Port 1 data register */

/* Joystick button masks */
#define JOY_UP      0x01
#define JOY_DOWN    0x02
#define JOY_LEFT    0x04
#define JOY_RIGHT   0x08
#define JOY_FIRE    0x80

static SDL_bool SDL_AtariJoystickInit(void)
{
    return SDL_TRUE;
}

static int SDL_AtariJoystickGetCount(void)
{
    return 2; /* Atari ST has 2 joystick ports */
}

static void SDL_AtariJoystickDetect(void)
{
    /* Nothing to do, ports are always present */
}

static const char *SDL_AtariJoystickGetDeviceName(int device_index)
{
    switch (device_index) {
        case 0:
            return "Atari Joystick Port 0";
        case 1:
            return "Atari Joystick Port 1";
        default:
            return NULL;
    }
}

static int SDL_AtariJoystickGetDevicePlayerIndex(int device_index)
{
    return device_index;
}

static SDL_JoystickID SDL_AtariJoystickGetDeviceInstanceID(int device_index)
{
    return device_index;
}

static int SDL_AtariJoystickOpen(SDL_Joystick *joystick, int device_index)
{
    joystick->naxes = 2;  /* X and Y axes */
    joystick->nbuttons = 1;  /* Fire button */
    joystick->nhats = 0;
    return 0;
}

static int SDL_AtariJoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    return SDL_Unsupported();
}

static void SDL_AtariJoystickUpdate(SDL_Joystick *joystick)
{
    Uint8 state;
    Sint16 x = 0, y = 0;
    volatile Uint8 *joy_port = (Uint8 *)(joystick->instance_id == 0 ? IKBD_JOY_PORT0 : IKBD_JOY_PORT1);

    /* Read joystick state */
    state = *joy_port;

    /* Convert to axes */
    if (state & JOY_LEFT)
        x = -32768;
    else if (state & JOY_RIGHT)
        x = 32767;
    if (state & JOY_UP)
        y = -32768;
    else if (state & JOY_DOWN)
        y = 32767;

    /* Update axes */
    SDL_PrivateJoystickAxis(joystick, 0, x);
    SDL_PrivateJoystickAxis(joystick, 1, y);

    /* Update button */
    SDL_PrivateJoystickButton(joystick, 0, (state & JOY_FIRE) ? SDL_PRESSED : SDL_RELEASED);
}

static void SDL_AtariJoystickClose(SDL_Joystick *joystick)
{
    /* Nothing to do */
}

static SDL_JoystickDriver SDL_ATARI_JoystickDriver = {
    SDL_AtariJoystickInit,
    SDL_AtariJoystickGetCount,
    SDL_AtariJoystickDetect,
    SDL_AtariJoystickGetDeviceName,
    SDL_AtariJoystickGetDevicePlayerIndex,
    SDL_AtariJoystickGetDeviceInstanceID,
    SDL_AtariJoystickOpen,
    SDL_AtariJoystickRumble,
    NULL,  /* No LED support */
    NULL,  /* No rumble triggers */
    NULL,  /* No send effect */
    SDL_AtariJoystickUpdate,
    SDL_AtariJoystickClose,
    NULL   /* No quit */
};

SDL_JoystickDriver SDL_ATARI_JoystickDriver = {
    .Init = SDL_AtariJoystickInit,
    .GetCount = SDL_AtariJoystickGetCount,
    .Detect = SDL_AtariJoystickDetect,
    .GetDeviceName = SDL_AtariJoystickGetDeviceName,
    .GetDevicePlayerIndex = SDL_AtariJoystickGetDevicePlayerIndex,
    .GetDeviceInstanceID = SDL_AtariJoystickGetDeviceInstanceID,
    .Open = SDL_AtariJoystickOpen,
    .Rumble = SDL_AtariJoystickRumble,
    .Update = SDL_AtariJoystickUpdate,
    .Close = SDL_AtariJoystickClose
};

#endif /* SDL_JOYSTICK_ATARI */
