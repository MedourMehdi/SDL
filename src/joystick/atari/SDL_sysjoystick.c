/*
 * SDL_sysjoystick.c — Atari ST/STE/TT/Falcon joystick driver for SDL2
 *
 * Uses the XBIOS joystick vector (Kbdvbase()->joyvec) to receive
 * IKBD joystick packets asynchronously. The vector handler writes
 * Xbios_joystick; Update() reads it with no supervisor overhead.
 *
 * IKBD joystick byte bit layout (port 1, active-high):
 *   bit 0 = forward (up)
 *   bit 1 = back    (down)
 *   bit 2 = left
 *   bit 3 = right
 *   bit 7 = fire
 */

#include "../../SDL_internal.h"

#ifdef SDL_JOYSTICK_ATARI

#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"
#include "SDL_events.h"
#include "SDL_log.h"

#include <mint/osbind.h>
#include "xbios_it.h"

/* -----------------------------------------------------------------------
 * IKBD commands
 * 0x14 = enable joystick event reporting (mandatory — IKBD is silent by default)
 * 0x08 = restore relative mouse mode on quit
 * ----------------------------------------------------------------------- */
static const char ikbd_joy_enable[]    = { 0x14 };
static const char ikbd_mouse_restore[] = { 0x08 };

/* -----------------------------------------------------------------------
 * Driver state
 * ----------------------------------------------------------------------- */
static _KBDVECS *kbdvecs     = NULL;
static SDL_bool  xbios_active = SDL_FALSE;
static Uint16    prev_state   = 0;

/* Supexec wrappers — these run in supervisor mode */
static long DoInstall(void)
{
    kbdvecs = (_KBDVECS *)Kbdvbase();
    if (kbdvecs)
        XbiosInstall(kbdvecs, XbiosJoystickVector);
    return 0;
}

static long DoUninstall(void)
{
    if (kbdvecs) {
        XbiosUninstall(kbdvecs);
        kbdvecs = NULL;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Init
 * ----------------------------------------------------------------------- */
static int ATARI_JoystickInit(void)
{
    Supexec(DoInstall);

    if (!kbdvecs) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT,
            "ATARI: Kbdvbase() returned NULL — no joystick support");
        return 0;
    }

    /*
     * Tell the IKBD to start sending joystick event packets.
     * Without this command the IKBD never sends 0xFD packets
     * and Xbios_joystick is never updated.
     */
    // Ikbdws(0, (char *)ikbd_joy_enable);

    Xbios_joystick = 0;
    prev_state     = 0;
    xbios_active   = SDL_TRUE;

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "ATARI: XBIOS joystick handler installed");
    return 1; /* one device */
}

/* -----------------------------------------------------------------------
 * Device info
 * ----------------------------------------------------------------------- */
static int ATARI_JoystickGetCount(void)
{
    return xbios_active ? 1 : 0;
}

static void ATARI_JoystickDetect(void) {}

static const char *ATARI_JoystickGetDeviceName(int device_index)
{
    return (device_index == 0 && xbios_active) ? "Atari Joystick" : NULL;
}

static const char *ATARI_JoystickGetDevicePath(int device_index)
{
    (void)device_index; return NULL;
}

static int ATARI_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    (void)device_index; return -1;
}

static int ATARI_JoystickGetDevicePlayerIndex(int device_index)
{
    (void)device_index; return 0;
}

static void ATARI_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
    (void)device_index; (void)player_index;
}

static SDL_JoystickGUID ATARI_JoystickGetDeviceGUID(int device_index)
{
    return SDL_CreateJoystickGUIDForName(
        ATARI_JoystickGetDeviceName(device_index));
}

static SDL_JoystickID ATARI_JoystickGetDeviceInstanceID(int device_index)
{
    return (device_index == 0 && xbios_active) ? 0 : -1;
}

/* -----------------------------------------------------------------------
 * Open
 * ----------------------------------------------------------------------- */
static int ATARI_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    if (device_index != 0 || !xbios_active)
        return SDL_SetError("ATARI: invalid device_index %d", device_index);

    joystick->naxes    = 0;
    joystick->nhats    = 1;
    joystick->nballs   = 0;
    joystick->nbuttons = 1;

    joystick->hwdata = (struct joystick_hwdata *)SDL_calloc(1, 1);
    if (!joystick->hwdata)
        return SDL_OutOfMemory();

    return 0;
}

/* -----------------------------------------------------------------------
 * Update
 *
 * Xbios_joystick is written by XbiosJoystickVector at interrupt level.
 * Bit layout after the handler's andw #0x8f:
 *   bits 0-3 = directions (active-high: 1 = pressed)
 *   bit  7   = fire       (active-high: 1 = pressed)
 * ----------------------------------------------------------------------- */
static void ATARI_JoystickUpdate(SDL_Joystick *joystick)
{
    Uint16 cur;
    Uint8  hat;

    if (!joystick || !xbios_active) return;

    cur = Xbios_joystick & 0x8f;
    
    if (cur == prev_state) return;

    /* Hat */
    hat = SDL_HAT_CENTERED;
    if (cur & (1<<0)) hat |= SDL_HAT_UP;
    if (cur & (1<<1)) hat |= SDL_HAT_DOWN;
    if (cur & (1<<2)) hat |= SDL_HAT_LEFT;
    if (cur & (1<<3)) hat |= SDL_HAT_RIGHT;

    if ((cur & 0x0f) != (prev_state & 0x0f))
        SDL_PrivateJoystickHat(joystick, 0, hat);

    /* Fire button */
    if ((cur & 0x80) != (prev_state & 0x80))
        SDL_PrivateJoystickButton(joystick, 0,
            (cur & 0x80) ? SDL_PRESSED : SDL_RELEASED);

    prev_state = cur;

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT,
        "ATARI: joy=0x%02x hat=%02x fire=%d",
        (unsigned)cur, (unsigned)hat, (cur & 0x80) ? 1 : 0);
}

/* -----------------------------------------------------------------------
 * Close / Quit
 * ----------------------------------------------------------------------- */
static void ATARI_JoystickClose(SDL_Joystick *joystick)
{
    SDL_free(joystick->hwdata);
    joystick->hwdata = NULL;
}

static void ATARI_JoystickQuit(void)
{
    if (!xbios_active) return;

    /* Restore mouse before unhooking — IKBD may be in joystick-only mode */
    // Ikbdws(0, (char *)ikbd_mouse_restore);

    Supexec(DoUninstall);

    xbios_active = SDL_FALSE;
    prev_state   = 0;

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "ATARI: XBIOS handler uninstalled");
}

/* -----------------------------------------------------------------------
 * Unsupported
 * ----------------------------------------------------------------------- */
static int    ATARI_JoystickRumble(SDL_Joystick *j, Uint16 a, Uint16 b)         { (void)j;(void)a;(void)b; return SDL_Unsupported(); }
static int    ATARI_JoystickRumbleTriggers(SDL_Joystick *j, Uint16 a, Uint16 b) { (void)j;(void)a;(void)b; return SDL_Unsupported(); }
static Uint32 ATARI_JoystickGetCapabilities(SDL_Joystick *j)                    { (void)j; return 0; }
static int    ATARI_JoystickSetLED(SDL_Joystick *j, Uint8 r, Uint8 g, Uint8 b) { (void)j;(void)r;(void)g;(void)b; return SDL_Unsupported(); }
static int    ATARI_JoystickSendEffect(SDL_Joystick *j, const void *d, int s)  { (void)j;(void)d;(void)s; return SDL_Unsupported(); }
static int    ATARI_JoystickSetSensorsEnabled(SDL_Joystick *j, SDL_bool e)     { (void)j;(void)e; return SDL_Unsupported(); }
static SDL_bool ATARI_JoystickGetGamepadMapping(int i, SDL_GamepadMapping *o)  { (void)i;(void)o; return SDL_FALSE; }

/* -----------------------------------------------------------------------
 * Driver registration
 * ----------------------------------------------------------------------- */
SDL_JoystickDriver SDL_ATARI_JoystickDriver = {
    ATARI_JoystickInit,
    ATARI_JoystickGetCount,
    ATARI_JoystickDetect,
    ATARI_JoystickGetDeviceName,
    ATARI_JoystickGetDevicePath,
    ATARI_JoystickGetDeviceSteamVirtualGamepadSlot,
    ATARI_JoystickGetDevicePlayerIndex,
    ATARI_JoystickSetDevicePlayerIndex,
    ATARI_JoystickGetDeviceGUID,
    ATARI_JoystickGetDeviceInstanceID,
    ATARI_JoystickOpen,
    ATARI_JoystickRumble,
    ATARI_JoystickRumbleTriggers,
    ATARI_JoystickGetCapabilities,
    ATARI_JoystickSetLED,
    ATARI_JoystickSendEffect,
    ATARI_JoystickSetSensorsEnabled,
    ATARI_JoystickUpdate,
    ATARI_JoystickClose,
    ATARI_JoystickQuit,
    ATARI_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_ATARI */