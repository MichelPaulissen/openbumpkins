/* SPDX-License-Identifier: GPL-3.0-only */
#include "call_abi.h"

#if !defined(__i386__)
#error "The legacy call backend requires i386; a portable backend is separate"
#endif

/* One implementation of the working 32-bit boundary. New host backends can
 * replace this file without changing the translated callers' state contract.
 * The floating variant consumes the callee's x87 result into explicit storage.
 */
void __attribute__((naked, noinline, noipa))
bb_call_words6(struct bb_call_state *state __attribute__((unused)),
               uintptr_t target __attribute__((unused)),
               const uint32_t *args __attribute__((unused))) {
  __asm__ volatile("pushl %ebp;movl %esp,%ebp;pushl %ebx;pushl %esi;pushl %edi;"
                   "movl 8(%ebp),%ebx;movl 16(%ebp),%esi;"
                   "pushl 20(%esi);pushl 16(%esi);pushl 12(%esi);pushl 8(%esi);"
                   "pushl 4(%esi);pushl (%esi);"
                   "movl (%ebx),%eax;movl 4(%ebx),%ecx;movl 8(%ebx),%edx;"
                   "pushl 12(%ebx);popfl;call *12(%ebp);pushfl;popl %edi;"
                   "movl %eax,(%ebx);movl %ecx,4(%ebx);movl %edx,8(%ebx);"
                   "movl %edi,12(%ebx);leal -12(%ebp),%esp;"
                   "popl %edi;popl %esi;popl %ebx;popl %ebp;ret");
}

void __attribute__((naked, noinline, noipa))
bb_call_words10(struct bb_call_state *state __attribute__((unused)),
                uintptr_t target __attribute__((unused)),
                const uint32_t *args __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;pushl %ebx;pushl %esi;pushl %edi;"
      "movl 8(%ebp),%ebx;movl 16(%ebp),%esi;"
      "pushl 36(%esi);pushl 32(%esi);pushl 28(%esi);pushl 24(%esi);"
      "pushl 20(%esi);pushl 16(%esi);pushl 12(%esi);pushl 8(%esi);"
      "pushl 4(%esi);pushl (%esi);"
      "movl (%ebx),%eax;movl 4(%ebx),%ecx;movl 8(%ebx),%edx;"
      "pushl 12(%ebx);popfl;call *12(%ebp);pushfl;popl %edi;"
      "movl %eax,(%ebx);movl %ecx,4(%ebx);movl %edx,8(%ebx);"
      "movl %edi,12(%ebx);leal -12(%ebp),%esp;"
      "popl %edi;popl %esi;popl %ebx;popl %ebp;ret");
}

void __attribute__((naked, noinline, noipa))
bb_call_words16(struct bb_call_state *state __attribute__((unused)),
                uintptr_t target __attribute__((unused)),
                const uint32_t *args __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;pushl %ebx;pushl %esi;pushl %edi;"
      "movl 8(%ebp),%ebx;movl 16(%ebp),%esi;"
      "pushl 60(%esi);pushl 56(%esi);pushl 52(%esi);pushl 48(%esi);"
      "pushl 44(%esi);pushl 40(%esi);"
      "pushl 36(%esi);pushl 32(%esi);pushl 28(%esi);pushl 24(%esi);"
      "pushl 20(%esi);pushl 16(%esi);pushl 12(%esi);pushl 8(%esi);"
      "pushl 4(%esi);pushl (%esi);"
      "movl (%ebx),%eax;movl 4(%ebx),%ecx;movl 8(%ebx),%edx;"
      "pushl 12(%ebx);popfl;call *12(%ebp);pushfl;popl %edi;"
      "movl %eax,(%ebx);movl %ecx,4(%ebx);movl %edx,8(%ebx);"
      "movl %edi,12(%ebx);leal -12(%ebp),%esp;"
      "popl %edi;popl %esi;popl %ebx;popl %ebp;ret");
}

void __attribute__((naked, noinline, noipa))
bb_call_float10(struct bb_call_state *state __attribute__((unused)),
                uintptr_t target __attribute__((unused)),
                const uint32_t *args __attribute__((unused)),
                long double *value __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;pushl %ebx;pushl %esi;pushl %edi;"
      "movl 8(%ebp),%ebx;movl 16(%ebp),%esi;"
      "pushl 36(%esi);pushl 32(%esi);pushl 28(%esi);pushl 24(%esi);"
      "pushl 20(%esi);pushl 16(%esi);pushl 12(%esi);pushl 8(%esi);"
      "pushl 4(%esi);pushl (%esi);"
      "movl (%ebx),%eax;movl 4(%ebx),%ecx;movl 8(%ebx),%edx;"
      "pushl 12(%ebx);popfl;call *12(%ebp);pushfl;popl %edi;"
      "movl %eax,(%ebx);movl %ecx,4(%ebx);movl %edx,8(%ebx);"
      "movl %edi,12(%ebx);movl 20(%ebp),%esi;fstpt (%esi);leal -12(%ebp),%esp;"
      "popl %edi;popl %esi;popl %ebx;popl %ebp;ret");
}
