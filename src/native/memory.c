/* SPDX-License-Identifier: GPL-3.0-only */
/* Memory services shared by the working i386 backend and portable C ABI. */
#include "../bridge/address.h"
#include "kernel_handle.h"
#include "native.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifndef BB_MODEL_RUNTIME
#ifdef __APPLE__
#include <malloc/malloc.h>
#define malloc_usable_size malloc_size
#else
#include <malloc.h>
#endif
static struct {
  void *base;
  size_t size;
} allocations[1024];
static unsigned allocation_count;
static void *W HeapCreate(U32 flags, U32 initial, U32 maximum) {
  return (void *)0x1234;
}
static int W HeapDestroy(Handle heap) { return 1; }
static void *W HeapAlloc(Handle heap, U32 flags, U32 size) {
  return flags & 8 ? calloc(1, size ? size : 1) : malloc(size ? size : 1);
}
static int W HeapFree(Handle heap, U32 flags, void *memory) {
  free(memory);
  return 1;
}
static void *W HeapReAlloc(Handle heap, U32 flags, void *memory, U32 size) {
  size_t previous = memory ? malloc_usable_size(memory) : 0;
  if (flags & 0x10)
    return memory && (size ? size : 1) <= previous ? memory : NULL;
  void *result = realloc(memory, size ? size : 1);
  if (result && (flags & 8) && size > previous)
    memset((char *)result + previous, 0, size - previous);
  return result;
}
static U32 W HeapSize(Handle heap, U32 flags, void *memory) {
  return memory ? (U32)malloc_usable_size(memory) : UINT32_MAX;
}
static int W HeapValidate(Handle heap, U32 flags, const void *memory) {
  return 1;
}
static void *W GlobalAlloc(U32 flags, U32 size) {
  return flags & 0x40 ? calloc(1, size ? size : 1) : malloc(size ? size : 1);
}
static Handle W GlobalFree(Handle memory) {
  free(memory);
  return NULL;
}
static void *W GlobalLock(Handle memory) { return memory; }
static int W GlobalUnlock(Handle memory) { return 1; }
static int memory_protection(U32 protection) {
  if (protection == 1)
    return PROT_NONE;
  int value = PROT_READ;
  if (protection == 4 || protection == 8 || protection == 0x40 ||
      protection == 0x80)
    value |= PROT_WRITE;
  if (protection >= 0x10)
    value |= PROT_EXEC;
  return value;
}
static void *W VirtualAlloc(void *address, U32 size, U32 type, U32 protection) {
  uintptr_t aligned = (uintptr_t)address & ~(uintptr_t)4095;
  size_t length =
      ((size_t)size + ((uintptr_t)address - aligned) + 4095) & ~(size_t)4095;
  if (address && (type & 0x1000) && !(type & 0x2000)) {
    if (mprotect((void *)aligned, length, memory_protection(protection)))
      return NULL;
    return (void *)aligned;
  }
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
  if (address)
    flags |= MAP_FIXED_NOREPLACE;
#endif
  void *base = mmap(address, length,
                    (type & 0x1000) ? memory_protection(protection) : PROT_NONE,
                    flags, -1, 0);
  if (base == MAP_FAILED)
    return NULL;
  if (address && base != address) {
    munmap(base, length);
    return NULL;
  }
  if (allocation_count == 1024)
    bb_die("too many virtual allocations");
  allocations[allocation_count].base = base;
  allocations[allocation_count++].size = length;
  return base;
}
static int W VirtualFree(void *address, U32 size, U32 type) {
  if (type & 0x4000) {
    size_t length = (size + 4095) & ~4095U;
    if (mprotect(address, length, PROT_NONE))
      return 0;
    /* Recommitting discarded private pages must yield zero-filled storage. */
    return !madvise(address, length, MADV_DONTNEED);
  }
  for (unsigned n = 0; n < allocation_count; n++)
    if (allocations[n].base == address) {
      int ok = !munmap(address, allocations[n].size);
      allocations[n] = allocations[--allocation_count];
      return ok;
    }
  return 0;
}

static int W IsBadReadPtr(const void *pointer, U32 size) {
  return !pointer && size;
}
static int W IsBadWritePtr(void *pointer, U32 size) { return !pointer && size; }
static int W IsBadCodePtr(Handle pointer) { return !pointer; }
static I32 W InterlockedExchange(I32 *pointer, I32 value) {
  return __atomic_exchange_n(pointer, value, __ATOMIC_SEQ_CST);
}
static I32 W InterlockedIncrement(I32 *pointer) {
  return __atomic_add_fetch(pointer, 1, __ATOMIC_SEQ_CST);
}
static I32 W InterlockedDecrement(I32 *pointer) {
  return __atomic_sub_fetch(pointer, 1, __ATOMIC_SEQ_CST);
}

static void W InitializeCriticalSection(void *section) {
  pthread_mutex_t *mutex = malloc(sizeof(*mutex));
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(mutex, &attr);
  pthread_mutexattr_destroy(&attr);
  memset(section, 0, 24);
  *(void **)section = mutex;
}
static void W DeleteCriticalSection(void *section) {
  pthread_mutex_t *mutex = *(void **)section;
  pthread_mutex_destroy(mutex);
  free(mutex);
}
static void W EnterCriticalSection(void *section) {
  pthread_mutex_lock(*(void **)section);
}
static void W LeaveCriticalSection(void *section) {
  pthread_mutex_unlock(*(void **)section);
}

#else
#include "memory_portable.inc"
#endif

#ifndef BB_MODEL_RUNTIME
void *bb_memory_api(const char *name) {
  static const struct ApiEntry table[] = {API_ENTRY(HeapCreate),
                                          API_ENTRY(HeapDestroy),
                                          API_ENTRY(HeapAlloc),
                                          API_ENTRY(HeapFree),
                                          API_ENTRY(HeapReAlloc),
                                          API_ENTRY(HeapSize),
                                          API_ENTRY(HeapValidate),
                                          API_ENTRY(GlobalAlloc),
                                          API_ENTRY(GlobalFree),
                                          API_ENTRY(GlobalLock),
                                          API_ENTRY(GlobalUnlock),
                                          API_ENTRY(VirtualAlloc),
                                          API_ENTRY(VirtualFree),
                                          API_ENTRY(IsBadReadPtr),
                                          API_ENTRY(IsBadWritePtr),
                                          API_ENTRY(IsBadCodePtr),
                                          API_ENTRY(InterlockedExchange),
                                          API_ENTRY(InterlockedIncrement),
                                          API_ENTRY(InterlockedDecrement),
                                          API_ENTRY(InitializeCriticalSection),
                                          API_ENTRY(DeleteCriticalSection),
                                          API_ENTRY(EnterCriticalSection),
                                          API_ENTRY(LeaveCriticalSection)};
  API_RETURN(table);
}
#else
#include "memory_models.inc"
#endif
