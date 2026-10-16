/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "address.h"
#include <pthread.h>

struct Region {
  uint32_t address;
  size_t size;
  unsigned char *host;
};
/* Keep both lookup directions sorted. Rendering frequently encodes pointers
 * back to game addresses; scanning every allocation made that cost grow with
 * the number of loaded sprites. Both indexes share the same lock/lifetime. */
static struct Region *regions, *host_regions;
static size_t count, capacity;
static pthread_rwlock_t region_lock = PTHREAD_RWLOCK_INITIALIZER;

static size_t lower_bound(uint32_t address) {
  size_t lo = 0, hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (regions[mid].address < address)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}
static size_t lower_bound_host(uintptr_t host) {
  size_t lo = 0, hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if ((uintptr_t)host_regions[mid].host < host)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}
static int overlaps(uintptr_t a, size_t an, uintptr_t b, size_t bn) {
  return a <= b ? b - a < an : a - b < bn;
}
static int bind_locked(uint32_t address, void *host, size_t size) {
  if (!address || !host || !size ||
      (uint64_t)size > UINT64_C(0x100000000) - address ||
      size - 1 > UINTPTR_MAX - (uintptr_t)host)
    return 0;
  size_t index = lower_bound(address);
  size_t host_index = lower_bound_host((uintptr_t)host);
  int valid = !(index && overlaps(address, size, regions[index - 1].address,
                                  regions[index - 1].size)) &&
              !(index < count && overlaps(address, size, regions[index].address,
                                          regions[index].size));
  valid = valid &&
          !(host_index && overlaps((uintptr_t)host, size,
                                  (uintptr_t)host_regions[host_index - 1].host,
                                  host_regions[host_index - 1].size)) &&
          !(host_index < count &&
            overlaps((uintptr_t)host, size,
                     (uintptr_t)host_regions[host_index].host,
                     host_regions[host_index].size));
  if (valid && count == capacity) {
    size_t next = capacity && capacity <= SIZE_MAX / 2 ? capacity * 2
                  : capacity                           ? 0
                                                       : 16;
    struct Region *grown = next && next <= SIZE_MAX / sizeof(*grown)
                               ? realloc(regions, next * sizeof(*grown))
                               : NULL;
    if (!grown)
      valid = 0;
    else {
      regions = grown;
      struct Region *host_grown = realloc(host_regions, next * sizeof(*grown));
      if (!host_grown)
        valid = 0;
      else {
        host_regions = host_grown;
        capacity = next;
      }
    }
  }
  if (valid) {
    for (size_t i = count; i > index; --i)
      regions[i] = regions[i - 1];
    regions[index] = (struct Region){address, size, host};
    for (size_t i = count; i > host_index; --i)
      host_regions[i] = host_regions[i - 1];
    host_regions[host_index] = regions[index];
    ++count;
  }
  return valid;
}
int bb_address_bind(uint32_t address, void *host, size_t size) {
  pthread_rwlock_wrlock(&region_lock);
  int valid = bind_locked(address, host, size);
  pthread_rwlock_unlock(&region_lock);
  return valid;
}
int bb_address_bind_auto(uint32_t first, uint64_t limit, void *host, size_t size,
                          uint32_t alignment, uint32_t *address) {
  if (!first || !address || !size || !alignment ||
      (alignment & (alignment - 1u)) || limit > UINT64_C(0x100000000) ||
      first >= limit || size > limit - first)
    return 0;
  uint64_t mask = alignment - 1u;
  uint64_t candidate = ((uint64_t)first + mask) & ~mask;
  pthread_rwlock_wrlock(&region_lock);
  for (size_t i = 0; i < count && candidate < limit; ++i) {
    uint64_t end = (uint64_t)regions[i].address + regions[i].size;
    if (end <= candidate)
      continue;
    if (candidate < regions[i].address &&
        size <= (uint64_t)regions[i].address - candidate)
      break;
    candidate = (end + mask) & ~mask;
  }
  int valid = candidate < limit && size <= limit - candidate &&
              bind_locked((uint32_t)candidate, host, size);
  if (valid)
    *address = (uint32_t)candidate;
  pthread_rwlock_unlock(&region_lock);
  return valid;
}
int bb_address_unbind(uint32_t address) {
  pthread_rwlock_wrlock(&region_lock);
  size_t index = lower_bound(address);
  int found = index < count && regions[index].address == address;
  if (found) {
    size_t host_index = lower_bound_host((uintptr_t)regions[index].host);
    for (size_t i = host_index + 1; i < count; ++i)
      host_regions[i - 1] = host_regions[i];
    for (size_t i = index + 1; i < count; ++i)
      regions[i - 1] = regions[i];
    if (!--count) {
      free(regions);
      free(host_regions);
      regions = host_regions = NULL;
      capacity = 0;
    }
  }
  pthread_rwlock_unlock(&region_lock);
  return found;
}
int bb_address_try_pointer(uintptr_t address, size_t size, void **result) {
  if (!result)
    return 0;
  if (!address || address > UINT32_MAX) {
    if (size && size - 1 > UINTPTR_MAX - address)
      return 0;
    *result = (void *)address;
    return 1;
  }
  pthread_rwlock_rdlock(&region_lock);
  uint32_t word = (uint32_t)address;
  size_t index = lower_bound(word);
  if (index == count || regions[index].address != word) {
    if (!index) {
      pthread_rwlock_unlock(&region_lock);
      return 0;
    }
    --index;
  }
  const struct Region *region = &regions[index];
  size_t offset = word - region->address;
  int valid = offset <= region->size && size <= region->size - offset;
  if (valid)
    *result = region->host + offset;
  pthread_rwlock_unlock(&region_lock);
  return valid;
}
int bb_address_try_encode(const void *pointer, uint32_t *result) {
  if (!result)
    return 0;
  uintptr_t host = (uintptr_t)pointer;
  pthread_rwlock_rdlock(&region_lock);
  size_t index = lower_bound_host(host);
  if (index == count || (uintptr_t)host_regions[index].host != host) {
    if (index)
      --index;
  }
  if (index < count) {
    uintptr_t start = (uintptr_t)host_regions[index].host;
    if (host >= start && host - start < host_regions[index].size) {
      *result = host_regions[index].address + (uint32_t)(host - start);
      pthread_rwlock_unlock(&region_lock);
      return 1;
    }
  }
  pthread_rwlock_unlock(&region_lock);
  if (host <= UINT32_MAX) {
    *result = (uint32_t)host;
    return 1;
  }
  return 0;
}
void *bb_address_pointer(uintptr_t address, size_t size) {
  void *result;
  if (!bb_address_try_pointer(address, size, &result))
    abort();
  return result;
}
uint32_t bb_address_encode(const void *pointer) {
  uint32_t result;
  if (!bb_address_try_encode(pointer, &result))
    abort();
  return result;
}
