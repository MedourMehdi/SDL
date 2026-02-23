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
   SDL_gemmodel.h – Atari hardware description types and interface
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Defines CPU type, video hardware and machine type enumerations, the
   atari_hw_info structure, memory allocation flags, and the three public
   functions exposed by SDL_gemmodel.c.

   C90 compliant.
   ============================================================================ */

#ifndef SDL_atarimodel_h_
#define SDL_atarimodel_h_

#include "../../SDL_internal.h"

/* CPU types */
enum {
    ATARI_CPU_UNKNOWN = -1,
    ATARI_CPU_68000,
    ATARI_CPU_68010,
    ATARI_CPU_68020,
    ATARI_CPU_68030,
    ATARI_CPU_68040,
    ATARI_CPU_68060
};

/* Video hardware types */
enum {
    ATARI_VIDEO_ST,
    ATARI_VIDEO_STE,
    ATARI_VIDEO_TT,
    ATARI_VIDEO_F30,
    ATARI_VIDEO_MILAN,
    ATARI_VIDEO_HADES,
    ATARI_VIDEO_NOVA,
    ATARI_VIDEO_IMAGINE
};

/* Hardware types for driver selection */
typedef enum {
    ATARI_HW_UNKNOWN = -1,
    ATARI_HW_ST,
    ATARI_HW_STE,
    ATARI_HW_TT,
    ATARI_HW_F30,
    ATARI_HW_MILAN,
    ATARI_HW_HADES,
    ATARI_HW_NOVA,
    ATARI_HW_IMAGINE
} AtariHardwareType;

/* Hardware information structure */
typedef struct {
    int cpu;
    int video;
    unsigned long mch;
    int pmmu;
    int blitter;
    int dsp;
    unsigned long vdo;
    unsigned long snd;
    AtariHardwareType hw_type;
} atari_hw_info;

/* Global hardware info */
extern atari_hw_info hw_info;

/* Memory allocation types */
// #define MX_STRAM 0x0000
// #define MX_TTRAM 0x0001
#define MX_PREFER_STRAM 0x0002
#define MX_PREFER_TTRAM 0x0003

/* Functions */
extern void Atari_DetectHW(void);
extern const char *Atari_GetMachineName(void);

#endif /* SDL_atarimodel_h_ */