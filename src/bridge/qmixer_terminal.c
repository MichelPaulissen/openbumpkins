/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "qmixer_internal.h"
#include "qmixer_terminal.h"
#ifdef _WIN32
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif

/* The callback and terminal policy are C. Temporary owner frames still let
 * legacy callbacks reach their intended failure landing through shared SEH. */
void __attribute__((used, noinline)) bb_qmix_terminal_worker(uintptr_t frame) {
  BB_I32(frame - 4) = 0;
  if (BB_U32(0x1001fcf4)) {
    BB_I32(frame - 4) = 1;
    ((void (*)(void))BB_CODE(BB_POINTER(0x1001fcf4)))();
    BB_I32(frame - 4) = 0;
  }
  BB_I32(frame - 4) = -1;
  bb_qmix_signal_abort_entry();
}
void __attribute__((used, noinline)) bb_qmix_inconsistent_worker(uintptr_t frame) {
  BB_I32(frame - 4) = 0;
  if (BB_U32(0x1001fcfc)) {
    BB_I32(frame - 4) = 1;
    ((void (*)(void))BB_CODE(BB_POINTER(0x1001fcfc)))();
    BB_I32(frame - 4) = 0;
  }
  BB_I32(frame - 4) = -1;
  bb_qmix_terminal_entry();
}
void __attribute__((used, noinline)) bb_qmix_terminal_catch_worker(uintptr_t frame) {
  BB_I32(frame - 4) = -1;
  bb_qmix_signal_abort_entry();
}
void __attribute__((used, noinline)) bb_qmix_inconsistent_catch_worker(uintptr_t frame) {
  BB_I32(frame - 4) = -1;
  bb_qmix_terminal_entry();
}
int32_t bb_qmix_terminal_filter_entry(void) { return 1; }
int32_t bb_qmix_inconsistent_filter_entry(void) { return 1; }
void bb_qmix_terminal_cleanup_entry(void) { bb_qmix_signal_abort_entry(); }
void bb_qmix_inconsistent_cleanup_entry(void) { bb_qmix_terminal_entry(); }

#define RESTORE_FRAME                                                          \
  "movl -16(%ebp),%eax;movl %eax,%fs:0;popl %edi;popl %esi;popl "              \
  "%ebx;leave;ret"
#define GUARDED(name, table)                                                   \
  void __attribute__((naked, noinline)) name##_entry(void) {                   \
    __asm__ volatile(                                                          \
        "pushl %ebp;movl %esp,%ebp;pushl $-1;pushl $" #table                   \
        ";pushl $0x10015b74;movl %fs:0,%eax;pushl %eax;movl %esp,%fs:0;subl "  \
        "$8,%esp;pushl %ebx;pushl %esi;pushl %edi;movl %esp,-24(%ebp);leal "   \
        "-128(%esp),%esp;pushl %ebp;call " ASM_NAME(                           \
            name##_worker) ";movl -24(%ebp),%esp;" RESTORE_FRAME);             \
  }
#define LANDING(name)                                                          \
  void __attribute__((naked, noinline)) name##_entry(void) {                   \
    __asm__ volatile("movl -24(%ebp),%esp;movl $0,-4(%ebp);leal "              \
                     "-128(%esp),%esp;pushl %ebp;call " ASM_NAME(              \
                         name##_worker) ";movl -24(%ebp),%esp;" RESTORE_FRAME); \
  }
GUARDED(bb_qmix_terminal, 0x1001a7c0)
GUARDED(bb_qmix_inconsistent, 0x1001a7d8)
LANDING(bb_qmix_terminal_catch)
LANDING(bb_qmix_inconsistent_catch)

void bb_qmix_terminal_c(uint32_t *r __attribute__((unused))) { bb_qmix_terminal_entry(); }
void bb_qmix_inconsistent_c(uint32_t *r __attribute__((unused))) { bb_qmix_inconsistent_entry(); }
void bb_qmix_terminal_filter_c(uint32_t *r) { r[7] = 1; }
void bb_qmix_inconsistent_filter_c(uint32_t *r) { r[7] = 1; }
void bb_qmix_terminal_catch_c(uint32_t *r) { bb_qmix_terminal_catch_worker(r[2]); }
void bb_qmix_inconsistent_catch_c(uint32_t *r) { bb_qmix_inconsistent_catch_worker(r[2]); }
void bb_qmix_terminal_cleanup_c(uint32_t *r __attribute__((unused))) { bb_qmix_terminal_cleanup_entry(); }
void bb_qmix_inconsistent_cleanup_c(uint32_t *r __attribute__((unused))) { bb_qmix_inconsistent_cleanup_entry(); }
