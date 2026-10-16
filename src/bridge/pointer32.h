/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_POINTER32_H
#define BB_POINTER32_H
#include <stdint.h>
#include <stdlib.h>
#include "address.h"

/* Packed game records contain four-byte addresses, including unaligned slots.
 * A native void ** load/store would touch eight bytes on a 64-bit host.
 * The alignment annotation lets the compiler select a valid access sequence.
 */
typedef uint32_t bb_pointer_word __attribute__((aligned(1), may_alias));
_Static_assert(sizeof(bb_pointer_word) == 4, "packed game pointer width");
_Static_assert(_Alignof(bb_pointer_word) == 1, "packed game pointer alignment");

static inline void *bb_pointer32_load(uintptr_t slot) {
  /* Preserve code/data identities as words. BB_DATA resolves a data reference
   * when a caller needs actual host bytes; BB_CODE resolves code separately. */
  return (void *)(uintptr_t)*(const volatile bb_pointer_word *)BB_SPAN(slot, 4);
}

static inline void *bb_pointer32_store(uintptr_t slot, const void *value) {
  uint32_t address = bb_address_encode(value);
  *(volatile bb_pointer_word *)BB_SPAN(slot, 4) = address;
  return (void *)value;
}
#endif
