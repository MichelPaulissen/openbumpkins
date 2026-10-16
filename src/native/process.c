/* SPDX-License-Identifier: GPL-3.0-only */
#include "kernel_exports.h"
#include "kernel_handle.h"
#include "../bridge/address.h"
#ifdef BB_MODEL_RUNTIME
#include "../bridge/crt_exception_runtime.h"
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef BB_MODEL_RUNTIME
#include "../bridge/callback_runtime.h"
#endif

static void W __attribute__((noreturn)) ExitProcess(U32 code) {
  fprintf(stderr, "Game exited: %u\n", code);
  exit((int)code);
}
static int W TerminateProcess(Handle process, U32 code) { ExitProcess(code); }
static Handle W GetCurrentProcess(void) { return (void *)UINT32_MAX; }
static Handle W GetCurrentThread(void) { return (void *)(UINT32_MAX - 1); }
static U32 W GetCurrentProcessId(void) { return (U32)getpid(); }
static U32 W GetCurrentThreadId(void) {
#if defined(__APPLE__) || defined(_WIN32)
  /* Windows thread IDs are opaque four-byte values. Do not narrow Darwin's
   * pthread handle or its wider native numeric thread ID into that slot. */
  static atomic_uint next_id = 1;
  static _Thread_local uint32_t id;
  if (!id) {
    id = atomic_fetch_add(&next_id, 1);
    if (!id)
      bb_die("thread identity space exhausted");
  }
  return id;
#else
  return (U32)gettid();
#endif
}
static int W SetThreadPriority(Handle thread, I32 priority) { return 1; }
static void W RaiseException(U32 code, U32 flags, U32 count,
                             const U32 *arguments) {
#ifdef BB_MODEL_RUNTIME
  /* Converted C++ owners have their own C cleanup/recovery scopes. Raw Win32
   * raises propagate a terminal C error, without an x86 registration chain. */
  struct bb_model_exception exception = {
      .code = code, .flags = flags,
      .address = bb_model_current_identity(), .count = count > 15 ? 15 : count};
  if (exception.count)
    memcpy(exception.arguments, BB_SPAN(arguments, exception.count * 4u),
           exception.count * 4u);
  bb_model_raise_exception(&exception);
#else
  bb_exception_raise(code, flags, count, arguments, bb_exception_filter_get());
#endif
}
static void W RtlUnwind(Handle frame, Handle ip, Handle record, Handle value) {
#ifdef BB_MODEL_RUNTIME
  (void)ip;
  (void)value;
  bb_crt_model_unwind(bb_address_encode(frame), bb_address_encode(record));
#else
  bb_exception_unwind(frame, ip, record, value);
#endif
}
static void W DebugBreak(void) { bb_die("original DebugBreak"); }
struct ThreadStart {
#ifdef BB_MODEL_RUNTIME
  uint32_t function, argument;
  struct bb_callback_context context;
#else
  U32(W *function)(void *);
  void *argument;
#endif
  struct KernelHandle *handle;
};
static void *thread_start(void *opaque) {
  struct ThreadStart *start = opaque;
#ifndef BB_MODEL_RUNTIME
  bb_init_teb();
#endif
  struct KernelHandle *h = start->handle;
  pthread_mutex_lock(&h->mutex);
  h->thread_id = GetCurrentThreadId();
  h->started = 1;
  pthread_cond_broadcast(&h->condition);
  pthread_mutex_unlock(&h->mutex);
#ifdef BB_MODEL_RUNTIME
  bb_callback_invoke(&start->context, start->function, &start->argument, 1);
  bb_callback_destroy(&start->context);
#else
  U32(W * function)(void *) =
      (void *)bb_native_entry((uintptr_t)start->function);
  function(start->argument);
  bb_free_teb();
#endif
  free(start);
  SDL_TLSCleanup();
  pthread_mutex_lock(&h->mutex);
  h->signaled = 1;
  pthread_cond_broadcast(&h->condition);
  pthread_mutex_unlock(&h->mutex);
  bb_kernel_handle_release(h);
  return NULL;
}
static Handle W CreateThread(Handle security, U32 stack,
                             U32(W *function)(void *), void *argument,
                             U32 flags, U32 *id) {
  struct KernelHandle *h = bb_kernel_handle_new(3, -1);
  struct ThreadStart *start = malloc(sizeof(*start));
  if (!start || !bb_kernel_wait_initialize(h)) {
    free(start);
    free(h);
    bb_kernel_last_error = 8;
    return NULL;
  }
  start->handle = h;
#ifdef BB_MODEL_RUNTIME
  start->function = (uint32_t)(uintptr_t)function;
  start->argument = (uint32_t)(uintptr_t)argument;
  uint32_t reference = 0;
  if (!bb_callback_initialize(&start->context) ||
      !(reference = bb_kernel_handle_export(h))) {
    bb_callback_destroy(&start->context);
    bb_kernel_handle_release(h);
    free(start);
    bb_kernel_last_error = 8;
    return NULL;
  }
  /* Reserve the public reference before a worker can run. A separate creator
   * reference protects the handshake even if a worker closes its own handle. */
  atomic_fetch_add(&h->references, 1);
#else
  start->function = function;
  start->argument = argument;
#endif
  pthread_attr_t attributes;
  int error = pthread_attr_init(&attributes);
  if (!error) {
    error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (!error) {
      /* The public handle and worker each own a reference. CloseHandle never
       * terminates a running worker, including one closing its own handle. */
      atomic_fetch_add(&h->references, 1);
      error = pthread_create(&h->thread, &attributes, thread_start, start);
      if (error)
        bb_kernel_handle_release(h);
    }
    pthread_attr_destroy(&attributes);
  }
  if (error) {
#ifdef BB_MODEL_RUNTIME
    bb_callback_destroy(&start->context);
    if (!bb_kernel_handle_close(reference))
      bb_die("thread creation lost its public handle");
#endif
    free(start);
    bb_kernel_handle_release(h);
    bb_kernel_last_error = 8;
    return NULL;
  }
  pthread_mutex_lock(&h->mutex);
  while (!h->started)
    pthread_cond_wait(&h->condition, &h->mutex);
  if (id)
    *id = h->thread_id;
  pthread_mutex_unlock(&h->mutex);
#ifdef BB_MODEL_RUNTIME
  bb_kernel_handle_release(h);
  return (void *)(uintptr_t)reference;
#else
  return h;
#endif
}

void *bb_process_api(const char *name) {
  static const struct ApiEntry table[] = {
#define ENTRY(name) API_ENTRY(name),
      BB_KERNEL_BASE_EXPORTS(ENTRY)
#undef ENTRY
  };
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "process_models.inc"
#endif
