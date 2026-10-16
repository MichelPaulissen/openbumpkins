/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_WINDOWS_MMAN_H
#define BB_WINDOWS_MMAN_H
#include <stddef.h>
#include <sys/types.h>
enum { PROT_NONE = 0, PROT_READ = 1, PROT_WRITE = 2, PROT_EXEC = 4 };
enum { MAP_PRIVATE = 2, MAP_FIXED = 16, MAP_ANONYMOUS = 32 };
#define MAP_FAILED ((void *)-1)
void *bb_host_mmap(void *, size_t, int, int, int, off_t);
int bb_host_munmap(void *, size_t);
int bb_host_mprotect(void *, size_t, int);
#define mmap bb_host_mmap
#define munmap bb_host_munmap
#define mprotect bb_host_mprotect
#endif
