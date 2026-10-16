/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_HEAP_H
#define BUMPKINS_CRT_HEAP_H
#include <stdint.h>
int32_t bb_crt_heap_initialize_entry(void);
int32_t bb_crt_heap_retry_entry(uint32_t);
void *bb_crt_heap_malloc_entry(uint32_t);
void *bb_crt_heap_malloc_retry_entry(uint32_t, int32_t);
void *bb_crt_heap_allocate_entry(uint32_t);
int32_t bb_crt_heap_permission_entry(void);
void *bb_crt_heap_expand_entry(void *, uint32_t);
void *bb_crt_heap_reallocate_entry(void *, uint32_t);
void bb_crt_heap_free_entry(void *);
int32_t bb_crt_heap_check_entry(void);
void *bb_crt_heap_region_create_entry(void);
void bb_crt_heap_region_release_entry(void *);
void bb_crt_heap_decommit_entry(int32_t);
void *bb_crt_heap_find_entry(void *, uint32_t *, uint32_t *);
void bb_crt_heap_block_free_entry(void *, void *, void *);
void *bb_crt_heap_block_allocate_entry(uint32_t);
void *bb_crt_heap_page_allocate_entry(void *, uint32_t, uint32_t);
int32_t bb_crt_heap_block_resize_entry(void *, void *, void *, uint32_t);
int32_t bb_crt_heap_pool_check_entry(void);
#endif
