/*
 * Implementation for Posix compliant platforms
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#include "equeue_platform.h"

#if defined(EQUEUE_PLATFORM_PICO)
#include<hardware/timer.h>

// Tick operations
unsigned equeue_tick(void) {
    return time_us_32() / 1000;
}


// Mutex operations
int equeue_mutex_create(equeue_mutex_t *m) {
    critical_section_init(m);
    return 0;
}

void equeue_mutex_destroy(equeue_mutex_t *m) {
    critical_section_deinit(m);
}

void equeue_mutex_lock(equeue_mutex_t *m) {
    critical_section_enter_blocking(m);
}

void equeue_mutex_unlock(equeue_mutex_t *m) {
    critical_section_exit(m);
}


// Semaphore operations
// acquires decrease the permits, releases increment
int equeue_sema_create(equeue_sema_t *s) {
    sem_init(s, 1,1);
    return 0;
}

void equeue_sema_destroy(equeue_sema_t *s) {
    sem_reset(s,1);
}

void equeue_sema_signal(equeue_sema_t *s) {
    sem_release(s);
}

bool equeue_sema_wait(equeue_sema_t *s, int ms) {
    if (ms < 0){
        sem_acquire_blocking(s);
        return true;
    } else{
        return sem_acquire_timeout_ms(s,ms);
    }
}

#endif
