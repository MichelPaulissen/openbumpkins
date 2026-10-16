/* SPDX-License-Identifier: GPL-3.0-only */
#include "heap.h"
#include "address.h"
#include <pthread.h>
#include <string.h>
#ifdef _WIN32
#include <malloc.h>
#define host_allocate(bytes) _aligned_malloc(bytes, 16)
#define host_release(pointer) _aligned_free(pointer)
#else
#define host_allocate(bytes) aligned_alloc(16, bytes)
#define host_release(pointer) free(pointer)
#endif

struct allocation {
  struct allocation *next;
  uint32_t address;
  void *host;
  size_t size, capacity;
};
static struct allocation *allocations;
static size_t live_count, live_bytes, byte_limit = SIZE_MAX;
static pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;

static struct allocation **find(uint32_t address) {
  struct allocation **slot = &allocations;
  while (*slot && (*slot)->address != address)
    slot = &(*slot)->next;
  return slot;
}
static uint32_t allocate_locked(size_t size, int zero) {
  size_t charge = size ? size : 1;
  if (charge > UINT32_MAX - 15u || charge > byte_limit - live_bytes)
    return 0;
  size_t capacity = (charge + 15u) & ~(size_t)15u;
  struct allocation *node = malloc(sizeof(*node));
  void *host = node ? host_allocate(capacity) : NULL;
  if (!host) {
    free(node);
    return 0;
  }
  uint32_t address;
#ifdef BB_PORTABLE_RUNTIME
  if (!bb_address_bind_auto(0x20000000u, UINT64_C(0xf0000000), host, capacity,
                            16, &address)) {
    host_release(host);
    free(node);
    return 0;
  }
#else
  address = bb_address_encode(host);
#endif
  if (zero)
    memset(host, 0, charge);
  *node = (struct allocation){allocations, address, host, size, capacity};
  allocations = node;
  ++live_count;
  live_bytes += charge;
  return address;
}
uint32_t bb_heap_allocate(size_t size) {
  pthread_mutex_lock(&heap_lock);
  uint32_t address = allocate_locked(size, 0);
  pthread_mutex_unlock(&heap_lock);
  return address;
}
uint32_t bb_heap_zeroed(size_t count, size_t size) {
  if (count && size > SIZE_MAX / count)
    return 0;
  pthread_mutex_lock(&heap_lock);
  uint32_t address = allocate_locked(count * size, 1);
  pthread_mutex_unlock(&heap_lock);
  return address;
}
static void release_locked(struct allocation **slot) {
  struct allocation *node = *slot;
  *slot = node->next;
#ifdef BB_PORTABLE_RUNTIME
  if (!bb_address_unbind(node->address))
    abort();
#endif
  live_bytes -= node->size ? node->size : 1;
  --live_count;
  host_release(node->host);
  free(node);
}
int bb_heap_release(uint32_t address) {
  if (!address)
    return 1;
  pthread_mutex_lock(&heap_lock);
  struct allocation **slot = find(address);
  int valid = *slot != NULL;
  if (valid)
    release_locked(slot);
  pthread_mutex_unlock(&heap_lock);
  return valid;
}
uint32_t bb_heap_resize(uint32_t address, size_t size) {
  if (!address)
    return bb_heap_allocate(size);
  pthread_mutex_lock(&heap_lock);
  struct allocation **slot = find(address);
  uint32_t result = 0;
  if (*slot && !size)
    release_locked(slot);
  else if (*slot && size <= (*slot)->size) {
    live_bytes -= (*slot)->size - size;
    (*slot)->size = size;
    result = address;
  } else if (*slot) {
    struct allocation *old = *slot;
    result = allocate_locked(size, 0);
    if (result) {
      memcpy(allocations->host, old->host, old->size);
      release_locked(find(address));
    }
  }
  pthread_mutex_unlock(&heap_lock);
  return result;
}
int bb_heap_size(uint32_t address, size_t *size) {
  if (!size)
    return 0;
  pthread_mutex_lock(&heap_lock);
  struct allocation *node = *find(address);
  int found = node != NULL;
  if (node)
    *size = node->size;
  pthread_mutex_unlock(&heap_lock);
  return found;
}
int bb_heap_capacity(uint32_t address, size_t *capacity) {
  if (!capacity)
    return 0;
  pthread_mutex_lock(&heap_lock);
  struct allocation *node = *find(address);
  if (node)
    *capacity = node->capacity;
  pthread_mutex_unlock(&heap_lock);
  return node != NULL;
}
uint32_t bb_heap_resize_in_place(uint32_t address, size_t size) {
  pthread_mutex_lock(&heap_lock);
  struct allocation *node = *find(address);
  size_t charge = size ? size : 1,
         old = node ? (node->size ? node->size : 1) : 0;
  uint32_t result = 0;
  if (node && charge <= node->capacity &&
      (charge <= old || charge - old <= byte_limit - live_bytes)) {
    live_bytes = live_bytes - old + charge;
    node->size = size;
    result = address;
  }
  pthread_mutex_unlock(&heap_lock);
  return result;
}
int bb_heap_set_limit(size_t limit) {
  pthread_mutex_lock(&heap_lock);
  int valid = limit >= live_bytes;
  if (valid)
    byte_limit = limit;
  pthread_mutex_unlock(&heap_lock);
  return valid;
}
void bb_heap_usage(size_t *count, size_t *bytes) {
  pthread_mutex_lock(&heap_lock);
  if (count)
    *count = live_count;
  if (bytes)
    *bytes = live_bytes;
  pthread_mutex_unlock(&heap_lock);
}
