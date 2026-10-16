/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* 0x10015c50..0x10015d9e: overlap-safe copying. The original body contains
 * two sixteen-byte tables for aligned tails; ordinary C memmove handles
 * both copy directions and every alignment. Zero length accesses neither
 * operand, including null operands. Callers follow the DF-clear C ABI. */
void bb_qmix_crt_memmove_c(uint32_t *input) {
  uint32_t p = input[3] + 2052u;
  uint32_t destination = BB_U32(p + 4u), source = BB_U32(p + 8u);
  uint32_t bytes = BB_U32(p + 12u);
  if (bytes) memmove((void *)(uintptr_t)destination,
                     (const void *)(uintptr_t)source, bytes);
  input[7] = destination;
}
uint32_t __attribute__((naked)) bb_qmix_crt_memmove_entry(
    uint32_t destination __attribute__((unused)),
    uint32_t source __attribute__((unused)), uint32_t bytes __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_crt_memmove_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret");
}
