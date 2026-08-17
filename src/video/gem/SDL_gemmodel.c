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
   SDL_gemmodel.c – Atari hardware detection
   Medour Mehdi - 2026
   Architecture: Motorola 68000 / Atari ST-TT-Falcon

   Reads the BIOS cookie jar (_CPU, _MCH, _VDO, _SND, _MIL, hade, NOVA, IMNE)
   to populate the global hw_info structure at VideoInit time.
   Atari_GetMachineName() for logging.

   C90 compliant.
   ============================================================================ */
#include "SDL_gemmodel.h"
#include <mint/cookie.h>
#include <mint/osbind.h>
#include <mint/sysvars.h>

/* Cookie definitions */
#define MCH_ST      0x00000000
#define MCH_STE     0x00010000
#define MCH_TT      0x00020000   /* TT, Hades, Medusa T40 */
#define MCH_F30     0x00030000   /* Falcon030 / Sparrow */
#define MCH_MILAN   0x00040000   /* Milan */
#define MCH_ARANYM  0x00050000   /* ARAnyM >= 0.8.5beta */
#define MCH_MASK    0xFFFF0000

#define VDO_ST      0x0000
#define VDO_STE     0x0001
#define VDO_TT      0x0002
#define VDO_F30     0x0003
#define VDO_NOVA    0x0010
#define VDO_IMAGINE 0x0011

/* VDI Cookies */
#define C_NVDI      0x4E564449L /* 'NVDI' */
#define C_fVDI      0x66564449L /* 'fVDI' */

/* Standard BIOS cookies – define explicitly in case mint/cookie.h
   is incomplete or uses wrong values on some toolchains. */
#ifndef C__CPU
#define C__CPU      0x5F435055L /* '_CPU' */
#endif
#ifndef C__VDO
#define C__VDO      0x5F56444FL /* '_VDO' */
#endif
#ifndef C__MCH
#define C__MCH      0x5F4D4348L /* '_MCH' */
#endif
#ifndef C__SND
#define C__SND      0x5F534E44L /* '_SND' */
#endif
#ifndef C__MIL
#define C__MIL      0x5F4D494CL /* '_MIL' */
#endif

atari_hw_info hw_info;

/* Assembly helper to check for legacy GDOS via TRAP #2 */
static long check_vq_gdos(void)
{
    long ret;
    __asm__ volatile (
        "move.w #-2, d0\n\t"
        "trap #2\n\t"
        "move.l d0, %0"
        : "=r"(ret)
        :
        : "d0", "d1", "d2", "a0", "a1", "a2"
    );
    return ret;
}

void Atari_DetectHW(void)
{
    long cookie_mil = 0, cookie_hade = 0, cookie_nova = 0, cookie_imne = 0;
    long cookie_cpu = 0, cookie_vdo = 0, cookie_snd = 0, cookie_mch = 0;
    long cookie_vdi = 0;

    /* Initialize to unknown */
    hw_info.cpu = ATARI_CPU_UNKNOWN;
    hw_info.video = ATARI_VIDEO_ST;
    hw_info.hw_type = ATARI_HW_UNKNOWN;
    hw_info.vdi_type = ATARI_VDI_ROM;
    hw_info.mch = 0;
    hw_info.pmmu = 0;
    hw_info.blitter = 0;
    hw_info.dsp = 0;
    hw_info.vdo = 0;
    hw_info.snd = 0;

    /* VDI Detection (NVDI / fVDI / GDOS) */
    if (Getcookie(C_NVDI, &cookie_vdi) == C_FOUND) {
        hw_info.vdi_type = ATARI_VDI_NVDI;
    } else if (Getcookie(C_fVDI, &cookie_vdi) == C_FOUND) {
        hw_info.vdi_type = ATARI_VDI_FVDI;
    } else {
        if (check_vq_gdos() != -2) {
            hw_info.vdi_type = ATARI_VDI_GDOS;
        }
    }

    /* CPU detection */
    if (Getcookie(C__CPU, &cookie_cpu) == C_FOUND) {
        int cpu_type = (int)(cookie_cpu & 0xFFFF);

        if (cpu_type >= 60) {
            hw_info.cpu = ATARI_CPU_68060;
        } else if (cpu_type >= 40) {
            hw_info.cpu = ATARI_CPU_68040;
        } else if (cpu_type >= 30) {
            hw_info.cpu = ATARI_CPU_68030;
        } else if (cpu_type >= 20) {
            hw_info.cpu = ATARI_CPU_68020;
        } else if (cpu_type >= 10) {
            hw_info.cpu = ATARI_CPU_68010;
        } else {
            hw_info.cpu = ATARI_CPU_68000;
        }
    }

    /* ================================================================
       VIDEO HARDWARE (_VDO) – detect BEFORE _MCH so we have a reliable
       fallback if the _MCH cookie is missing or unrecognised.
       ================================================================ */
    if (Getcookie(C__VDO, &cookie_vdo) == C_FOUND) {
        hw_info.vdo = cookie_vdo;
        switch (cookie_vdo >> 16) {
            case VDO_ST:
                hw_info.video = ATARI_VIDEO_ST;
                break;
            case VDO_STE:
                hw_info.video = ATARI_VIDEO_STE;
                break;
            case VDO_TT:
                hw_info.video = ATARI_VIDEO_TT;
                break;
            case VDO_F30:
                hw_info.video = ATARI_VIDEO_F30;
                break;
            case VDO_NOVA:
                hw_info.video = ATARI_VIDEO_NOVA;
                break;
            case VDO_IMAGINE:
                hw_info.video = ATARI_VIDEO_IMAGINE;
                break;
        }
    }

    /* ================================================================
       MACHINE TYPE (_MCH) – 16-bit upper word = machine family.
       Note: Hades shares MCH_TT (0x0002). It is distinguished by the
       'hade' cookie, detected below.
       ================================================================ */
    if (Getcookie(C__MCH, &cookie_mch) == C_FOUND) {
        hw_info.mch = cookie_mch;
        switch (cookie_mch & MCH_MASK) {
            case MCH_ST:
                hw_info.hw_type = ATARI_HW_ST;
                hw_info.video = ATARI_VIDEO_ST;
                break;
            case MCH_STE:
                hw_info.hw_type = ATARI_HW_STE;
                hw_info.video = ATARI_VIDEO_STE;
                hw_info.blitter = 1;
                break;
            case MCH_TT:
                /* Could be TT, Hades, or Medusa. Don't override video yet. */
                hw_info.hw_type = ATARI_HW_TT;
                hw_info.pmmu = 1;
                break;
            case MCH_F30:
                hw_info.hw_type = ATARI_HW_F30;
                hw_info.video = ATARI_VIDEO_F30;
                hw_info.dsp = 1;
                hw_info.blitter = 1;
                break;
            case MCH_MILAN:
                hw_info.hw_type = ATARI_HW_MILAN;
                hw_info.video = ATARI_VIDEO_MILAN;
                break;
            case MCH_ARANYM:
                /* ARAnyM emulates various machines; keep video as-is */
                hw_info.hw_type = ATARI_HW_UNKNOWN; /* or add ATARI_HW_ARANYM */
                break;
            default:
                /* Unknown _MCH value – keep hw_type UNKNOWN, fallback
                   from _VDO below will catch it. */
                break;
        }
    }

    /* ================================================================
       FALLBACK: if _MCH was missing or unknown, derive hw_type from
       the _VDO video type we already detected.
       ================================================================ */
    if (hw_info.hw_type == ATARI_HW_UNKNOWN) {
        switch (hw_info.video) {
            case ATARI_VIDEO_ST:      hw_info.hw_type = ATARI_HW_ST;      break;
            case ATARI_VIDEO_STE:     hw_info.hw_type = ATARI_HW_STE;     break;
            case ATARI_VIDEO_TT:      hw_info.hw_type = ATARI_HW_TT;      break;
            case ATARI_VIDEO_F30:     hw_info.hw_type = ATARI_HW_F30;     break;
            case ATARI_VIDEO_MILAN:   hw_info.hw_type = ATARI_HW_MILAN;   break;
            case ATARI_VIDEO_HADES:   hw_info.hw_type = ATARI_HW_HADES;   break;
            case ATARI_VIDEO_NOVA:    hw_info.hw_type = ATARI_HW_NOVA;    break;
            case ATARI_VIDEO_IMAGINE: hw_info.hw_type = ATARI_HW_IMAGINE; break;
            default: break;
        }
    }

    /* Sound hardware */
    if (Getcookie(C__SND, &cookie_snd) == C_FOUND) {
        hw_info.snd = cookie_snd;
    }

    /* PMMU: 68030+ always has PMMU; TT always has PMMU */
    hw_info.pmmu = (hw_info.cpu >= ATARI_CPU_68030) || (hw_info.hw_type == ATARI_HW_TT);

    /* ================================================================
       EXPANSION HARDWARE – these OVERRIDE _MCH/_VDO because they are
       more specific (add-on video cards, accelerator boards, etc.)
       ================================================================ */
    if (Getcookie(C__MIL, &cookie_mil) == C_FOUND) {
        hw_info.hw_type = ATARI_HW_MILAN;
        hw_info.video = ATARI_VIDEO_MILAN;
        hw_info.mch = MCH_MILAN;
    } else if (Getcookie(C_hade, &cookie_hade) == C_FOUND) {
        hw_info.hw_type = ATARI_HW_HADES;
        hw_info.video = ATARI_VIDEO_HADES;
        /* Hades shares MCH_TT (0x0002) with the TT */
    } else if (Getcookie(C_NOVA, &cookie_nova) == C_FOUND) {
        hw_info.hw_type = ATARI_HW_NOVA;
        hw_info.video = ATARI_VIDEO_NOVA;
    } else if (Getcookie(C_IMNE, &cookie_imne) == C_FOUND) {
        hw_info.hw_type = ATARI_HW_IMAGINE;
        hw_info.video = ATARI_VIDEO_IMAGINE;
    }
}

const char *Atari_GetMachineName(void)
{
    /* Prefer hw_type (machine family) over video (display hardware) */
    switch (hw_info.hw_type) {
        case ATARI_HW_ST:      return "ST";
        case ATARI_HW_STE:     return (hw_info.blitter) ? "STE" : "ST";
        case ATARI_HW_TT:      return "TT";
        case ATARI_HW_F30:     return "Falcon";
        case ATARI_HW_MILAN:   return "Milan";
        case ATARI_HW_HADES:   return "Hades";
        case ATARI_HW_NOVA:    return "Nova Video";
        case ATARI_HW_IMAGINE: return "Imagine Video";
        default:
            /* Fallback to video type if hw_type is still unknown */
            switch (hw_info.video) {
                case ATARI_VIDEO_ST:  return "ST";
                case ATARI_VIDEO_STE: return "STE";
                case ATARI_VIDEO_TT:  return "TT";
                case ATARI_VIDEO_F30: return "Falcon";
                default:              return "Unknown";
            }
    }
}
