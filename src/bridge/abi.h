/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ABI_H
#define BB_ABI_H
/* These conventions describe the original bridge, not native 64-bit C calls. */
#if defined(__i386__) && !defined(BB_PORTABLE_RUNTIME)
#define BB_THISCALL __attribute__((thiscall))
#define BB_STDCALL __attribute__((stdcall))
#define BB_FASTCALL __attribute__((fastcall))
#else
#define BB_THISCALL
#define BB_STDCALL
#define BB_FASTCALL
#endif
#endif
