/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/heap.h"
static void *reference(uint32_t p) { return (void *)(uintptr_t)p; }
static void *W HeapCreate(U32 flags, U32 initial, U32 maximum) {
  return reference(0x1234);
}
static int W HeapDestroy(Handle heap) { return 1; }
static void *W HeapAlloc(Handle heap, U32 flags, U32 size) {
  return reference(flags & 8 ? bb_heap_zeroed(1, size ? size : 1)
                             : bb_heap_allocate(size ? size : 1));
}
static int W HeapFree(Handle heap, U32 flags, void *p) {
  return bb_heap_release(bb_address_encode(p));
}
static void *W HeapReAlloc(Handle heap, U32 flags, void *p, U32 size) {
  uint32_t old = bb_address_encode(p);
  size_t capacity = 0;
  if (old && !bb_heap_capacity(old, &capacity))
    return NULL;
  uint32_t out = flags & 0x10 ? bb_heap_resize_in_place(old, size ? size : 1)
                              : bb_heap_resize(old, size ? size : 1);
  if (out && (flags & 8) && !(flags & 0x10) && size > capacity)
    memset(BB_SPAN(out + capacity, size - capacity), 0, size - capacity);
  return reference(out);
}
static U32 W HeapSize(Handle heap, U32 flags, void *p) {
  size_t capacity;
  return bb_heap_capacity(bb_address_encode(p), &capacity) ? (U32)capacity
                                                           : UINT32_MAX;
}
static int W HeapValidate(Handle heap, U32 flags, const void *p) {
  size_t size;
  return !p || bb_heap_size(bb_address_encode(p), &size);
}
static void *W GlobalAlloc(U32 flags, U32 size) {
  return HeapAlloc(NULL, flags & 0x40 ? 8 : 0, size);
}
static Handle W GlobalFree(Handle p) { return HeapFree(NULL, 0, p) ? NULL : p; }
static void *W GlobalLock(Handle p) { return p; }
static int W GlobalUnlock(Handle p) { return 1; }
struct virtual_span {
  struct virtual_span *next;
  uint32_t address;
  size_t size;
  void *host;
  unsigned char *access;
  int logical_protection;
};
static struct virtual_span *virtual_spans;
static pthread_mutex_t virtual_lock = PTHREAD_MUTEX_INITIALIZER;
/* Game reservations have 4 KiB pages. A host with larger pages cannot protect
 * their subsets independently. Keep those host mappings writable and enforce
 * the logical per-page access state in pointer validation instead. */
static int logical_protection(void) { return sysconf(_SC_PAGESIZE) != 4096; }
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
static struct virtual_span *virtual_find(uint32_t p, size_t size) {
  for (struct virtual_span *v = virtual_spans; v; v = v->next)
    if (p >= v->address && (uint64_t)p + size <= (uint64_t)v->address + v->size)
      return v;
  return NULL;
}
static void *W VirtualAlloc(void *p, U32 size, U32 type, U32 protection) {
  uint32_t requested = bb_address_encode(p), base = requested & ~4095u;
  uint64_t bytes =
      ((uint64_t)size + (requested - base) + 4095) & ~UINT64_C(4095);
  if (!size || !bytes || bytes > UINT32_MAX ||
      (uint64_t)base + bytes > UINT64_C(0x100000000))
    return NULL;
  size_t length = bytes;
  int access = memory_protection(protection);
  pthread_mutex_lock(&virtual_lock);
  if (p && (type & 0x1000) && !(type & 0x2000)) {
    struct virtual_span *v = virtual_find(base, length);
    void *result = NULL;
    if (v &&
        (v->logical_protection ||
         !mprotect((char *)v->host + (base - v->address), length, access))) {
      memset(v->access + (base - v->address) / 4096, access, length / 4096);
      result = reference(base);
    }
    pthread_mutex_unlock(&virtual_lock);
    return result;
  }
  struct virtual_span *v = calloc(1, sizeof(*v));
  if (v)
    v->access = calloc(length / 4096, 1);
  if (!v || !v->access) {
    if (v)
      free(v);
    pthread_mutex_unlock(&virtual_lock);
    return NULL;
  }
  void *hint = NULL;
  int map_flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifndef BB_PORTABLE_RUNTIME
  hint = reference(base);
#ifdef MAP_FIXED_NOREPLACE
  if (hint)
    map_flags |= MAP_FIXED_NOREPLACE;
#endif
#endif
  void *host = mmap(hint, length,
                    logical_protection() ? PROT_READ | PROT_WRITE
                                         : (type & 0x1000 ? access : PROT_NONE),
                    map_flags, -1, 0);
  if (host == MAP_FAILED)
    goto fail;
#ifndef BB_PORTABLE_RUNTIME
  if (hint && host != hint) {
    munmap(host, length);
    goto fail;
  }
  base = bb_address_encode(host);
#else
  if (requested) {
    if (!bb_address_bind(base, host, length)) {
      munmap(host, length);
      goto fail;
    }
  } else if (!bb_address_bind_auto(0x20000000u, UINT64_C(0xf0000000), host,
                                   length, 0x10000, &base)) {
    munmap(host, length);
    goto fail;
  }
#endif
  if (type & 0x1000)
    memset(v->access, access, length / 4096);
  *v = (struct virtual_span){virtual_spans, base,      length,
                             host,          v->access, logical_protection()};
  virtual_spans = v;
  pthread_mutex_unlock(&virtual_lock);
  return reference(base);
fail:
  free(v->access);
  free(v);
  pthread_mutex_unlock(&virtual_lock);
  return NULL;
}
static int W VirtualFree(void *p, U32 size, U32 type) {
  uint32_t address = bb_address_encode(p);
  pthread_mutex_lock(&virtual_lock);
  struct virtual_span **slot = &virtual_spans;
  while (*slot &&
         !((uint64_t)address >= (*slot)->address &&
           (uint64_t)address < (uint64_t)(*slot)->address + (*slot)->size))
    slot = &(*slot)->next;
  struct virtual_span *v = *slot;
  int result = 0;
  if (!v)
    goto done;
  if (type & 0x4000) {
    uint32_t base = address & ~4095u;
    uint64_t bytes =
        ((uint64_t)size + (address - base) + 4095) & ~UINT64_C(4095);
    if (!size)
      bytes = (uint64_t)v->address + v->size - base;
    if (base < v->address || !bytes ||
        (uint64_t)base + bytes > (uint64_t)v->address + v->size)
      goto done;
    void *host = (char *)v->host + (base - v->address);
    /* Replacing only our owned pages discards bytes on Linux and Darwin. The
     * reservation and logical binding remain; future commit sees fresh zeros.
     */
    if (!v->logical_protection &&
        mmap(host, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
             -1, 0) != host)
      goto done;
    if (v->logical_protection)
      memset(host, 0, bytes);
    memset(v->access + (base - v->address) / 4096, 0, bytes / 4096);
    result = 1;
  } else if ((type & 0x8000) && address == v->address && !size) {
    if (munmap(v->host, v->size))
      goto done;
#ifdef BB_PORTABLE_RUNTIME
    if (!bb_address_unbind(v->address))
      abort();
#endif
    *slot = v->next;
    free(v->access);
    free(v);
    result = 1;
  }
done:
  pthread_mutex_unlock(&virtual_lock);
  return result;
}
static int bad_pointer(const void *p, U32 size, int writing) {
  if (!size)
    return 0;
  uint32_t address = bb_address_encode(p);
  void *host;
#ifdef BB_PORTABLE_RUNTIME
  if (!address || !bb_address_try_pointer(address, size, &host))
    return 1;
#else
  (void)host;
  if (!address)
    return 1;
#endif
  pthread_mutex_lock(&virtual_lock);
  int bad = 0;
  struct virtual_span *v = virtual_find(address, size);
  if (v) {
    size_t first = (address - v->address) / 4096,
           last = ((uint64_t)address - v->address + size - 1) / 4096;
    for (size_t n = first; n <= last; n++)
      if (!(v->access[n] & (writing ? PROT_WRITE : PROT_READ))) {
        bad = 1;
        break;
      }
  }
  pthread_mutex_unlock(&virtual_lock);
  return bad;
}
static int W IsBadReadPtr(const void *p, U32 size) {
  return bad_pointer(p, size, 0);
}
static int W IsBadWritePtr(void *p, U32 size) {
  return bad_pointer(p, size, 1);
}
static int W IsBadCodePtr(Handle p) { return !p; }
static pthread_mutex_t atomic_lock = PTHREAD_MUTEX_INITIALIZER;
static I32 W InterlockedExchange(I32 *p, I32 value) {
  pthread_mutex_lock(&atomic_lock);
  uint32_t a = bb_address_encode(p), old = BB_U32(a);
  BB_U32(a) = value;
  pthread_mutex_unlock(&atomic_lock);
  return old;
}
static I32 W InterlockedIncrement(I32 *p) {
  pthread_mutex_lock(&atomic_lock);
  uint32_t a = bb_address_encode(p), value = BB_U32(a) + 1u;
  BB_U32(a) = value;
  pthread_mutex_unlock(&atomic_lock);
  return value;
}
static I32 W InterlockedDecrement(I32 *p) {
  pthread_mutex_lock(&atomic_lock);
  uint32_t a = bb_address_encode(p), value = BB_U32(a) - 1u;
  BB_U32(a) = value;
  pthread_mutex_unlock(&atomic_lock);
  return value;
}
static pthread_mutex_t *section_mutex(void *section) {
  uint32_t proxy = BB_U32(bb_address_encode(section));
  return (pthread_mutex_t *)(uintptr_t)BB_U64(proxy + 8u);
}
static void W InitializeCriticalSection(void *section) {
  pthread_mutex_t *mutex = malloc(sizeof(*mutex));
  uint32_t proxy = mutex ? bb_heap_zeroed(1, 16) : 0;
  if (!proxy) {
    free(mutex);
    abort();
  }
  pthread_mutexattr_t attr;
  if (pthread_mutexattr_init(&attr))
    abort();
  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) ||
      pthread_mutex_init(mutex, &attr))
    abort();
  pthread_mutexattr_destroy(&attr);
  BB_U64(proxy + 8u) = (uintptr_t)mutex;
  uint32_t address = bb_address_encode(section);
  memset(BB_SPAN(address, 24), 0, 24);
  BB_U32(address) = proxy;
}
static void W DeleteCriticalSection(void *section) {
  uint32_t address = bb_address_encode(section), proxy = BB_U32(address);
  pthread_mutex_t *mutex = section_mutex(section);
  if (pthread_mutex_destroy(mutex))
    abort();
  free(mutex);
  if (!bb_heap_release(proxy))
    abort();
  BB_U32(address) = 0;
}
static void W EnterCriticalSection(void *section) {
  if (pthread_mutex_lock(section_mutex(section)))
    abort();
}
static void W LeaveCriticalSection(void *section) {
  if (pthread_mutex_unlock(section_mutex(section)))
    abort();
}
