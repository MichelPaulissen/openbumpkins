/* SPDX-License-Identifier: GPL-3.0-only */
#include "codepage.h"
#include "kernel_handle.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The registered filter is an opaque code identity in the portable runtime. */
static _Atomic(void *) exception_filter;
void *bb_exception_filter_get(void) { return atomic_load(&exception_filter); }
static Handle W SetUnhandledExceptionFilter(Handle filter) {
  return atomic_exchange(&exception_filter, filter);
}
static I32 W UnhandledExceptionFilter(void *pair) {
  (void)pair;
  return 0;
}
static U32 W GetVersion(void) { return 0x80000004; }
static U32 W GetACP(void) { return 1252; }
static U32 W GetOEMCP(void) { return 437; }
static int W GetCPInfo(U32 page, void *info) {
  if (!info || !bb_codepage_id(page))
    return bb_kernel_error(87);
  memset(info, 0, 20);
  *(U32 *)info = 1;
  ((char *)info)[4] = '?';
  return 1;
}
static char *W GetCommandLineA(void) { return "BEASTS.EXE"; }
static void W GetStartupInfoA(void *info) {
  memset(info, 0, 68);
  *(U32 *)info = 68;
  ((U32 *)info)[14] = 1;
  ((U32 *)info)[15] = 2;
  ((U32 *)info)[16] = 3;
}
static void W OutputDebugStringA(const char *text) {
  fprintf(stderr, "BEASTS: %s\n", text);
}
static U32 W lstrlenA(const char *text) { return (U32)strlen(text); }
static char *W lstrcpyA(char *destination, const char *source) {
  return strcpy(destination, source);
}
static int W MulDiv(int a, int b, int c) {
  if (!c)
    return -1;
  int64_t product = (int64_t)a * b;
  int64_t divisor = c;
  int64_t half = (divisor < 0 ? -divisor : divisor) / 2;
  return (int)((product + (product < 0 ? -half : half)) / divisor);
}

void *bb_system_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(GetVersion),
      API_ENTRY(GetACP),
      API_ENTRY(GetOEMCP),
      API_ENTRY(GetCPInfo),
      API_ENTRY(GetCommandLineA),
      API_ENTRY(GetStartupInfoA),
      API_ENTRY(OutputDebugStringA),
      API_ENTRY(lstrlenA),
      API_ENTRY(lstrcpyA),
      API_ENTRY(MulDiv),
      API_ENTRY(SetUnhandledExceptionFilter),
      API_ENTRY(UnhandledExceptionFilter)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "system_models.inc"
#endif
