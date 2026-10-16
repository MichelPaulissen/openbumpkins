/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_runtime_models.h"
#include "crt_bootstrap.h"
#include "crt_signals.h"
#include "crt_entry.h"
#include "crt_errors.h"
#include "crt_exception_runtime.h"
#include "address.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
#define P(n) ((void *)(uintptr_t)A(n))
extern void bb_crt_errors_report_c(uint32_t *);
static void initializers(uint32_t *r) { (void)r; bb_crt_boot_initializers_entry(P(0), P(1)); }
static void initialize(uint32_t *r) { (void)r; bb_crt_boot_initialize_entry(); }
static void shutdown(uint32_t *r) { (void)r; bb_crt_boot_shutdown_entry(A(0), A(1), A(2)); }
static void exit_process(uint32_t *r) { (void)r; bb_crt_boot_exit_entry(A(0)); }
static void exit_quick(uint32_t *r) { (void)r; bb_crt_boot_quick_exit_entry(A(0)); }
static void fatal(uint32_t *r) { (void)r; bb_crt_boot_fatal_entry(A(0)); }
static void noop(uint32_t *r) { (void)r; bb_crt_boot_noop_entry(); }
static void onexit(uint32_t *r) { r[7] = bb_address_encode(bb_crt_boot_onexit_entry(P(0))); }
static void atexit_callback(uint32_t *r) { r[7] = bb_crt_boot_atexit_entry(P(0)); }
static void exit_table(uint32_t *r) { (void)r; bb_crt_boot_exit_table_entry(); }
static void exception(uint32_t *r) { r[7] = bb_crt_signal_exception_entry(A(0), P(1)); }
static void exception_lookup(uint32_t *r) { r[7] = bb_address_encode(bb_crt_signal_exception_lookup_entry(A(0))); }
static void signal_raise(uint32_t *r) { r[7] = bb_crt_signal_raise_entry(A(0)); }
static void signal_lookup(uint32_t *r) { r[7] = bb_address_encode(bb_crt_signal_lookup_entry(A(0))); }
static void float_trap(uint32_t *r) { (void)r; bb_crt_signal_float_trap_entry(); }
static void math_error(uint32_t *r) { r[7] = bb_crt_signal_math_error_entry(P(0)); }
static void signal_initialize(uint32_t *r) { (void)r; bb_crt_signal_initialize_entry(); }
static void signal_install(uint32_t *r) { (void)r; bb_crt_signal_install_entry(); }
static void unhandled(uint32_t *r) { r[7] = bb_crt_signal_unhandled_entry(P(0)); }
static void restore(uint32_t *r) { (void)r; bb_crt_signal_restore_entry(); }
static void validate_read(uint32_t *r) { r[7] = bb_crt_signal_validate_read_entry(P(0), A(1)); }
static void validate_write(uint32_t *r) { r[7] = bb_crt_signal_validate_write_entry(P(0), A(1)); }
static void validate_code(uint32_t *r) { r[7] = bb_crt_signal_validate_code_entry(P(0)); }
static void signal_abort(uint32_t *r) { (void)r; bb_crt_signal_abort_entry(); }
static void signal_terminate(uint32_t *r) { (void)r; bb_crt_signal_terminate_entry(); }
static void inconsistent(uint32_t *r) { (void)r; bb_crt_signal_inconsistent_entry(); }
static void terminate_filter(uint32_t *r) { r[7] = bb_crt_signal_terminate_filter_entry(); }
static void inconsistent_filter(uint32_t *r) { r[7] = bb_crt_signal_inconsistent_filter_entry(); }
static void terminate_catch(uint32_t *r) { (void)r; bb_crt_signal_terminate_catch_entry(); }
static void inconsistent_catch(uint32_t *r) { (void)r; bb_crt_signal_inconsistent_catch_entry(); }
static void abort_cleanup(uint32_t *r) { (void)r; bb_crt_signal_abort_cleanup_entry(); }
static void terminate_cleanup(uint32_t *r) { (void)r; bb_crt_signal_terminate_cleanup_entry(); }
static void itoa_value(uint32_t *r) { r[7] = bb_address_encode(bb_crt_errors_itoa_entry(A(0), P(1), A(2))); }
static void digits(uint32_t *r) { (void)r; bb_crt_errors_digits_entry(A(0), P(1), A(2), A(3)); }
static void breakpoint(uint32_t *r) { (void)r; bb_crt_errors_break_entry(); }
static void window(uint32_t *r) { r[7] = bb_crt_errors_window_entry(A(0), P(1), P(2), P(3), P(4)); }
static void banner(uint32_t *r) { (void)r; bb_crt_errors_banner_entry(); }
static void message(uint32_t *r) { (void)r; bb_crt_errors_message_entry(A(0)); }
static void message_box(uint32_t *r) { r[7] = bb_crt_errors_message_box_entry(P(0), P(1), A(2)); }
extern void bb_crt_entry_c(uintptr_t);
extern void bb_crt_entry_filter_c(uint32_t *);
extern void bb_crt_entry_catch_c(uintptr_t);
static void entry_body(uint32_t *r) { bb_crt_entry_c(r[2]); }
static void executable_entry(uint32_t *r) {
  uint32_t prior = r[2], frame = r[3] + 256 + 4, error;
  r[2] = frame;
  if (!bb_model_try_body(entry_body, r, &error)) {
    BB_U32(frame - 108) = error;
    bb_crt_entry_catch_c(frame);
  }
  r[2] = prior;
}
static void entry_catch(uint32_t *r) { bb_crt_entry_catch_c(r[2]); }
static const struct bb_model_target targets[] = {
    {0x0046d3d0u, executable_entry, 0, 256, 0},
    {0x0046d5d0u, bb_crt_entry_filter_c, 0, 0, 0},
    {0x0046d5f0u, entry_catch, 0, 0, 0},
    {0x0046c020u, initializers, 2, 0, 0},
    {0x0046be90u, initialize, 0, 0, 0},
    {0x0046bf20u, shutdown, 3, 0, 0},
    {0x0046bee0u, exit_process, 1, 0, 0},
    {0x0046bf00u, exit_quick, 1, 0, 0},
    {0x0046d620u, fatal, 1, 0, 0},
    {0x0046c0b0u, noop, 0, 0, 0},
    {0x0046c320u, onexit, 1, 0, 0},
    {0x0046c3e0u, atexit_callback, 1, 0, 0},
    {0x0046c410u, exit_table, 0, 0, 0},
    {0x004753e0u, exception, 2, 0, 0},
    {0x004755e0u, exception_lookup, 1, 0, 0},
    {0x0047a320u, signal_raise, 1, 0, 0},
    {0x0047a540u, signal_lookup, 1, 0, 0},
    {0x0047b620u, float_trap, 0, 0, 0},
    {0x0047e7d0u, math_error, 1, 0, 0},
    {0x0047e7f0u, signal_initialize, 0, 0, 0},
    {0x0047e810u, signal_install, 0, 0, 0},
    {0x0047e840u, unhandled, 1, 0, 0},
    {0x0047e8e0u, restore, 0, 0, 0},
    {0x0047e900u, validate_read, 2, 0, 0},
    {0x0047e940u, validate_write, 2, 0, 0},
    {0x0047e980u, validate_code, 1, 0, 0},
    {0x0047e9c0u, signal_abort, 0, 0, 0},
    {0x00479d30u, signal_terminate, 0, 0, 0},
    {0x00479dd0u, inconsistent, 0, 0, 0},
    {0x00479d83u, terminate_filter, 0, 0, 0},
    {0x00479e23u, inconsistent_filter, 0, 0, 0},
    {0x00479d8eu, terminate_catch, 0, 0, 0},
    {0x00479e2eu, inconsistent_catch, 0, 0, 0},
    {0x00479da9u, abort_cleanup, 0, 0, 0},
    {0x00479e49u, terminate_cleanup, 0, 0, 0},
    {0x0046fbe0u, itoa_value, 3, 0, 0},
    {0x0046fc40u, digits, 4, 0, 0},
    {0x00470ed0u, breakpoint, 0, 0, 0},
    {0x004712e0u, window, 5, 0, 0},
    {0x00476550u, banner, 0, 0, 0},
    {0x004765b0u, message, 1, 0, 0},
    {0x0047a5b0u, message_box, 3, 0, 0},
    {0x00470ef0u, bb_crt_errors_report_c, 5 | BB_MODEL_VARIADIC, 16384, 0},
};
const struct bb_model_target *bb_crt_runtime_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return bb_crt_exception_model_find(NULL, id);
}
