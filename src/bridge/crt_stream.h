/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_STREAM_H
#define BUMPKINS_CRT_STREAM_H
#include <stdint.h>
int32_t bb_crt_stream_close_entry(void *);
void *bb_crt_stream_shared_open_entry(const char *, const char *, int32_t);
void *bb_crt_stream_open_entry(const char *, const char *);
int32_t bb_crt_stream_tell_entry(void *);
int32_t bb_crt_stream_seek_entry(void *, int32_t, int32_t);
uint32_t bb_crt_stream_read_entry(void *, uint32_t, uint32_t, void *);
uint32_t bb_crt_stream_write_entry(const void *, uint32_t, uint32_t, void *);
int32_t bb_crt_stream_getc_entry(void *);
int32_t bb_crt_stream_write_char_entry(int32_t, void *);
void bb_crt_stream_free_buffer_entry(void *);
int32_t bb_crt_stream_flush_entry(void *);
int32_t bb_crt_stream_flush_buffer_entry(void *);
int32_t bb_crt_stream_flush_all_entry(void);
int32_t bb_crt_stream_flush_table_entry(int32_t);
void *bb_crt_stream_configure_entry(const char *, const char *, int32_t,
                                    void *);
void *bb_crt_stream_acquire_entry(void);
int32_t bb_crt_stream_fill_entry(void *);
void bb_crt_stream_allocate_buffer_entry(void *);
int32_t bb_crt_stream_ungetc_entry(int32_t, void *);
#endif
