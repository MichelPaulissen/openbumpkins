/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"

/* 0x0046be20: retain the shared seed's wrapping arithmetic and live reread.
 * The original also leaves the old seed in ECX and preserves incoming EDX.
 */
uint64_t bb_random_registers(void) {
  uint32_t seed = BB_U32(0x0048cba4);
  BB_U32(0x0048cba4) = seed * 214013u + 2531011u;
  uint32_t value = (BB_U32(0x0048cba4) >> 16) & 0x7fffu;
  return ((uint64_t)seed << 32) | value;
}
#ifdef _WIN32
#define BB_RANDOM_ASM "_bb_random_registers"
#else
#define BB_RANDOM_ASM "bb_random_registers"
#endif
#if defined(__i386__) && !defined(BB_PORTABLE_RUNTIME)
int32_t __attribute__((naked)) bb_random(void) {
  __asm__ volatile("pushl %edx;call " BB_RANDOM_ASM
                   ";movl %edx,%ecx;popl %edx;andl $0x7fff,%eax;ret");
}
#else
int32_t bb_random(void) { return (int32_t)(uint32_t)bb_random_registers(); }
#endif

/* Typed backend for 0046be00, already bridged by sample audio. */
uint32_t bb_random_seed_entry(uint32_t value) {
  BB_U32(0x0048cba4) = value;
  return value;
}
