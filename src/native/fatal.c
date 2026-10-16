/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
void bb_die(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("bumpkins: ", stderr);
  vfprintf(stderr, format, args);
  va_end(args);
  fputc('\n', stderr);
  exit(1);
}
