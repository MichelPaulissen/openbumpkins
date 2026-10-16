/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "../bridge/callback_runtime.h"
#include "sync_models.h"
#endif
struct Timer {
  struct Timer *retired_next;
  SDL_TimerID timer;
  U32 id, delay, flags, user;
#ifdef BB_MODEL_RUNTIME
  uint32_t callback;
  struct bb_callback_context context;
#else
  void(W *callback)(U32, U32, U32, U32, U32);
#endif
  atomic_uint state;
  int cancellation_waiter;
};
enum { TIMER_WAITING, TIMER_RUNNING, TIMER_FINISHED, TIMER_CANCELLED = 4 };
static struct Timer *timers[64];
static struct Timer *retired_timers;
static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timer_condition = PTHREAD_COND_INITIALIZER;
static U32 next_timer = 1;
#ifndef BB_MODEL_RUNTIME
static _Thread_local int timer_teb;
#endif
static _Thread_local struct Timer *current_timer;

typedef void (*wave_pump_fn)(void);
static _Atomic(wave_pump_fn) wave_pump;
void bb_timer_set_wave_pump(void (*pump)(void)) {
  atomic_store_explicit(&wave_pump, pump, memory_order_release);
}
static void timer_dispose(struct Timer *timer) {
#ifdef BB_MODEL_RUNTIME
  bb_callback_destroy(&timer->context);
#endif
  free(timer);
}
/* Cancelled records remain owned until the SDL worker acknowledges them.
 * This also gives final shutdown an owner for callbacks SDL never enters. */
static void timer_unretire_locked(struct Timer *timer) {
  struct Timer **slot = &retired_timers;
  while (*slot && *slot != timer)
    slot = &(*slot)->retired_next;
  if (*slot)
    *slot = timer->retired_next;
  timer->retired_next = NULL;
}
void bb_timer_shutdown(void) {
  /* Owners must stop callbacks first. SDL_Quit joins its timer worker before
   * this collection can safely release compatibility callback userdata. */
  if (current_timer || SDL_WasInit(SDL_INIT_TIMER))
    bb_die("timer shutdown requires a stopped SDL timer subsystem");
  pthread_mutex_lock(&timer_mutex);
  for (unsigned n = 0; n < 64; n++) {
    if (timers[n]) {
      timer_dispose(timers[n]);
      timers[n] = NULL;
    }
  }
  while (retired_timers) {
    struct Timer *timer = retired_timers;
    retired_timers = timer->retired_next;
    timer_dispose(timer);
  }
  pthread_mutex_unlock(&timer_mutex);
}
static U32 timer_callback(U32 interval, void *opaque) {
  struct Timer *timer = opaque;
  /* Creation publishes the SDL handle before a callback can use the record.
   * SDL2-compat has already finished both property lookups at this point. */
  pthread_mutex_lock(&timer_mutex);
  SDL_TimerID handle = timer->timer;
  U32 flags = timer->flags;
  unsigned expected = TIMER_WAITING;
  int active =
      atomic_compare_exchange_strong(&timer->state, &expected, TIMER_RUNNING);
  pthread_mutex_unlock(&timer_mutex);
  if (!active) {
    SDL_RemoveTimer(handle);
    pthread_mutex_lock(&timer_mutex);
    timer_unretire_locked(timer);
    pthread_mutex_unlock(&timer_mutex);
    timer_dispose(timer);
    return 0;
  }
#ifndef BB_MODEL_RUNTIME
  if (!timer_teb) {
    bb_init_teb();
    timer_teb = 1;
  }
#endif
  struct Timer *previous_timer = current_timer;
  current_timer = timer;
  wave_pump_fn pump = atomic_load_explicit(&wave_pump, memory_order_acquire);
  if (pump)
    pump();
#ifdef BB_MODEL_RUNTIME
  if (flags & 0x30) {
    uint32_t args[] = {timer->callback};
    bb_callback_invoke(&timer->context, bb_sync_model_export("SetEvent"), args,
                       1);
  } else {
    uint32_t args[] = {timer->id, 0, timer->user, 0, 0};
    bb_callback_invoke(&timer->context, timer->callback, args, 5);
  }
#else
  if (flags & 0x30) {
    int(W * set)(Handle) = bb_kernel_api("SetEvent");
    set((void *)(uintptr_t)timer->callback);
  } else {
    void(W * callback)(U32, U32, U32, U32, U32) =
        (void *)bb_native_entry((uintptr_t)timer->callback);
    callback(timer->id, 0, timer->user, 0, 0);
  }
#endif
  current_timer = previous_timer;
  /* A pending cancellation is released by this worker, after compat's
   * property reads and the application callback. Removing it on the caller
   * thread could destroy a property mutex the worker had already borrowed. */
#ifdef BB_MODEL_RUNTIME
  if (!(flags & 1))
    bb_callback_destroy(&timer->context);
#endif
  unsigned next = flags & 1 ? TIMER_WAITING : TIMER_FINISHED;
  expected = TIMER_RUNNING;
  if (atomic_compare_exchange_strong(&timer->state, &expected, next))
    return flags & 1 ? interval : 0;
  SDL_RemoveTimer(handle);
  pthread_mutex_lock(&timer_mutex);
  if (timer->cancellation_waiter) {
    atomic_store(&timer->state, TIMER_FINISHED);
    pthread_cond_broadcast(&timer_condition);
    pthread_mutex_unlock(&timer_mutex);
  } else {
    timer_unretire_locked(timer);
    pthread_mutex_unlock(&timer_mutex);
    timer_dispose(timer);
  }
  return 0;
}
#ifdef BB_MODEL_RUNTIME
typedef uint32_t timer_callback_ref;
#else
typedef void(W *timer_callback_ref)(U32, U32, U32, U32, U32);
#endif
static U32 W timeSetEvent(U32 delay, U32 resolution,
                          timer_callback_ref callback, U32 user, U32 flags) {
  if (SDL_InitSubSystem(SDL_INIT_TIMER))
    return 0;
  pthread_mutex_lock(&timer_mutex);
  unsigned slot = 0;
  while (slot < 64 && timers[slot])
    slot++;
  if (slot == 64) {
    pthread_mutex_unlock(&timer_mutex);
    return 0;
  }
  struct Timer *timer = calloc(1, sizeof(*timer));
  if (!timer) {
    pthread_mutex_unlock(&timer_mutex);
    return 0;
  }
#ifdef BB_MODEL_RUNTIME
  if (!bb_callback_initialize(&timer->context)) {
    timer_dispose(timer);
    pthread_mutex_unlock(&timer_mutex);
    return 0;
  }
#endif
  timer->id = next_timer++;
  timer->delay = delay;
  timer->callback = callback;
  timer->user = user;
  timer->flags = flags;
  atomic_init(&timer->state, TIMER_WAITING);
  timer->timer = SDL_AddTimer(delay ? delay : 1, timer_callback, timer);
  if (!timer->timer) {
    timer_dispose(timer);
    pthread_mutex_unlock(&timer_mutex);
    return 0;
  }
  timers[slot] = timer;
  U32 id = timer->id;
  pthread_mutex_unlock(&timer_mutex);
  return id;
}
static U32 W timeKillEvent(U32 id) {
  pthread_mutex_lock(&timer_mutex);
  for (unsigned n = 0; n < 64; n++)
    if (timers[n] && timers[n]->id == id) {
      struct Timer *timer = timers[n];
      unsigned prior = atomic_fetch_or(&timer->state, TIMER_CANCELLED);
      timers[n] = NULL;
      if (prior != TIMER_FINISHED) {
        timer->retired_next = retired_timers;
        retired_timers = timer;
      }
#ifdef BB_MODEL_RUNTIME
      /* Its worker can only take the cancelled path now. Release the large
       * stack while the small SDL context waits for that acknowledgement. */
      if (prior == TIMER_WAITING)
        bb_callback_destroy(&timer->context);
#endif
      /* Finished one-shots have no remaining property reads or record use.
       * Waiting/running timers acknowledge cancellation on their worker. */
      if (prior == TIMER_FINISHED) {
        SDL_RemoveTimer(timer->timer);
        timer_dispose(timer);
      } else if ((timer->flags & 0x100) && prior == TIMER_RUNNING &&
                 current_timer != timer) {
        /* TIME_KILL_SYNCHRONOUS protects callback-owned application resources.
         * The worker retains the SDL context until its property reads finish;
         * this waiter takes ownership only after the application callback ends.
         * A callback cancelling itself must let its own worker release it. */
        timer->cancellation_waiter = 1;
        while (atomic_load(&timer->state) != TIMER_FINISHED)
          pthread_cond_wait(&timer_condition, &timer_mutex);
        timer_unretire_locked(timer);
        timer_dispose(timer);
      }
      pthread_mutex_unlock(&timer_mutex);
      return 0;
    }
  pthread_mutex_unlock(&timer_mutex);
  return 97;
}
static U32 W timeBeginPeriod(U32 period) {
  return period >= 1 && period <= 1000 ? 0 : 97;
}
static U32 W timeEndPeriod(U32 period) { return 0; }
static U32 W timeGetTime(void) { return bb_tick(); }
static U32 W timeGetDevCaps(U32 *caps, U32 size) {
  if (size < 8)
    return 97;
  caps[0] = 1;
  caps[1] = 1000;
  return 0;
}

void *bb_timer_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(timeSetEvent),    API_ENTRY(timeKillEvent),
      API_ENTRY(timeBeginPeriod), API_ENTRY(timeEndPeriod),
      API_ENTRY(timeGetTime),     API_ENTRY(timeGetDevCaps)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "timer_models.inc"
#endif
