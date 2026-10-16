/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_bootstrap.h"
#include "crt_math_base.h"
#include "crt_format.h"
#include "game_calls.h"
#ifdef BB_MODEL_RUNTIME
#include <fenv.h>
#endif
#define WIN BB_STDCALL
#define STR(a) ((const char *)(uintptr_t)(a))
void bb_crt_boot_precision_entry(void) {
  bb_crt_math_control_safe_entry(0x10000, 0x30000);
}
uint32_t bb_crt_boot_divide_probe_entry(void) {
#ifdef BB_MODEL_RUNTIME
  /* This detects the 1994 Pentium FDIV hardware defect. Supported native
   * processors implement division correctly, so no workaround is needed. */
  return 0;
#else
  const uint64_t denominator = UINT64_C(0x4147ffff80000000),
                 numerator = UINT64_C(0x4150017ec0000000);
  uint64_t residual;
  uint16_t status;
  __asm__ volatile(
      "fldl %3;fdivl %2;fmull %2;fsubrl %3;fstpl %0;fldl %0;fcompl %4;fnstsw %1"
      : "=m"(residual), "=a"(status)
      : "m"(denominator), "m"(numerator), "m"(BB_DOUBLE(0x482f68))
      : "st");
  return !(status & 0x4100);
#endif
}
uint32_t bb_crt_boot_processor_probe_entry(void) {
#ifdef BB_MODEL_RUNTIME
  uint32_t module = BB_GAME_CALL(BB_U32(0x6ca39c), 0, 0x482f8c);
  if (module) {
    uint32_t function = BB_GAME_CALL(BB_U32(0x6ca4ac), 0, module, 0x482f70);
    if (function)
      return BB_GAME_CALL(function, 0, 0);
  }
#else
  void *module =
      ((void *(WIN *)(const char *))BB_CODE(BB_POINTER(0x6ca39c)))(STR(0x482f8c));
  if (module) {
    void *function = ((void *(WIN *)(void *, const char *))BB_CODE(BB_POINTER(
        0x6ca4ac)))(module, STR(0x482f70));
    if (function)
      return ((uint32_t(WIN *)(uint32_t))BB_CODE(function))(0);
  }
#endif
  return bb_crt_boot_divide_probe_entry();
}
void bb_crt_boot_math_entry(void) {
  bb_crt_format_initialize_entry();
  BB_U32(0x48cc04) = bb_crt_boot_processor_probe_entry();
  bb_crt_boot_precision_entry();
#ifdef BB_MODEL_RUNTIME
  feclearexcept(FE_ALL_EXCEPT);
#else
  __asm__ volatile("fnclex");
#endif
}
