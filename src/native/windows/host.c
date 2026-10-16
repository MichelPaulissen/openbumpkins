/* SPDX-License-Identifier: GPL-3.0-only */
#include "compat.h"
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

struct Mapping {
  struct Mapping *next;
  void *address, *base;
  size_t size;
  int anonymous;
};
static struct Mapping *mappings;
static SRWLOCK mapping_lock = SRWLOCK_INIT;
static DWORD protection(int access) {
  if (access & PROT_EXEC)
    return access & PROT_WRITE ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
  if (access & PROT_WRITE)
    return PAGE_READWRITE;
  return access & PROT_READ ? PAGE_READONLY : PAGE_NOACCESS;
}
static int failed(DWORD error) {
  errno = error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY
              ? ENOMEM
          : error == ERROR_ACCESS_DENIED ? EACCES : EINVAL;
  return -1;
}
void *bb_host_mmap(void *address, size_t size, int access, int flags, int fd,
                   off_t offset) {
  if (!size || offset < 0 || !(flags & MAP_PRIVATE)) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  AcquireSRWLockExclusive(&mapping_lock);
  if (flags & MAP_FIXED) {
    /* Fixed replacements only discard pages of our own anonymous reservation.
     * Retain the reservation; the next access must see freshly zeroed bytes. */
    struct Mapping *m = mappings;
    uintptr_t p = (uintptr_t)address;
    while (m && !(m->anonymous && p >= (uintptr_t)m->address &&
                 p - (uintptr_t)m->address < m->size &&
                 size <= m->size - (p - (uintptr_t)m->address)))
      m = m->next;
    int okay = (flags & MAP_ANONYMOUS) && m &&
               VirtualFree(address, size, MEM_DECOMMIT) &&
               VirtualAlloc(address, size, MEM_COMMIT, protection(access));
    DWORD error = GetLastError();
    ReleaseSRWLockExclusive(&mapping_lock);
    if (!okay) {
      failed(error);
      return MAP_FAILED;
    }
    return address;
  }
  struct Mapping *m = calloc(1, sizeof(*m));
  if (!m) {
    ReleaseSRWLockExclusive(&mapping_lock);
    errno = ENOMEM;
    return MAP_FAILED;
  }
  m->anonymous = !!(flags & MAP_ANONYMOUS);
  m->size = size;
  if (m->anonymous) {
    m->base = VirtualAlloc(address, size, MEM_RESERVE | MEM_COMMIT,
                           protection(access));
    if (!m->base && address)
      m->base = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT,
                             protection(access));
    m->address = m->base;
  } else {
    HANDLE file = (HANDLE)_get_osfhandle(fd);
    HANDLE mapping = CreateFileMappingW(file, NULL,
        access & PROT_WRITE ? PAGE_WRITECOPY : PAGE_READONLY, 0, 0, NULL);
    if (mapping) {
      SYSTEM_INFO system;
      GetSystemInfo(&system);
      uint64_t aligned = (uint64_t)offset &
                         ~((uint64_t)system.dwAllocationGranularity - 1);
      size_t delta = (size_t)((uint64_t)offset - aligned);
      if (size <= SIZE_MAX - delta)
        m->base = MapViewOfFile(mapping,
            access & PROT_WRITE ? FILE_MAP_COPY : FILE_MAP_READ,
            (DWORD)(aligned >> 32), (DWORD)aligned, size + delta);
      if (m->base)
        m->address = (char *)m->base + delta;
      DWORD error = GetLastError();
      CloseHandle(mapping);
      SetLastError(error);
    }
  }
  if (!m->address) {
    DWORD error = GetLastError();
    free(m);
    ReleaseSRWLockExclusive(&mapping_lock);
    failed(error);
    return MAP_FAILED;
  }
  m->next = mappings;
  mappings = m;
  ReleaseSRWLockExclusive(&mapping_lock);
  return m->address;
}
int bb_host_munmap(void *address, size_t size) {
  AcquireSRWLockExclusive(&mapping_lock);
  struct Mapping **slot = &mappings;
  while (*slot && (*slot)->address != address)
    slot = &(*slot)->next;
  struct Mapping *m = *slot;
  if (!m || size != m->size) {
    ReleaseSRWLockExclusive(&mapping_lock);
    errno = EINVAL;
    return -1;
  }
  int okay = m->anonymous ? VirtualFree(m->base, 0, MEM_RELEASE)
                          : UnmapViewOfFile(m->base);
  DWORD error = GetLastError();
  if (okay) {
    *slot = m->next;
    free(m);
  }
  ReleaseSRWLockExclusive(&mapping_lock);
  return okay ? 0 : failed(error);
}
int bb_host_mprotect(void *address, size_t size, int access) {
  DWORD previous;
  return VirtualProtect(address, size, protection(access), &previous)
             ? 0 : failed(GetLastError());
}
long bb_host_pagesize(int name) {
  if (name != _SC_PAGESIZE) {
    errno = EINVAL;
    return -1;
  }
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return (long)info.dwPageSize;
}
void bb_host_initialize(void) { _set_fmode(_O_BINARY); }
char *bb_host_realpath(const char *path, char *output) {
  struct stat info;
  if (stat(path, &info))
    return NULL;
  return _fullpath(output, path, output ? _MAX_PATH : 0);
}
char *bb_host_strsep(char **remaining, const char *delimiters) {
  char *start = *remaining;
  if (!start)
    return NULL;
  char *end = start + strcspn(start, delimiters);
  *remaining = *end ? end + 1 : NULL;
  if (*end)
    *end = 0;
  return start;
}
int bb_host_setenv(const char *name, const char *value, int overwrite) {
  if (!name || !*name || strchr(name, '=') || !value) {
    errno = EINVAL;
    return -1;
  }
  return !overwrite && getenv(name) ? 0 : _putenv_s(name, value) ? -1 : 0;
}
int bb_host_unsetenv(const char *name) { return bb_host_setenv(name, "", 1); }
time_t bb_host_timegm(struct tm *t) {
  /* Signed Gregorian conversion also handles dates before the Windows CRT's
   * epoch. SYSTEMTIME permits day overflow (including non-leap February 29). */
  int64_t y = (int64_t)t->tm_year + 1900, month = t->tm_mon;
  int64_t carry = month / 12;
  if (month % 12 < 0)
    --carry;
  y += carry;
  unsigned m = (unsigned)(month - carry * 12) + 1;
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yo = (unsigned)(y - era * 400);
  unsigned day = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5;
  int64_t days = era * 146097 + yo * 365 + yo / 4 - yo / 100 + day - 719468;
  return (time_t)((days + t->tm_mday - 1) * 86400 +
                  (int64_t)t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
}
int bb_host_file_times(int fd, uint64_t times[3]) {
  FILETIME created, accessed, modified;
  if (!GetFileTime((HANDLE)_get_osfhandle(fd), &created, &accessed, &modified))
    return failed(GetLastError());
  times[0] = ((uint64_t)created.dwHighDateTime << 32) | created.dwLowDateTime;
  times[1] = ((uint64_t)accessed.dwHighDateTime << 32) | accessed.dwLowDateTime;
  times[2] = ((uint64_t)modified.dwHighDateTime << 32) | modified.dwLowDateTime;
  return 0;
}
int bb_host_disk_space(uint32_t *sectors, uint32_t *bytes, uint32_t *available,
                       uint32_t *total) {
  DWORD s, b, a, t;
  if (!GetDiskFreeSpaceW(NULL, &s, &b, &a, &t))
    return 0;
  *sectors = s;
  *bytes = b;
  *available = a;
  *total = t;
  return 1;
}
uint32_t bb_host_timezone(void *output) {
  _Static_assert(sizeof(TIME_ZONE_INFORMATION) == 172, "Windows time-zone layout");
  TIME_ZONE_INFORMATION zone;
  DWORD result = GetTimeZoneInformation(&zone);
  if (result != TIME_ZONE_ID_INVALID)
    memcpy(output, &zone, sizeof zone);
  return result;
}
