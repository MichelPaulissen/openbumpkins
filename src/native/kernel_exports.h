/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_KERNEL_EXPORTS_H
#define BB_KERNEL_EXPORTS_H
#define BB_KERNEL_BASE_EXPORTS(X)                                              \
  X(ExitProcess)                                                               \
  X(TerminateProcess)                                                          \
  X(GetCurrentProcess)                                                         \
  X(GetCurrentThread)                                                          \
  X(GetCurrentProcessId)                                                       \
  X(GetCurrentThreadId)                                                        \
  X(SetThreadPriority)                                                         \
  X(RaiseException)                                                            \
  X(RtlUnwind)                                                                 \
  X(DebugBreak)                                                                \
  X(CreateThread)
#endif
