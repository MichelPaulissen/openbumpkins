/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_CRT_SIGNALS_H
#define BUMPKINS_CRT_SIGNALS_H
#include <stdint.h>
int32_t bb_crt_signal_exception_entry(uint32_t, const void *);
void *bb_crt_signal_exception_lookup_entry(uint32_t);
int32_t bb_crt_signal_raise_entry(int32_t);
void *bb_crt_signal_lookup_entry(int32_t);
void bb_crt_signal_float_trap_entry(void);
int32_t bb_crt_signal_math_error_entry(void *);
void bb_crt_signal_initialize_entry(void);
void bb_crt_signal_install_entry(void);
int32_t BB_STDCALL bb_crt_signal_unhandled_entry(const void *);
void bb_crt_signal_restore_entry(void);
int32_t bb_crt_signal_validate_read_entry(const void *, uint32_t);
int32_t bb_crt_signal_validate_write_entry(void *, uint32_t);
int32_t bb_crt_signal_validate_code_entry(const void *);
void bb_crt_signal_abort_entry(void);
void bb_crt_signal_terminate_entry(void);
void bb_crt_signal_inconsistent_entry(void);
int32_t bb_crt_signal_terminate_filter_entry(void);
int32_t bb_crt_signal_inconsistent_filter_entry(void);
void bb_crt_signal_terminate_catch_entry(void);
void bb_crt_signal_inconsistent_catch_entry(void);
void bb_crt_signal_abort_cleanup_entry(void);
void bb_crt_signal_terminate_cleanup_entry(void);
#endif
