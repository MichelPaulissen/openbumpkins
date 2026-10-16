/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_UNICODE_H
#define BB_UNICODE_H
#include <stdint.h>
uint32_t bb_unicode_case(uint32_t character, int upper);
uint16_t bb_unicode_type(uint16_t character, unsigned level);
#endif
