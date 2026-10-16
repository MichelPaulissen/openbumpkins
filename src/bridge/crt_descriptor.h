/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_DESCRIPTOR_H
#define BUMPKINS_CRT_DESCRIPTOR_H
#include <stdint.h>
int32_t bb_crt_descriptor_close_entry(int32_t);
int32_t bb_crt_descriptor_initialize_entry(void);
int32_t bb_crt_descriptor_seek_entry(int32_t, int32_t, int32_t);
int32_t bb_crt_descriptor_write_entry(int32_t, const void *, uint32_t);
int32_t bb_crt_descriptor_read_entry(int32_t, void *, uint32_t);
int32_t bb_crt_descriptor_terminal_entry(int32_t);
void bb_crt_descriptor_error_entry(uint32_t);
int32_t bb_crt_descriptor_acquire_entry(void);
int32_t bb_crt_descriptor_set_handle_entry(int32_t, uint32_t);
int32_t bb_crt_descriptor_free_handle_entry(int32_t);
uint32_t bb_crt_descriptor_get_handle_entry(int32_t);
int32_t bb_crt_descriptor_commit_entry(int32_t);
int32_t bb_crt_descriptor_open_entry(const char *, uint32_t, int32_t, uint32_t);
int32_t bb_crt_descriptor_resize_entry(int32_t, int32_t);
int32_t bb_crt_descriptor_mode_entry(int32_t, int32_t);
#endif
