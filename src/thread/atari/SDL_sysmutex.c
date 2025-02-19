#include "../../SDL_internal.h"
#include "SDL_thread.h"

#ifdef SDL_THREAD_ATARI

#include <mint/mintbind.h>

struct SDL_mutex {
    long handle;
    SDL_threadID owner;
    int count;
    int32_t id;
};

SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *mutex = (SDL_mutex *)Mxalloc(sizeof(*mutex), MX_PREFTTRAM);
    if (mutex) {
        static int32_t mutex_count = 0;
        mutex->id = ++mutex_count;
        mutex->handle = Psemaphore(0, mutex->id, 1);
        mutex->owner = 0;
        mutex->count = 0;
        
        if (mutex->handle < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Mutex creation failed with error %ld", mutex->handle);
            Mfree(mutex);
            return NULL;
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Created mutex id=%d handle=%ld", mutex->id, mutex->handle);
    }
    return mutex;
}

void SDL_DestroyMutex(SDL_mutex *mutex)
{
    if (mutex) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Destroying mutex id=%d", mutex->id);
        Psemaphore(1, mutex->handle, 0);
        Mfree(mutex);
    }
}

int SDL_LockMutex(SDL_mutex *mutex)
{
    SDL_threadID this_thread;
    long result;
    
    if (!mutex) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Attempt to lock NULL mutex");
        return -1;
    }
    
    this_thread = SDL_ThreadID();
    if (mutex->owner == this_thread) {
        mutex->count++;
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Recursive mutex lock id=%d count=%d", mutex->id, mutex->count);
        return 0;
    }
    
    result = Psemaphore(2, mutex->handle, -1);
    if (result >= 0) {
        mutex->owner = this_thread;
        mutex->count = 1;
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Locked mutex id=%d", mutex->id);
        return 0;
    }
    
    SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Failed to lock mutex id=%d error=%ld", mutex->id, result);
    return -1;
}

int SDL_UnlockMutex(SDL_mutex *mutex)
{
    long result;
    
    if (!mutex) {
        return -1;
    }
    
    if (mutex->owner == SDL_ThreadID()) {
        mutex->count--;
        if (mutex->count == 0) {
            mutex->owner = 0;
            result = Psemaphore(3, mutex->handle, 1);
            if (result < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Failed to unlock mutex id=%d error=%ld", mutex->id, result);
                return -1;
            }
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Unlocked mutex id=%d", mutex->id);
        }
        return 0;
    }
    
    SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Attempt to unlock mutex not owned by current thread");
    return -1;
}

#endif /* SDL_THREAD_ATARI */
