/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_BOOTSTRAP_H
#define BUMPKINS_CRT_BOOTSTRAP_H
#include <stdint.h>
void bb_crt_boot_initialize_entry(void);
void bb_crt_boot_exit_entry(uint32_t);
void bb_crt_boot_quick_exit_entry(uint32_t);
void bb_crt_boot_shutdown_entry(uint32_t, int32_t, int32_t);
void bb_crt_boot_initializers_entry(uint32_t *, uint32_t *);
void *bb_crt_boot_new_entry(uint32_t);
void bb_crt_boot_math_entry(void);
void bb_crt_boot_noop_entry(void);
void bb_crt_boot_delete_entry(void *);
void *bb_crt_boot_onexit_entry(void *);
int32_t bb_crt_boot_atexit_entry(void *);
void bb_crt_boot_exit_table_entry(void);
void bb_crt_boot_fatal_entry(int32_t);
void bb_crt_boot_stdio_entry(void);
void bb_crt_boot_end_stdio_entry(void);
void bb_crt_boot_precision_entry(void);
uint32_t bb_crt_boot_divide_probe_entry(void);
uint32_t bb_crt_boot_processor_probe_entry(void);
uint32_t bb_crt_boot_lead_entry(uint32_t);
uint32_t bb_crt_boot_byte_type_entry(uint32_t, uint32_t, uint32_t);
void *bb_crt_boot_environment_entry(void);
void *bb_crt_boot_arguments_entry(void);
void bb_crt_boot_parse_arguments_entry(const uint8_t *, uint32_t *, uint8_t *,
                                       uint32_t *, uint32_t *);
void *bb_crt_boot_capture_environment_entry(void);
int32_t bb_crt_boot_codepage_entry(int32_t);
uint32_t bb_crt_boot_system_codepage_entry(uint32_t);
uint32_t bb_crt_boot_codepage_locale_entry(uint32_t);
void bb_crt_boot_clear_codepage_entry(void);
void bb_crt_boot_initialize_codepage_entry(void);
uint32_t bb_crt_boot_close_streams_entry(void);
#endif
