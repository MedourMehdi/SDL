#include "../../SDL_internal.h"
#include "SDL_timer.h"

#ifdef SDL_TIMER_ATARI

#include <mint/mintbind.h>
#include <mint/osbind.h>
#include <mint/sysvars.h>

static Uint32 start_time = 0;
static SDL_bool timer_initialized = SDL_FALSE;

/* Access _hz_200 system variable safely through Supexec */
static long ReadHz200(void)
{
    return Supexec(_hz_200);
}

void SDL_TicksInit(void)
{
    if (!timer_initialized) {
        Vsync();  /* Synchronize with VBL for accurate timing */
        start_time = ReadHz200();
        timer_initialized = SDL_TRUE;
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Timer initialized, start_time=%lu", (unsigned long)start_time);
    }
}

void SDL_TicksQuit(void)
{
    timer_initialized = SDL_FALSE;
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Timer system quit");
}

Uint64 SDL_GetTicks64(void)
{
    if (!timer_initialized) {
        SDL_TicksInit();
    }
    return (Uint64)((ReadHz200() - start_time) * 5);
}

Uint64 SDL_GetPerformanceCounter(void)
{
    if (!timer_initialized) {
        SDL_TicksInit();
    }
    return (Uint64)((ReadHz200() - start_time) * 5000);
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return 1000000; /* Return frequency in microseconds */
}

void SDL_Delay(Uint32 ms)
{
    Uint32 start;
    if (!timer_initialized) {
        SDL_TicksInit();
    }
    
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Delaying for %lu ms", (unsigned long)ms);
    start = SDL_GetTicks();
    while ((SDL_GetTicks() - start) < ms) {
        Vsync();
    }
}

#endif /* SDL_TIMER_ATARI */
