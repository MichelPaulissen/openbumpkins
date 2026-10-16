/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ADDRESS_H
#define BB_ADDRESS_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef BB_PORTABLE_RUNTIME
/* Bind nonoverlapping game address ranges to independently placed host data.
 * These functions borrow storage. Unbinding requires the owner to stop all
 * users of that storage first, just as freeing a shared allocation does.
 */
int bb_address_bind(uint32_t, void *, size_t);
/* Atomically choose an aligned free logical span in [first, limit), borrowing
 * the supplied host storage just like bind. limit may be 2^32. */
int bb_address_bind_auto(uint32_t first, uint64_t limit, void *, size_t,
                          uint32_t alignment, uint32_t *address);
int bb_address_unbind(uint32_t);
int bb_address_try_pointer(uintptr_t, size_t, void **);
int bb_address_try_encode(const void *, uint32_t *);
/* Four-byte numeric addresses use the logical space; wider native pointers
 * pass through. Keep logical address arithmetic in uint32_t before resolving.
 */
void *bb_address_pointer(uintptr_t, size_t);
uint32_t bb_address_encode(const void *);
#else
static inline void *bb_address_pointer(uintptr_t address, size_t size) {
  (void)size;
  return (void *)address;
}
static inline uint32_t bb_address_encode(const void *pointer) {
  uintptr_t address = (uintptr_t)pointer;
#if UINTPTR_MAX > UINT32_MAX
  if (address > UINT32_MAX)
    abort();
#endif
  return (uint32_t)address;
}
#endif

#define BB_DATA(address) bb_address_pointer((uintptr_t)(address), 1)
#define BB_SPAN(address, size) bb_address_pointer((uintptr_t)(address), (size))

/* All packed fields may be unaligned. Keep their widths independent of the
 * host, and tell the compiler the real alignment and aliasing requirements.
 */
#define BB_MEMORY_TYPE(name, type)                                             \
  typedef type name __attribute__((aligned(1), may_alias))
BB_MEMORY_TYPE(bb_memory_u8, uint8_t);
BB_MEMORY_TYPE(bb_memory_u16, uint16_t);
BB_MEMORY_TYPE(bb_memory_i16, int16_t);
BB_MEMORY_TYPE(bb_memory_u32, uint32_t);
BB_MEMORY_TYPE(bb_memory_i32, int32_t);
BB_MEMORY_TYPE(bb_memory_u64, uint64_t);
BB_MEMORY_TYPE(bb_memory_f32, float);
BB_MEMORY_TYPE(bb_memory_f64, double);
#undef BB_MEMORY_TYPE
#define BB_U8(a) (*(volatile bb_memory_u8 *)BB_SPAN(a, 1))
#define BB_U16(a) (*(volatile bb_memory_u16 *)BB_SPAN(a, 2))
#define BB_I16(a) (*(volatile bb_memory_i16 *)BB_SPAN(a, 2))
#define BB_U32(a) (*(volatile bb_memory_u32 *)BB_SPAN(a, 4))
#define BB_I32(a) (*(volatile bb_memory_i32 *)BB_SPAN(a, 4))
#define BB_U64(a) (*(volatile bb_memory_u64 *)BB_SPAN(a, 8))
#define BB_FLOAT(a) (*(volatile bb_memory_f32 *)BB_SPAN(a, 4))
#define BB_DOUBLE(a) (*(volatile bb_memory_f64 *)BB_SPAN(a, 8))
#endif
