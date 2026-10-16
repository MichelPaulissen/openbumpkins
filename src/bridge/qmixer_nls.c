/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* CRT NLS adapters in C. Imported platform APIs retain their existing native
 * or Windows implementations; no original NLS/allocator bodies run here. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned n) {
  return BB_U32(stack(input) + 4u * (n + 1u));
}
static uint32_t call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, stack(input) - 256u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax; input[6] = state.ecx;
  input[5] = state.edx; input[8] = state.flags;
  return input[7];
}
static uint32_t api(uint32_t *input, uint32_t slot, uint32_t args[16]) {
  return call(input, BB_U32(slot), args);
}
static uint32_t allocate(uint32_t *input, uint32_t bytes) {
  uint32_t args[16] = {bytes}; return call(input, 0x10011710u, args);
}
static void release(uint32_t *input, uint32_t p) {
  uint32_t args[16] = {p}; call(input, 0x100117c0u, args);
}
static uint32_t bounded_length(uint32_t p, uint32_t count) {
  uint32_t n = 0;
  while (n < count && BB_U8(p + n)) ++n;
  return n;
}
/* 0x10018420..0x1001844c: count bytes up to a terminator or the bound. */
void bb_qmix_nls_bounded_length_c(uint32_t *input) {
  input[7] = bounded_length(arg(input, 0), arg(input, 1));
}
/* 0x10019200..0x10019294: wrapping product, minimum/aligned allocation,
 * zeroed small blocks, zeroed system blocks and the allocation retry policy. */
void bb_qmix_nls_calloc_c(uint32_t *input) {
  uint32_t bytes = arg(input, 0) * arg(input, 1);
  if (bytes <= 0xffffffe0u) bytes = bytes ? (bytes + 15u) & ~15u : 16u;
  uint32_t heap_allocate = BB_U32(0x100212ecu);
  for (;;) {
    uint32_t p = 0;
    if (bytes <= 0xffffffe0u) {
      if (BB_U32(0x1001fcd4u) >= bytes) {
        uint32_t args[16] = {bytes >> 4}; p = call(input, 0x10012570u, args);
        if (p) memset((void *)(uintptr_t)p, 0, bytes);
      }
      if (!p) {
        uint32_t args[16] = {BB_U32(0x10020e6cu), 8, bytes};
        p = call(input, heap_allocate, args);
      }
    }
    if (p || !BB_U32(0x1001f4b0u)) { input[7] = p; return; }
    uint32_t args[16] = {bytes};
    if (!call(input, 0x10012160u, args)) { input[7] = 0; return; }
  }
}
/* 0x100180c0..0x100181e3: probe/cache ANSI or wide classification, with
 * default locale/code page and a zeroed conversion buffer for the wide path. */
void bb_qmix_nls_character_type_c(uint32_t *input) {
  uint32_t choice = BB_U32(0x10020708u), result = 0;
  if (!choice) {
    uint32_t args[16] = {0, 1, 0x1001ac88u, 1, stack(input) - 16u};
    if (api(input, 0x10021240u, args)) choice = 2;
    else {
      args[0] = 1; args[1] = 0x1001ac8cu; args[2] = 1;
      args[3] = stack(input) - 16u;
      if (api(input, 0x1002123cu, args)) choice = 1;
      else { input[7] = 0; return; }
    }
  }
  BB_U32(0x10020708u) = choice;
  uint32_t type = arg(input, 0), source = arg(input, 1), count = arg(input, 2);
  uint32_t out = arg(input, 3), page = arg(input, 4), locale = arg(input, 5);
  if (choice == 2) {
    uint32_t args[16] = {locale ? locale : BB_U32(0x100204a8u), type, source, count, out};
    input[7] = api(input, 0x10021240u, args); return;
  }
  if (choice != 1) { input[7] = choice; return; }
  if (!page) page = BB_U32(0x100204b8u);
  uint32_t args[16] = {page, 9, source, count, 0, 0};
  uint32_t length = api(input, 0x10021268u, args), wide = 0;
  if (length) {
    args[0] = 2; args[1] = length; wide = call(input, 0x10019200u, args);
    if (wide) {
      args[0] = page; args[1] = 1; args[2] = source; args[3] = count;
      args[4] = wide; args[5] = length;
      uint32_t converted = api(input, 0x10021268u, args);
      if (converted) {
        args[0] = type; args[1] = wide; args[2] = converted; args[3] = out;
        result = api(input, 0x1002123cu, args);
      }
    }
  }
  release(input, wide); input[7] = result;
}
/* 0x100181f0..0x1001841b: bounded ANSI mapping or wide conversion, including
 * sort-key byte output, capacity queries, conversion flags and all cleanup. */
void bb_qmix_nls_map_string_c(uint32_t *input) {
  uint32_t choice = BB_U32(0x10020710u), result = 0;
  if (!choice) {
    uint32_t args[16] = {0, 0x100, 0x1001ac88u, 1, 0, 0};
    if (api(input, 0x10021238u, args)) choice = 2;
    else {
      args[2] = 0x1001ac8cu;
      if (api(input, 0x10021234u, args)) choice = 1;
      else { input[7] = 0; return; }
    }
  }
  BB_U32(0x10020710u) = choice;
  uint32_t locale = arg(input, 0), flags = arg(input, 1), source = arg(input, 2);
  uint32_t count = arg(input, 3), out = arg(input, 4), capacity = arg(input, 5);
  if ((int32_t)count > 0) count = bounded_length(source, count);
  if (choice == 2) {
    uint32_t args[16] = {locale, flags, source, count, out, capacity};
    input[7] = api(input, 0x10021238u, args); return;
  }
  if (choice != 1) { input[7] = choice; return; }
  uint32_t page = arg(input, 6);
  if (!page) { page = BB_U32(0x100204b8u); BB_U32(stack(input) + 28u) = page; }
  uint32_t args[16] = {page, 9, source, count, 0, 0};
  uint32_t length = api(input, 0x10021268u, args);
  if (!length) { input[7] = 0; return; }
  uint32_t wide = allocate(input, length * 2u), mapped = 0;
  if (!wide) { input[7] = 0; return; }
  args[0] = page; args[1] = 1; args[2] = source; args[3] = count;
  args[4] = wide; args[5] = length;
  if (api(input, 0x10021268u, args)) {
    args[0] = locale; args[1] = flags; args[2] = wide; args[3] = length;
    args[4] = 0; args[5] = 0;
    uint32_t size = api(input, 0x10021234u, args);
    if (size && (flags & 0x400u)) {
      if (!capacity) result = size;
      else if ((int32_t)size <= (int32_t)capacity) {
        args[4] = out; args[5] = capacity;
        if (api(input, 0x10021234u, args)) result = size;
      }
    } else if (size && (mapped = allocate(input, size * 2u))) {
      args[0] = locale; args[1] = flags; args[2] = wide; args[3] = length;
      args[4] = mapped; args[5] = size;
      if (api(input, 0x10021234u, args)) {
        args[0] = page; args[1] = 0x220; args[2] = mapped; args[3] = size;
        args[4] = capacity ? out : 0; args[5] = capacity; args[6] = args[7] = 0;
        result = api(input, 0x10021258u, args);
      }
    }
  }
  release(input, wide); release(input, mapped); input[7] = result;
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_nls_bounded_length, (ARG(0), ARG(1)))
ENTRY(bb_qmix_nls_calloc, (ARG(0), ARG(1)))
ENTRY(bb_qmix_nls_character_type, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)))
ENTRY(bb_qmix_nls_map_string, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)))
#undef ENTRY
#undef ARG
