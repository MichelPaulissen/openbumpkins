/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_entry.h"
#include "abi.h"
#include "crt_bootstrap.h"
#include "crt_descriptor.h"
#include "crt_heap.h"
#include "crt_signals.h"
#include "interop.h"
#include "startup.h"
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#endif
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WIN BB_STDCALL
void __attribute__((used, noinline)) bb_crt_entry_c(uintptr_t frame) {
#ifdef BB_MODEL_RUNTIME
  BB_U32(0x48cbb4) = BB_GAME_CALL(BB_U32(0x6ca454), 0);
#else
  BB_U32(0x48cbb4) = ((uint32_t(WIN *)(void))BB_CODE(BB_POINTER(0x6ca454)))();
#endif
  BB_U32(0x48cbc0) = (BB_U32(0x48cbb4) >> 8) & 255;
  BB_U32(0x48cbbc) = BB_U32(0x48cbb4) & 255;
  BB_U32(0x48cbb8) = (BB_U32(0x48cbbc) << 8) + BB_U32(0x48cbc0);
  BB_U32(0x48cbb4) = (BB_U32(0x48cbb4) >> 16) & 65535;
  if (!bb_crt_heap_initialize_entry())
    bb_crt_boot_fatal_entry(28);
  BB_I32(frame - 4) = 0;
  bb_crt_descriptor_initialize_entry();
  bb_crt_boot_initialize_codepage_entry();
#ifdef BB_MODEL_RUNTIME
  BB_U32(0x6c9bbc) = BB_GAME_CALL(BB_U32(0x6ca464), 0);
#else
  BB_SET_POINTER(0x6c9bbc,
                 ((void *(WIN *)(void))BB_CODE(BB_POINTER(0x6ca464)))());
#endif
  BB_SET_POINTER(0x48cc50, bb_crt_boot_capture_environment_entry());
  if (!BB_U32(0x48cc50) || !BB_U32(0x6c9bbc))
    bb_crt_boot_exit_entry(UINT32_MAX);
  /* Exit is normally terminal. Keep the original continuation if a testing
   * or embedding exit callback returns. */
  bb_crt_boot_arguments_entry();
  bb_crt_boot_environment_entry();
  bb_crt_boot_initialize_entry();
  BB_U32(frame - 100) = BB_U32(0x6c9bbc);
  if (BYTE(BB_U32(frame - 100)) == '"') {
    ++BB_U32(frame - 100);
    while (BYTE(BB_U32(frame - 100)) != '"' && BYTE(BB_U32(frame - 100))) {
      if (bb_crt_boot_lead_entry(BYTE(BB_U32(frame - 100))))
        ++BB_U32(frame - 100);
      ++BB_U32(frame - 100);
    }
    if (BYTE(BB_U32(frame - 100)) == '"')
      ++BB_U32(frame - 100);
  } else
    while (BYTE(BB_U32(frame - 100)) > 32)
      ++BB_U32(frame - 100);
  while (BYTE(BB_U32(frame - 100)) && BYTE(BB_U32(frame - 100)) <= 32)
    ++BB_U32(frame - 100);
  /* STARTUPINFO's unfilled fields remain caller-owned legacy frame bytes. */
  BB_U32(frame - 48) = 0;
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(0x6ca45c), 0, frame - 92);
#else
  ((void(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca45c)))((void *)(frame - 92));
#endif
  BB_U32(frame - 104) =
      BB_U32(frame - 48) & 1 ? BB_U32(frame - 44) & 65535 : 10;
  int32_t show = BB_I32(frame - 104);
  const char *command = BB_POINTER(frame - 100);
#ifdef BB_MODEL_RUNTIME
  uint32_t instance = BB_GAME_CALL(BB_U32(0x6ca39c), 0, 0);
  BB_I32(frame - 96) =
      BB_GAME_CALL(0x406b30, 0, instance, 0, bb_address_encode(command), show);
#else
  void *instance =
      ((void *(WIN *)(const char *))BB_CODE(BB_POINTER(0x6ca39c)))(NULL);
  BB_I32(frame - 96) = bb_game_main_entry(instance, NULL, command, show);
#endif
  bb_crt_boot_exit_entry(BB_U32(frame - 96));
  BB_I32(frame - 4) = -1;
}
void __attribute__((used, noinline)) bb_crt_entry_filter_c(uint32_t *r) {
  uintptr_t frame = r[2], pointers = BB_U32(frame - 20);
  BB_U32(frame - 108) = BB_U32(BB_U32(pointers));
  r[7] = bb_crt_signal_exception_entry(BB_U32(frame - 108), (void *)pointers);
}
void __attribute__((used, noinline)) bb_crt_entry_catch_c(uintptr_t frame) {
  bb_crt_boot_quick_exit_entry(BB_U32(frame - 108));
  BB_I32(frame - 4) = -1;
}
#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif
#define RESTORE_FRAME                                                          \
  "movl -16(%ebp),%eax;movl %eax,%fs:0;popl %edi;popl %esi;popl "              \
  "%ebx;leave;ret"
/* The original scope table and registration remain accessible to the pending
 * C/platform exception dispatcher and original guard callbacks. */
void __attribute__((naked, noinline)) bb_crt_entry_entry(void) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;pushl $-1;pushl $0x482568;"
      "pushl $0x476478;movl %fs:0,%eax;pushl %eax;movl %esp,%fs:0;"
      "subl $8,%esp;subl $84,%esp;pushl %ebx;pushl %esi;pushl %edi;"
      "movl %esp,-24(%ebp);lea -2048(%esp),%esp;pushl %ebp;call " ASM_NAME(
          bb_crt_entry_c) ";movl -24(%ebp),%esp;" RESTORE_FRAME);
}
int32_t __attribute__((naked, noinline)) bb_crt_entry_filter_entry(void) {
  __asm__ volatile(
      "lea -128(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(
          bb_crt_entry_filter_c) ";addl $4,%esp;popal;popfl;lea "
                                 "128(%esp),%esp;ret");
}
void __attribute__((naked, noinline)) bb_crt_entry_catch_entry(void) {
  __asm__ volatile(
      "movl -24(%ebp),%esp;lea -2048(%esp),%esp;pushl %ebp;call " ASM_NAME(
          bb_crt_entry_catch_c) ";movl -24(%ebp),%esp;" RESTORE_FRAME);
}
#endif
