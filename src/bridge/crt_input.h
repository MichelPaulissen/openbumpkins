/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_INPUT_H
#define BUMPKINS_CRT_INPUT_H
#include <stdint.h>
int32_t bb_crt_sscanf_entry(const char *, const char *, ...);
int32_t bb_crt_input_entry(void *, const char *, const void *);
uint32_t bb_crt_scan_read_entry(void *);
void bb_crt_scan_unread_entry(uint32_t, void *);
uint32_t bb_crt_scan_space_entry(uint32_t *, void *);
uint32_t bb_crt_scan_hex_entry(uint32_t);
#endif
