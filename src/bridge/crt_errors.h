/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_ERRORS_H
#define BUMPKINS_CRT_ERRORS_H
#include <stdint.h>
/* Stack-probe intrinsic: allocation size in EAX, caller ESP adjusted. */
void bb_crt_errors_stack_probe_entry(void);
char *bb_crt_errors_itoa_entry(int32_t, char *, uint32_t);
void bb_crt_errors_digits_entry(uint32_t, char *, uint32_t, int32_t);
void bb_crt_errors_break_entry(void);
int32_t bb_crt_errors_report_entry(int32_t, const char *, int32_t, const char *,
                                   const char *, ...);
int32_t bb_crt_errors_window_entry(int32_t, const char *, const char *, char *,
                                   const char *);
void bb_crt_errors_banner_entry(void);
void bb_crt_errors_message_entry(int32_t);
int32_t bb_crt_errors_message_box_entry(const char *, const char *, uint32_t);
#endif
