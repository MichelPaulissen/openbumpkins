/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_QMIXER_VTABLES_H
#define BB_QMIXER_VTABLES_H
#include <stdint.h>
typedef void (*bb_qmix_virtual_method)(void);

#define BB_QMIX_VTABLES(X) \
  X(0x1001a000u, bb_qmix_vtable_decoder_base, 9) \
  X(0x1001a028u, bb_qmix_vtable_pcm, 9) \
  X(0x1001a050u, bb_qmix_vtable_codec, 9) \
  X(0x1001a078u, bb_qmix_vtable_callback, 9) \
  X(0x1001a0f8u, bb_qmix_vtable_session, 22) \
  X(0x1001a150u, bb_qmix_vtable_channel, 26) \
  X(0x1001a1b8u, bb_qmix_vtable_directsound, 22) \
  X(0x1001a210u, bb_qmix_vtable_session_base, 22) \
  X(0x1001a270u, bb_qmix_vtable_base, 26) \
  X(0x1001a2f0u, bb_qmix_vtable_waveout, 22) \
  X(0x1001a348u, bb_qmix_vtable_device, 15) \
  X(0x1001a388u, bb_qmix_vtable_device_base, 15) \
  X(0x1001a3c8u, bb_qmix_vtable_mixer, 8) \
  X(0x1001a3e8u, bb_qmix_vtable_mixer_base, 8) \
  X(0x1001a420u, bb_qmix_vtable_float_ramp, 3) \
  X(0x1001a430u, bb_qmix_vtable_float_accumulator, 8) \
  X(0x1001a450u, bb_qmix_vtable_accumulator_base, 8) \
  X(0x1001a470u, bb_qmix_vtable_float_factory, 5) \
  X(0x1001a490u, bb_qmix_vtable_integer_ramp, 3) \
  X(0x1001a4a0u, bb_qmix_vtable_integer_accumulator, 8) \
  X(0x1001a4c0u, bb_qmix_vtable_integer_factory, 5) \
  X(0x1001a4e0u, bb_qmix_vtable_short_ramp, 3) \
  X(0x1001a4f0u, bb_qmix_vtable_short_accumulator, 8) \
  X(0x1001a510u, bb_qmix_vtable_short_factory, 5) \
  X(0x1001a528u, bb_qmix_vtable_filter, 6) \
  X(0x1001a540u, bb_qmix_vtable_filter_intermediate, 6) \
  X(0x1001a558u, bb_qmix_vtable_filter_base, 3) \
  X(0x1001a568u, bb_qmix_vtable_float_biquad, 6) \
  X(0x1001a588u, bb_qmix_vtable_float_fir, 6) \
  X(0x1001a5a8u, bb_qmix_vtable_float_delay, 4) \
  X(0x1001a5b8u, bb_qmix_vtable_delay_base, 4) \
  X(0x1001a5f8u, bb_qmix_vtable_integer_iir, 6) \
  X(0x1001a638u, bb_qmix_vtable_integer_biquad, 6) \
  X(0x1001a660u, bb_qmix_vtable_integer_fir, 6) \
  X(0x1001a680u, bb_qmix_vtable_integer_delay, 4) \
  X(0x1001a6b0u, bb_qmix_vtable_short_iir, 6) \
  X(0x1001a6e0u, bb_qmix_vtable_short_biquad, 6) \
  X(0x1001a700u, bb_qmix_vtable_short_fir, 6) \
  X(0x1001a720u, bb_qmix_vtable_tap_delay, 4) \
  X(0x1001a734u, bb_qmix_vtable_exception_object, 1)

/* Packed object vptrs always identify four-byte logical method slots. */
#define ADDRESS(address, name, count) name##_address = address,
enum { BB_QMIX_VTABLES(ADDRESS) };
#undef ADDRESS
#ifdef BB_MODEL_RUNTIME
#define BB_QMIX_VTABLE(name) ((uint32_t)name##_address)
#else
#define BB_QMIX_VTABLE(name) ((uintptr_t)name)
#endif

#define VTABLE(address, name, count) extern bb_qmix_virtual_method const name[count];
BB_QMIX_VTABLES(VTABLE)
#undef VTABLE
uintptr_t bb_qmix_virtual_method_id(uintptr_t);
void bb_qmix_rebind_static_vtables(void);
#endif
