/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_LOCALE_H
#define BUMPKINS_CRT_LOCALE_H
#include <stdint.h>
/* Shared Windows conversion boundary, including private native C buffers. */
int32_t bb_crt_locale_wide_bytes(uint32_t, uint32_t, const uint16_t *, int32_t,
                                 char *, int32_t, uint32_t *);
int32_t bb_crt_locale_wctomb_entry(char *, uint32_t);
int32_t bb_crt_locale_map_entry(uint32_t, uint32_t, const char *, int32_t,
                                char *, int32_t, uint32_t);
int32_t bb_crt_locale_type_entry(uint32_t, const char *, int32_t, uint16_t *,
                                 uint32_t, uint32_t);
int32_t bb_crt_locale_mbtowc_entry(uint16_t *, const char *, uint32_t);
uint32_t bb_crt_locale_wcstombs_entry(char *, const uint16_t *, uint32_t);
char *bb_crt_locale_getenv_entry(const char *);
int32_t bb_crt_locale_collate_entry(const char *, const char *, uint32_t);
int32_t bb_crt_locale_convert_environment_entry(void);
int32_t bb_crt_locale_compare_entry(uint32_t, uint32_t, const char *, int32_t,
                                    const char *, int32_t, uint32_t);
int32_t bb_crt_locale_setenv_entry(char *, int32_t);
int32_t bb_crt_locale_findenv_entry(const char *, uint32_t);
void *bb_crt_locale_copy_environment_entry(const void *);
char *bb_crt_locale_find_character_entry(const char *, uint32_t);
#endif
