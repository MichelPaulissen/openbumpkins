/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_NUMERIC_STATE_H
#define BB_NUMERIC_STATE_H
#include <stdint.h>
/* The game and mixer share one numeric environment on each calling thread.
 * Precision and trap masks remain metadata; C fenv supplies rounding/status. */
uint16_t bb_numeric_word(void);
void bb_numeric_set_word(uint16_t);
uint16_t bb_numeric_status(void);
/* Explicit C operands replace implicit floating registers at internal ABI
 * boundaries. Index zero is the current operand. Packed 80-bit constants and
 * arguments are decoded numerically, independently of host long-double ABI. */
void bb_numeric_push(long double);
void bb_numeric_push_bits(uint64_t);
long double bb_numeric_get(unsigned);
unsigned bb_numeric_signaling(unsigned);
void bb_numeric_set(unsigned, long double);
long double bb_numeric_pop(void);
void bb_numeric_exchange(void);
long double bb_numeric_read_extended(uintptr_t);
void bb_numeric_store_extended(uintptr_t, unsigned);
#endif
