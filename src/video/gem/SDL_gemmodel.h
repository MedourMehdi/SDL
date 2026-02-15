/* ============================================ *
 *  SDL_atarimodel.h                            *
 * ============================================ */
#ifndef SDL_atarimodel_h_
#define SDL_atarimodel_h_

#include "../../SDL_internal.h"

/* CPU types */

/*
0 	= 	MC-68000
10 	= 	MC-68010
20 	= 	MC-68020
30 	= 	MC-68030
40 	= 	MC-68040
60 	= 	MC-68060
*/
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
extern void *Atari_SysMalloc(unsigned long size, unsigned short alloc_type);

#endif /* SDL_atarimodel_h_ */