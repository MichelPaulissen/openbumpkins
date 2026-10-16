/* SPDX-License-Identifier: GPL-3.0-only */
#include "kernel_handle.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
_Thread_local U32 bb_kernel_last_error;
int bb_kernel_error(U32 error) {
  bb_kernel_last_error = error;
  return 0;
}
struct KernelHandle *bb_kernel_handle_new(int type, int fd) {
  struct KernelHandle *h = calloc(1, sizeof(*h));
  if (!h)
    bb_die("handle allocation failed");
  h->type = type;
  h->fd = fd;
  atomic_init(&h->references, 1);
  return h;
}
void bb_kernel_handle_release(struct KernelHandle *h) {
  if (atomic_fetch_sub(&h->references, 1) != 1)
    return;
  if ((h->type == 1 || h->type == 4) && h->fd >= 0)
    close(h->fd);
  if (h->type == 2 || h->type == 3) {
    pthread_cond_destroy(&h->condition);
    pthread_mutex_destroy(&h->mutex);
  }
  free(h);
}
int bb_file_descriptor(Handle handle) {
  if ((uintptr_t)handle >= 1 && (uintptr_t)handle <= 3)
    return (int)(uintptr_t)handle - 1;
  if (!handle || handle == (void *)UINT32_MAX)
    return -1;
  return ((struct KernelHandle *)handle)->fd;
}
U32 bb_kernel_errno(void) {
  switch (errno) {
  case ENOENT:
    return 2;
  case ENOTDIR:
    return 3;
  case EMFILE:
  case ENFILE:
    return 4;
  case EACCES:
  case EPERM:
    return 5;
  case EBADF:
    return 6;
  case ENOMEM:
    return 8;
  case EXDEV:
    return 17;
  case EROFS:
    return 19;
  case EEXIST:
    return 80;
  case EPIPE:
    return 109;
  case ENOSPC:
    return 112;
  case ENOTEMPTY:
    return 145;
  case ENAMETOOLONG:
    return 206;
  default:
    return 87;
  }
}
