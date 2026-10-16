/* SPDX-License-Identifier: GPL-3.0-only */
#include "memory_models.h"
static uint32_t memory_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
#define A(n) memory_arg(r, n)
#define P(n) ((void *)(uintptr_t)A(n))
static void create(uint32_t *r) {
  r[7] = bb_address_encode(HeapCreate(A(0), A(1), A(2)));
}
static void destroy(uint32_t *r) { r[7] = HeapDestroy(P(0)); }
static void allocate(uint32_t *r) {
  r[7] = bb_address_encode(HeapAlloc(P(0), A(1), A(2)));
}
static void release(uint32_t *r) { r[7] = HeapFree(P(0), A(1), P(2)); }
static void resize(uint32_t *r) {
  r[7] = bb_address_encode(HeapReAlloc(P(0), A(1), P(2), A(3)));
}
static void capacity(uint32_t *r) { r[7] = HeapSize(P(0), A(1), P(2)); }
static void validate(uint32_t *r) { r[7] = HeapValidate(P(0), A(1), P(2)); }
static void global_allocate(uint32_t *r) {
  r[7] = bb_address_encode(GlobalAlloc(A(0), A(1)));
}
static void global_free(uint32_t *r) {
  r[7] = bb_address_encode(GlobalFree(P(0)));
}
static void global_lock(uint32_t *r) {
  r[7] = bb_address_encode(GlobalLock(P(0)));
}
static void global_unlock(uint32_t *r) { r[7] = GlobalUnlock(P(0)); }
static void virtual_allocate(uint32_t *r) {
  r[7] = bb_address_encode(VirtualAlloc(P(0), A(1), A(2), A(3)));
}
static void virtual_free(uint32_t *r) { r[7] = VirtualFree(P(0), A(1), A(2)); }
static void bad_read(uint32_t *r) { r[7] = IsBadReadPtr(P(0), A(1)); }
static void bad_write(uint32_t *r) { r[7] = IsBadWritePtr(P(0), A(1)); }
static void bad_code(uint32_t *r) { r[7] = IsBadCodePtr(P(0)); }
static void exchange(uint32_t *r) { r[7] = InterlockedExchange(P(0), A(1)); }
static void increment(uint32_t *r) { r[7] = InterlockedIncrement(P(0)); }
static void decrement(uint32_t *r) { r[7] = InterlockedDecrement(P(0)); }
static void initialize_section(uint32_t *r) { InitializeCriticalSection(P(0)); }
static void delete_section(uint32_t *r) { DeleteCriticalSection(P(0)); }
static void enter_section(uint32_t *r) { EnterCriticalSection(P(0)); }
static void leave_section(uint32_t *r) { LeaveCriticalSection(P(0)); }
#undef P
#undef A
#define API(n, name, worker, words)                                            \
  {#name, {BB_MEMORY_MODEL_API + (n) * 4u, worker, words, 0, 0}}
static const struct {
  const char *name;
  struct bb_model_target target;
} memory_targets[] = {API(0, HeapCreate, create, 3),
                      API(1, HeapDestroy, destroy, 1),
                      API(2, HeapAlloc, allocate, 3),
                      API(3, HeapFree, release, 3),
                      API(4, HeapReAlloc, resize, 4),
                      API(5, HeapSize, capacity, 3),
                      API(6, HeapValidate, validate, 3),
                      API(7, GlobalAlloc, global_allocate, 2),
                      API(8, GlobalFree, global_free, 1),
                      API(9, GlobalLock, global_lock, 1),
                      API(10, GlobalUnlock, global_unlock, 1),
                      API(11, VirtualAlloc, virtual_allocate, 4),
                      API(12, VirtualFree, virtual_free, 3),
                      API(13, IsBadReadPtr, bad_read, 2),
                      API(14, IsBadWritePtr, bad_write, 2),
                      API(15, IsBadCodePtr, bad_code, 1),
                      API(16, InterlockedExchange, exchange, 2),
                      API(17, InterlockedIncrement, increment, 1),
                      API(18, InterlockedDecrement, decrement, 1),
                      API(19, InitializeCriticalSection, initialize_section, 1),
                      API(20, DeleteCriticalSection, delete_section, 1),
                      API(21, EnterCriticalSection, enter_section, 1),
                      API(22, LeaveCriticalSection, leave_section, 1)};
#undef API
uint32_t bb_memory_model_export(const char *name) {
  for (unsigned n = 0; n < sizeof memory_targets / sizeof *memory_targets; n++)
    if (!strcmp(name, memory_targets[n].name))
      return memory_targets[n].target.identity;
  return 0;
}
const struct bb_model_target *bb_memory_model_find(void *ctx, uint32_t id) {
  (void)ctx;
  if (id < BB_MEMORY_MODEL_API || (id - BB_MEMORY_MODEL_API) % 4u)
    return NULL;
  uint32_t n = (id - BB_MEMORY_MODEL_API) / 4u;
  return n < sizeof memory_targets / sizeof *memory_targets
             ? &memory_targets[n].target
             : NULL;
}
