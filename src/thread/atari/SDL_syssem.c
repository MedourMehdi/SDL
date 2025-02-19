#include "../../SDL_internal.h"
#include "SDL_thread.h"
#include "SDL_timer.h"

#ifdef SDL_THREAD_ATARI

#include <mint/mintbind.h>
#include <mint/ssystem.h>

struct SDL_semaphore {
    int32_t id;
    int32_t initial_value;
};

static SDL_bool mint_available = SDL_FALSE;

static void InitMiNT(void)
{
    long mint_version = Sversion();
    mint_available = (mint_version >= 0x1000);
    
    if (mint_available) {
        if (Pdomain(1) == 0) {
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Switched to UNIX domain");
        }
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: MiNT %s (version 0x%lx)", 
                 mint_available ? "detected" : "not available", mint_version);
}

SDL_sem *SDL_CreateSemaphore(Uint32 initial_value)
{
    static int32_t sem_count = 0;
    SDL_sem *sem;
    long result;

    if (!mint_available) {
        InitMiNT();
    }

    sem = (SDL_sem *)Mxalloc(sizeof(*sem), MX_PREFTTRAM);
    if (!sem) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Failed to allocate semaphore");
        return NULL;
    }

    sem->id = ++sem_count;
    sem->initial_value = initial_value;

    result = Psemaphore(0, sem->id, initial_value);
    if (result < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Semaphore creation failed: %ld", result);
        Mfree(sem);
        return NULL;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Created semaphore id=%d value=%u", sem->id, initial_value);
    return sem;
}

void SDL_DestroySemaphore(SDL_sem *sem)
{
    if (sem) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Destroying semaphore id=%d", sem->id);
        Psemaphore(1, sem->id, 0);
        Mfree(sem);
    }
}

int SDL_SemWait(SDL_sem *sem)
{
    long result;
    
    if (!sem) {
        return -1;
    }
    
    result = Psemaphore(2, sem->id, -1);
    if (result < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Semaphore wait failed: %ld", result);
        return -1;
    }
    return 0;
}

int SDL_SemTryWait(SDL_sem *sem)
{
    if (!sem) {
        return -1;
    }
    return (Psemaphore(2, sem->id, 0) < 0) ? SDL_MUTEX_TIMEDOUT : 0;
}

int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 timeout)
{
    Uint32 start;
    long result;
    
    if (!sem) {
        return -1;
    }
    
    start = SDL_GetTicks();
    while ((result = Psemaphore(2, sem->id, -1)) < 0) {
        if ((SDL_GetTicks() - start) >= timeout) {
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Semaphore wait timeout");
            return SDL_MUTEX_TIMEDOUT;
        }
        Vsync();
    }
    return 0;
}

int SDL_SemPost(SDL_sem *sem)
{
    long result;
    
    if (!sem) {
        return -1;
    }
    
    result = Psemaphore(3, sem->id, 1);
    if (result < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Semaphore post failed: %ld", result);
        return -1;
    }
    return 0;
}

Uint32 SDL_SemValue(SDL_sem *sem)
{
    if (!sem) {
        return 0;
    }
    return (Uint32)sem->initial_value;
}

#endif /* SDL_THREAD_ATARI */
