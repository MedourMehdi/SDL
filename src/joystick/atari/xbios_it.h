/* xbios_it.h - XBIOS joystick vector interface */

#ifndef XBIOS_IT_H
#define XBIOS_IT_H

#include <mint/osbind.h>
#include <SDL_stdinc.h>

/*
 * Xbios_joystick — updated by XbiosJoystickVector on every IKBD packet.
 *
 * Bit layout (active-high, from IKBD joystick byte masked with 0x8f):
 *   bit 0 = forward (up)
 *   bit 1 = back    (down)
 *   bit 2 = left
 *   bit 3 = right
 *   bit 7 = fire
 */
extern volatile Uint16 Xbios_joystick;
extern void *old_joy_vec;

/*
 * XbiosInstall   — hook joyvec in _KBDVECS, save old vector.
 *                  Must be called via Supexec().
 * XbiosUninstall — restore original joyvec.
 *                  Must be called via Supexec().
 * XbiosJoystickVector — the handler itself, passed to XbiosInstall.
 */
extern void XbiosInstall(_KBDVECS *kbdvecs, void *newvector);
extern void XbiosUninstall(_KBDVECS *kbdvecs);
extern void XbiosJoystickVector(void);

#endif /* XBIOS_IT_H */