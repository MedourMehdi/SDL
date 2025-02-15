#include "SDL_atarimodel.h"
#include <mint/cookie.h>
#include <mint/osbind.h>

/* Cookie _MCH definitions */
#define MCH_ST      0x00000000
#define MCH_STE     0x00010000
#define MCH_TT      0x00020000
#define MCH_F30     0x00030000
#define MCH_MASK    0xFFFF0000

/* Video hardware cookie values */
#define VDO_ST     0x0000
#define VDO_STE    0x0001
#define VDO_TT     0x0002
#define VDO_F30    0x0003

atari_hw_info hw_info;

void Atari_DetectHW(void)
{
    long cookie_cpu = 0;
    long cookie_vdo = 0;
    long cookie_snd = 0;
    long cookie_mch = 0;
    long cookie_swi = 0;

    /* Clear hardware info */
    SDL_memset(&hw_info, 0, sizeof(hw_info));

    /* CPU type */
    if (Getcookie(C__CPU, &cookie_cpu) == C_FOUND) {
        switch (cookie_cpu) {
            case 0:
                hw_info.cpu = ATARI_CPU_68000;
                break;
            case 20:
                hw_info.cpu = ATARI_CPU_68020;
                break;
            case 30:
                hw_info.cpu = ATARI_CPU_68030;
                break;
            case 40:
                hw_info.cpu = ATARI_CPU_68040;
                break;
            case 60:
                hw_info.cpu = ATARI_CPU_68060;
                break;
            default:
                hw_info.cpu = ATARI_CPU_UNKNOWN;
                break;
        }
    } else {
        hw_info.cpu = ATARI_CPU_68000;
    }

    /* Machine type */
    if (Getcookie(C__MCH, &cookie_mch) == C_FOUND) {
        hw_info.mch = cookie_mch;
    }

    /* Video hardware */
    if (Getcookie(C__VDO, &cookie_vdo) == C_FOUND) {
        hw_info.vdo = cookie_vdo;
        switch (cookie_vdo) {
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
            default:
                hw_info.video = ATARI_VIDEO_UNKNOWN;
                break;
        }
    } else {
        hw_info.video = ATARI_VIDEO_ST;
    }

    /* Sound hardware */
    if (Getcookie(C__SND, &cookie_snd) == C_FOUND) {
        hw_info.snd = cookie_snd;
    }

    /* Check for PMMU */
    hw_info.pmmu = (hw_info.cpu >= ATARI_CPU_68030);

    /* Check for blitter */
    hw_info.blitter = ((cookie_mch == MCH_STE) || 
                      (cookie_mch == MCH_TT) || 
                      (cookie_mch == MCH_F30));

    /* Check for SWI */
    if (Getcookie(C__SWI, &cookie_swi) == C_FOUND) {
        hw_info.swi = 1;
    }
}

const char *Atari_GetMachineName(void)
{
    switch (hw_info.video) {
        case ATARI_VIDEO_ST:
            return "ST";
        case ATARI_VIDEO_STE:
            return "STE";
        case ATARI_VIDEO_TT:
            return "TT";
        case ATARI_VIDEO_F30:
            return "Falcon";
        default:
            return "Unknown";
    }
}
