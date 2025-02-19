#include "../../SDL_internal.h"
#include "SDL_thread.h"
#include "../SDL_thread_c.h"
#include "SDL_systhread_c.h"

#ifdef SDL_THREAD_ATARI

#include <mint/mintbind.h>
#include <mint/ssystem.h>

#define THREAD_STACK_SIZE (64 * 1024)  /* 64KB stack size */

typedef struct {
    SDL_Thread *thread;
    void *stack;
    void *args;
} ThreadData;

static void *AllocThreadStack(void)
{
    void *stack = (void*)Mxalloc(THREAD_STACK_SIZE, MX_PREFTTRAM | MX_PRIVATE);
    if (stack) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Allocated thread stack at %p", stack);
    }
    return stack;
}

static long RunThread(void *arg)
{
    ThreadData *data = (ThreadData *)arg;
    SDL_Thread *thread = data->thread;
    void *stack = data->stack;
    
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Thread %lu starting", (unsigned long)Pgetpid());
    
    Mfree(data);
    SDL_RunThread(thread);
    
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Thread %lu exiting", (unsigned long)Pgetpid());
    Mfree(stack);
    return 0;
}

int SDL_SYS_CreateThread(SDL_Thread *thread, void *args)
{
    void *stack;
    ThreadData *data;
    long pid;
    
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Creating new thread");
    
    stack = AllocThreadStack();
    if (!stack) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Failed to allocate thread stack");
        return -1;
    }
    
    data = (ThreadData *)Mxalloc(sizeof(ThreadData), MX_PREFTTRAM);
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Failed to allocate thread data");
        Mfree(stack);
        return -1;
    }
    
    data->thread = thread;
    data->stack = stack;
    data->args = args;
    
    pid = Pexec(104, (void *)RunThread, data, stack);
    if (pid < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Thread creation failed: %ld", pid);
        Mfree(stack);
        Mfree(data);
        return -1;
    }
    
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Created thread with PID %ld", pid);
    thread->handle = (SYS_ThreadHandle)pid;
    return 0;
}

void SDL_SYS_WaitThread(SDL_Thread *thread)
{
    long pid = (long)thread->handle;
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Waiting for thread %ld", pid);
    
    while (Pkill(pid, 0) == 0) {
        Vsync();
    }
}

void SDL_SYS_DetachThread(SDL_Thread *thread)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Detaching thread %ld", (long)thread->handle);
}

SDL_threadID SDL_ThreadID(void)
{
    return (SDL_threadID)Pgetpid();
}

int SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
    int prio;
    long result;
    
    switch (priority) {
        case SDL_THREAD_PRIORITY_LOW:
            prio = 0;
            break;
        case SDL_THREAD_PRIORITY_NORMAL:
            prio = 16;
            break;
        case SDL_THREAD_PRIORITY_HIGH:
            prio = 31;
            break;
        default:
            return -1;
    }
    
    result = Psetpriority(0, 0, prio);
    if (result < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: Failed to set thread priority: %ld", result);
        return -1;
    }
    
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Set thread priority to %d", prio);
    return 0;
}

#endif /* SDL_THREAD_ATARI */
