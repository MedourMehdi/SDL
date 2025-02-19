#include "../../SDL_internal.h"
#include "SDL_thread.h"
#include "../SDL_thread_c.h"

#ifdef SDL_THREAD_ATARI

#include <mint/mintbind.h>

typedef struct {
    SDL_threadID thread_id;
    SDL_TLSData *tls_data;
    char *thread_name;
} TLSEntry;

#define MAX_TLS_ENTRIES 64
static TLSEntry tls_entries[MAX_TLS_ENTRIES];
static int num_tls_entries = 0;

void SDL_SYS_InitTLSData(void)
{
    num_tls_entries = 0;
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: TLS system initialized");
}

SDL_TLSData *SDL_SYS_GetTLSData(void)
{
    SDL_threadID current = SDL_ThreadID();
    int i;
    
    for (i = 0; i < num_tls_entries; i++) {
        if (tls_entries[i].thread_id == current) {
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Retrieved TLS data for thread %lu", (unsigned long)current);
            return tls_entries[i].tls_data;
        }
    }
    
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: No TLS data found for thread %lu", (unsigned long)current);
    return NULL;
}

int SDL_SYS_SetTLSData(SDL_TLSData *data)
{
    SDL_threadID current = SDL_ThreadID();
    int i;
    
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Setting TLS data for thread %lu", (unsigned long)current);
    
    for (i = 0; i < num_tls_entries; i++) {
        if (tls_entries[i].thread_id == current) {
            tls_entries[i].tls_data = data;
            return 0;
        }
    }
    
    if (num_tls_entries < MAX_TLS_ENTRIES) {
        tls_entries[num_tls_entries].thread_id = current;
        tls_entries[num_tls_entries].tls_data = data;
        tls_entries[num_tls_entries].thread_name = NULL;
        num_tls_entries++;
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Created new TLS entry, total=%d", num_tls_entries);
        return 0;
    }
    
    SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Atari: TLS entries table full (max=%d)", MAX_TLS_ENTRIES);
    return -1;
}

void SDL_SYS_SetupThread(const char *name)
{
    SDL_threadID current = SDL_ThreadID();
    int i;
    
    for (i = 0; i < num_tls_entries; i++) {
        if (tls_entries[i].thread_id == current) {
            tls_entries[i].thread_name = name ? SDL_strdup(name) : NULL;
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: Set thread name to '%s'", name ? name : "NULL");
            break;
        }
    }
}

void SDL_SYS_QuitTLSData(void)
{
    int i;
    for (i = 0; i < num_tls_entries; i++) {
        SDL_free(tls_entries[i].thread_name);
    }
    num_tls_entries = 0;
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Atari: TLS system cleaned up");
}

#endif /* SDL_THREAD_ATARI */
