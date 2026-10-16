/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ARCHIVE_MODELS_H
#define BB_ARCHIVE_MODELS_H
#include <stdint.h>
enum {
  BB_ARCHIVE_DIRECTORY_BYTES = 544,
  BB_ARCHIVE_ADD_BYTES = 556,
  BB_ARCHIVE_REPLACE_BYTES = 16,
  BB_ARCHIVE_REFRESH_BYTES = 16 + 0x214
};
void *bb_archive_name(void *, const char *);
void *bb_archive_construct(void *, const char *);
void *bb_archive_clear(void *);
uint32_t bb_archive_destroy(void *);
uint32_t bb_archive_view_delete(void *, uint32_t);
uint32_t bb_archive_directory(void *);
uint32_t bb_archive_directory_c(void *, uint32_t);
void *bb_archive_find_name(void *, const char *);
void *bb_archive_find_index(void *, uint32_t);
uint32_t bb_archive_open(void *);
void *bb_archive_close(void *);
void *bb_archive_member(void *, const char *);
uint32_t bb_archive_release(void *);
uint32_t bb_archive_seek(void *, uint32_t, uint32_t, uint32_t);
void *bb_archive_view(void *, void *, const void *);
uint32_t bb_archive_read(void *, void *, uint32_t);
uint32_t bb_archive_read_c(void *, void *, uint32_t, uint32_t);
uint32_t bb_archive_buffer(void);
uint32_t bb_archive_add(void *, const char *);
uint32_t bb_archive_add_c(void *, const char *, uint32_t);
uint32_t bb_archive_replace(void *, void *);
uint32_t bb_archive_replace_c(void *, void *, uint32_t);
uint32_t bb_archive_refresh(void *);
uint32_t bb_archive_refresh_c(void *, uint32_t);
#ifdef BB_MODEL_RUNTIME
#include "model_runtime.h"
const struct bb_model_target *bb_archive_model_find(void *, uint32_t);
#endif
#endif
