/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_QMIXER_DEVICE_CATCHES_H
#define BB_QMIXER_DEVICE_CATCHES_H
#include <stdint.h>

#define BB_QMIX_DEVICE_CATCHES(X)                                               \
  X(0x1000a46fu, bb_qmix_waveout_latency_catch, 0, 0x1000a3fcu)                 \
  X(0x1000a572u, bb_qmix_waveout_open_catch, 29, 0x1000a54fu)                   \
  X(0x1000a5efu, bb_qmix_waveout_close_catch, 31, 0x1000a5e3u)                  \
  X(0x1000aab7u, bb_qmix_waveout_pump_catch, 30, 0x1000aa76u)
#define BB_QMIX_DEVICE_RETURNS(X)                                               \
  X(0x1000a3fcu, bb_qmix_waveout_latency_resume, 0)                              \
  X(0x1000a54fu, bb_qmix_waveout_open_resume, 4)                                 \
  X(0x1000a5e3u, bb_qmix_waveout_close_resume, 4)                                \
  X(0x1000aa76u, bb_qmix_waveout_pump_resume, 0)
#define DECLARE_CATCH(address, name, error, continuation)                       \
  uint32_t name##_entry(void);
BB_QMIX_DEVICE_CATCHES(DECLARE_CATCH)
#undef DECLARE_CATCH
#define DECLARE_RETURN(address, name, cleanup) uint32_t name##_entry(void);
BB_QMIX_DEVICE_RETURNS(DECLARE_RETURN)
#undef DECLARE_RETURN
#endif
