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
#define MCH_TT      0x00020000
#define MCH_F30     0x00030000
#define MCH_MILAN   0x00050000
#define MCH_HADES   0x00040000
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
    hw_info.vdi_type = ATARI_VDI_ROM; /* Assume base ROM VDI initially */
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
        /* Fall back to checking TRAP #2 dispatcher for legacy GDOS */
        if (check_vq_gdos() != -2) {
            hw_info.vdi_type = ATARI_VDI_GDOS;
        }
    }

    /* CPU detection */
    if (Getcookie(C__CPU, &cookie_cpu) == C_FOUND) {
        /* The low WORD contains the CPU type: 0, 10, 20, 30, 40, or 60 */
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

    /* Machine type */
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
                hw_info.hw_type = ATARI_HW_TT;
                hw_info.video = ATARI_VIDEO_TT;
                hw_info.pmmu = 1;
                break;
            case MCH_F30:
                hw_info.hw_type = ATARI_HW_F30;
                hw_info.video = ATARI_VIDEO_F30;
                hw_info.dsp = 1;
                hw_info.blitter = 1;
                break;
            default:
                hw_info.hw_type = ATARI_HW_UNKNOWN;
                break;
        }
    }

    /* Video hardware */
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
        }
    }

    /* Sound hardware */
    if (Getcookie(C__SND, &cookie_snd) == C_FOUND) {
        hw_info.snd = cookie_snd;
    }

    /* Check for PMMU */
    hw_info.pmmu = (hw_info.cpu >= ATARI_CPU_68030) || (hw_info.hw_type == ATARI_HW_TT);

    /* Expansion hardware detection */
    if (Getcookie(C__MIL, &cookie_mil) == C_FOUND) {
        hw_info.hw_type = ATARI_HW_MILAN;
        hw_info.video = ATARI_VIDEO_MILAN;
        hw_info.mch = MCH_MILAN;
    } else if (Getcookie(C_hade, &cookie_hade) == C_FOUND) {
        hw_info.hw_type = ATARI_HW_HADES;
        hw_info.video = ATARI_VIDEO_HADES;
        hw_info.mch = MCH_HADES;
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
    switch(hw_info.video) {
        case ATARI_VIDEO_ST:
            return "ST";
        case ATARI_VIDEO_STE:
            return (hw_info.blitter) ? "STE" : "ST";
        case ATARI_VIDEO_TT:
            return "TT";
        case ATARI_VIDEO_F30:
            return "Falcon";
        case ATARI_VIDEO_MILAN:
            return "Milan";
        case ATARI_VIDEO_HADES:
            return "Hades";
        case ATARI_VIDEO_NOVA:
            return "Nova Video";
        case ATARI_VIDEO_IMAGINE:
            return "Imagine Video";
        default:
            return "Unknown";
    }
}
