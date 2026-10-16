/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_HEAP_H
#define BB_HEAP_H
#include <stddef.h>
#include <stdint.h>

/* The shared native heap returns logical references on the portable runtime.
 * No native pointer is narrowed. Payloads remain valid until release/resize;
 * owners must synchronize access before either operation, as with C malloc.
 * Zero-size requests have distinct releasable storage. */
uint32_t bb_heap_allocate(size_t);
uint32_t bb_heap_zeroed(size_t count, size_t size);
uint32_t bb_heap_resize(uint32_t, size_t);
int bb_heap_release(uint32_t);
int bb_heap_size(uint32_t, size_t *);
/* OS heap adapters can query retained physical capacity and request an
 * in-place resize. Ordinary resize retains its established moving behavior. */
int bb_heap_capacity(uint32_t, size_t *);
uint32_t bb_heap_resize_in_place(uint32_t, size_t);
/* Requested-byte accounting/budget, not host allocator capacity or RSS. The
 * budget cannot fall below live requests. A failed resize preserves its input.
 * Growing resize counts both allocations while copying; shrinking keeps the
 * reference and may retain physical capacity. */
int bb_heap_set_limit(size_t);
void bb_heap_usage(size_t *allocations, size_t *bytes);
#endif
