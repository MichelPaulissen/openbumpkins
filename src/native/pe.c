/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <asm/ldt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

static _Thread_local U32 *thread_teb;
void bb_init_teb(void) {
  if (thread_teb)
    return;
  U32 *teb = calloc(1, 4096);
  if (!teb)
    bb_die("TEB allocation failed");
  teb[0] = UINT32_MAX;
  teb[1] = (U32)(uintptr_t)&teb + 0x100000;
  teb[2] = 0x10000;
  teb[6] = (U32)(uintptr_t)teb;
  teb[8] = (U32)getpid();
  teb[9] = (U32)syscall(SYS_gettid);
  struct user_desc descriptor = {0};
  U16 inherited_selector;
  __asm__ volatile("movw %%fs, %0" : "=rm"(inherited_selector));
  descriptor.entry_number =
      inherited_selector ? inherited_selector / 8 : UINT32_MAX;
  descriptor.base_addr = (U32)(uintptr_t)teb;
  descriptor.limit = 0xfffff;
  descriptor.seg_32bit = 1;
  descriptor.limit_in_pages = 1;
  descriptor.useable = 1;
  if (syscall(SYS_set_thread_area, &descriptor))
    bb_die("cannot establish the Win32 FS segment");
  U16 selector = (U16)(descriptor.entry_number * 8 + 3);
  __asm__ volatile("movw %0, %%fs" ::"rm"(selector));
  thread_teb = teb;
}
void bb_free_teb(void) {
  if (!thread_teb)
    return;
  /* All guest callbacks/exception frames have returned before worker cleanup.
   * Native i386 libc TLS uses GS; stop exposing the freed guest storage in FS. */
  U16 selector = 0;
  __asm__ volatile("movw %0, %%fs" ::"rm"(selector));
  free(thread_teb);
  thread_teb = NULL;
}
void bb_patch_jump(U32 address, void (*target)(void)) {
  unsigned char *code = (unsigned char *)(uintptr_t)address;
  code[0] = 0xe9;
  U32 relative = (U32)(uintptr_t)target - address - 5;
  memcpy(code + 1, &relative, 4);
}
static void missing(const char *name) {
  bb_die("unimplemented Win32 call: %s", name);
}
static void *trap(const char *dll, const char *name) {
  char *label;
  if (asprintf(&label, "%s!%s", dll, name) < 0)
    bb_die("out of memory");
  unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (code == MAP_FAILED)
    bb_die("cannot allocate import trap");
  code[0] = 0x68;
  U32 pointer = (U32)(uintptr_t)label;
  memcpy(code + 1, &pointer, 4);
  code[5] = 0xb8;
  pointer = (U32)(uintptr_t)missing;
  memcpy(code + 6, &pointer, 4);
  code[10] = 0xff;
  code[11] = 0xd0;
  code[12] = 0x0f;
  code[13] = 0x0b;
  return code;
}
void *bb_api(const char *dll, const char *name) {
  void *result = bb_library_api(dll, name);
  return result ? result : trap(dll, name);
}
