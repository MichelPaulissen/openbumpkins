/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_QMIXER_TERMINAL_H
#define BUMPKINS_QMIXER_TERMINAL_H
#include <stdint.h>
void bb_qmix_terminal_entry(void);
void bb_qmix_inconsistent_entry(void);
int32_t bb_qmix_terminal_filter_entry(void);
int32_t bb_qmix_inconsistent_filter_entry(void);
void bb_qmix_terminal_catch_entry(void);
void bb_qmix_inconsistent_catch_entry(void);
void bb_qmix_terminal_cleanup_entry(void);
void bb_qmix_inconsistent_cleanup_entry(void);
#endif
