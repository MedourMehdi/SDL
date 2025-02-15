#ifndef SDL_atarimodel_h_
#define SDL_atarimodel_h_

#include "../../SDL_internal.h"

/* CPU types */
enum {
    ATARI_CPU_UNKNOWN,
    ATARI_CPU_68000,
    ATARI_CPU_68020,
    ATARI_CPU_68030,
    ATARI_CPU_68040,
    ATARI_CPU_68060
};

/* Video hardware types */
enum {
    ATARI_VIDEO_UNKNOWN,
    ATARI_VIDEO_ST,
    ATARI_VIDEO_STE,
    ATARI_VIDEO_TT,
    ATARI_VIDEO_F30
};

/* Hardware information structure */
typedef struct {
    int cpu;            /* CPU type */
    int cpu_freq;       /* CPU frequency */
    int video;          /* Video subsystem */
    int mch;           /* Machine type (_MCH cookie) */
    int pmmu;          /* PMMU available (TT/F30) */
    int blitter;       /* Blitter available */
    int swi;           /* SWI cookie present */
    unsigned long vdo;  /* _VDO cookie */
    unsigned long snd;  /* _SND cookie */
} atari_hw_info;

/* Global variable holding hardware information */
extern atari_hw_info hw_info;

/* Functions */
extern void Atari_DetectHW(void);
extern const char *Atari_GetMachineName(void);

#endif /* SDL_atarimodel_h_ */
