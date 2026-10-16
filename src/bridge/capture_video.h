/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_CAPTURE_VIDEO_H
#define BUMPKINS_CAPTURE_VIDEO_H
#include <stdint.h>
void *bb_numbered_file_entry(const char *, const char *, char *);
void BB_THISCALL bb_screenshot_entry(void *, const char *);
void BB_THISCALL bb_video_entry(void *, const char *);
#endif
