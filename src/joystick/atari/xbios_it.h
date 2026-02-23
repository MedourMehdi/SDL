/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* ============================================================================
   xbios_it.h – XBIOS joystick vector interface
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Declares the install/uninstall helpers and the interrupt handler that
   hooks Kbdvbase()->joyvec to capture raw IKBD joystick packets.

   XbiosInstall / XbiosUninstall must be called via Supexec().
   Xbios_joystick is updated at interrupt level; read it from user mode
   without locking (word access is atomic on 68000).

   C90 compliant.
   ============================================================================ */

#ifndef XBIOS_IT_H
#define XBIOS_IT_H

#include <mint/osbind.h>
#include <SDL_stdinc.h>

extern volatile Uint16 Xbios_joystick;
extern void *old_joy_vec;

extern void XbiosInstall(_KBDVECS *kbdvecs, void *newvector);
extern void XbiosUninstall(_KBDVECS *kbdvecs);
extern void XbiosJoystickVector(void);

#endif /* XBIOS_IT_H */