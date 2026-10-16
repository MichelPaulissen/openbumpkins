/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_DEBUG_HEAP_H
#define BUMPKINS_CRT_DEBUG_HEAP_H
#include <stdint.h>
void *bb_crt_malloc_entry(uint32_t);
void *bb_crt_malloc_debug_entry(uint32_t, uint32_t, const char *, int32_t);
void *bb_crt_malloc_retry_entry(uint32_t, int32_t);
void *bb_crt_malloc_debug_retry_entry(uint32_t, int32_t, uint32_t, const char *,
                                      int32_t);
void *bb_crt_debug_allocate_entry(uint32_t, uint32_t, const char *, int32_t);
void *bb_crt_calloc_entry(uint32_t, uint32_t);
void *bb_crt_calloc_debug_entry(uint32_t, uint32_t, uint32_t, const char *,
                                int32_t);
void *bb_crt_reallocate_debug_entry(void *, uint32_t, uint32_t, const char *,
                                    int32_t);
void *bb_crt_debug_resize_entry(void *, uint32_t, uint32_t, const char *,
                                int32_t, int32_t);
void bb_crt_free_entry(void *);
void bb_crt_free_debug_entry(void *, uint32_t);
uint32_t bb_crt_size_debug_entry(void *, uint32_t);
int32_t bb_crt_debug_check_bytes_entry(const void *, uint32_t, uint32_t);
int32_t bb_crt_debug_check_memory_entry(void);
int32_t bb_crt_debug_set_flags_entry(int32_t);
int32_t bb_crt_debug_valid_pointer_entry(const void *, uint32_t, int32_t);
int32_t bb_crt_debug_valid_heap_entry(const void *);
void bb_crt_debug_checkpoint_entry(uint32_t *);
void bb_crt_debug_dump_objects_entry(const uint32_t *);
void bb_crt_debug_print_data_entry(const void *);
int32_t bb_crt_debug_dump_leaks_entry(void);
#endif
