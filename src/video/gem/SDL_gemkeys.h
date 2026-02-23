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
   SDL_gemkeys.h – Atari keyboard mapping public interface
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Declares the three keyboard helper functions used by the GEM event pump:
   scancode mapping, keycode mapping, and modifier state query.

   C90 compliant.
   ============================================================================ */

#ifndef SDL_gemkeys_h_
#define SDL_gemkeys_h_

#include "SDL_scancode.h"
#include "SDL_keycode.h"

extern SDL_Scancode ATARI_MapScancode(int scancode);
extern SDL_Keycode ATARI_MapKey(int scancode);
extern Uint16 ATARI_ModState(void);

#endif /* SDL_gemkeys_h_ */