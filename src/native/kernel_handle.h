/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_KERNEL_HANDLE_H
#define BB_KERNEL_HANDLE_H
#include "native.h"
#include <pthread.h>
#include <stdatomic.h>
struct KernelHandle {
  int type, fd;
  atomic_uint references;
  size_t size;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int signaled, manual, started;
  U32 thread_id;
#ifdef BB_MODEL_RUNTIME
  uint32_t reference;
  struct KernelHandle *model_next;
#endif
};
extern _Thread_local U32 bb_kernel_last_error;
int bb_kernel_error(U32);
U32 bb_kernel_errno(void);
struct KernelHandle *bb_kernel_handle_new(int, int);
void bb_kernel_handle_release(struct KernelHandle *);
int bb_kernel_wait_initialize(struct KernelHandle *);
void *bb_file_api(const char *);
#ifdef BB_MODEL_RUNTIME
/* Export transfers an owned host handle to its logical owner. Acquire/release
 * borrow a host reference; close removes the logical owner atomically. */
uint32_t bb_kernel_handle_export(Handle);
Handle bb_kernel_handle_acquire(uint32_t);
void bb_kernel_handle_unborrow(Handle);
int bb_kernel_handle_close(uint32_t);
#endif
#endif
