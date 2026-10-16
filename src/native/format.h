/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PACKED_FORMAT_H
#define BB_PACKED_FORMAT_H
#include <stdint.h>
/* The Windows shim's vsprintf contract with a four-byte logical argument
 * cursor. Output/format are host pointers, pointer arguments remain logical. */
int bb_packed_sprintf(char *, const char *, uint32_t arguments);
#endif
