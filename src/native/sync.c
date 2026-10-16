/* SPDX-License-Identifier: GPL-3.0-only */
#include "kernel_handle.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int bb_kernel_wait_initialize(struct KernelHandle *h) {
  if (pthread_mutex_init(&h->mutex, NULL))
    return 0;
#ifdef __APPLE__
  int error = pthread_cond_init(&h->condition, NULL);
#else
  pthread_condattr_t attributes;
  int error = pthread_condattr_init(&attributes);
  if (!error) {
    error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (!error)
      error = pthread_cond_init(&h->condition, &attributes);
    pthread_condattr_destroy(&attributes);
  }
#endif
  if (error)
    pthread_mutex_destroy(&h->mutex);
  return !error;
}

/* Darwin waits relative to a monotonic deadline; Linux configures the condition
 * clock directly. Recompute remaining time after every spurious wakeup. */
static int wait_until(struct KernelHandle *h, const struct timespec *limit) {
#ifdef __APPLE__
  struct timespec now, remaining;
  clock_gettime(CLOCK_MONOTONIC, &now);
  remaining.tv_sec = limit->tv_sec - now.tv_sec;
  remaining.tv_nsec = limit->tv_nsec - now.tv_nsec;
  if (remaining.tv_nsec < 0) {
    --remaining.tv_sec;
    remaining.tv_nsec += 1000000000;
  }
  if (remaining.tv_sec < 0 || (!remaining.tv_sec && !remaining.tv_nsec))
    return ETIMEDOUT;
  return pthread_cond_timedwait_relative_np(&h->condition, &h->mutex,
                                            &remaining);
#else
  return pthread_cond_timedwait(&h->condition, &h->mutex, limit);
#endif
}
static Handle W CreateEventA(Handle security, int manual, int initial,
                             const char *name) {
  struct KernelHandle *h = bb_kernel_handle_new(2, -1);
  if (!bb_kernel_wait_initialize(h)) {
    free(h);
    bb_kernel_last_error = 8;
    return NULL;
  }
  h->manual = manual;
  h->signaled = initial;
  return h;
}
static int W SetEvent(Handle event) {
  if ((uintptr_t)event <= 3 || event == (void *)UINT32_MAX ||
      ((struct KernelHandle *)event)->type != 2)
    return bb_kernel_error(6);
  struct KernelHandle *h = event;
  pthread_mutex_lock(&h->mutex);
  h->signaled = 1;
  pthread_cond_broadcast(&h->condition);
  pthread_mutex_unlock(&h->mutex);
  return 1;
}
static int W ResetEvent(Handle event) {
  if ((uintptr_t)event <= 3 || event == (void *)UINT32_MAX ||
      ((struct KernelHandle *)event)->type != 2)
    return bb_kernel_error(6);
  struct KernelHandle *h = event;
  pthread_mutex_lock(&h->mutex);
  h->signaled = 0;
  pthread_mutex_unlock(&h->mutex);
  return 1;
}
static U32 W WaitForSingleObject(Handle handle, U32 timeout) {
  if ((uintptr_t)handle <= 3 || handle == (void *)UINT32_MAX) {
    bb_kernel_last_error = 6;
    return UINT32_MAX;
  }
  struct KernelHandle *h = handle;
  if (h->type != 2 && h->type != 3) {
    bb_kernel_last_error = 6;
    return UINT32_MAX;
  }
  /* One deadline survives every spurious wakeup and excludes wall-clock jumps.
   * Thread completion remains signaled for every waiter, just like a manual
   * event. Waiting does not join or consume the worker's native resources. */
  struct timespec limit = {0};
  if (timeout && timeout != UINT32_MAX) {
    clock_gettime(CLOCK_MONOTONIC, &limit);
    limit.tv_sec += timeout / 1000;
    limit.tv_nsec += (timeout % 1000) * 1000000;
    if (limit.tv_nsec >= 1000000000) {
      limit.tv_nsec -= 1000000000;
      ++limit.tv_sec;
    }
  }
  pthread_mutex_lock(&h->mutex);
  while (!h->signaled) {
    if (!timeout) {
      pthread_mutex_unlock(&h->mutex);
      return 258;
    }
    int error = timeout == UINT32_MAX
                    ? pthread_cond_wait(&h->condition, &h->mutex)
                    : wait_until(h, &limit);
    if (error && !h->signaled) {
      pthread_mutex_unlock(&h->mutex);
      if (error == ETIMEDOUT)
        return 258;
      bb_kernel_last_error = 87;
      return UINT32_MAX;
    }
  }
  if (h->type == 2 && !h->manual)
    h->signaled = 0;
  pthread_mutex_unlock(&h->mutex);
  return 0;
}

void *bb_sync_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(CreateEventA), API_ENTRY(SetEvent), API_ENTRY(ResetEvent),
      API_ENTRY(WaitForSingleObject)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "sync_models.inc"
#endif
