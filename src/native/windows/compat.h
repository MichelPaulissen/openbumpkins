/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_WINDOWS_COMPAT_H
#define BB_WINDOWS_COMPAT_H
/* Keep Windows SDK declarations in host.c: the engine implements its own
 * logical Win32 services with the same names and different pointer layouts. */
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
void bb_host_initialize(void);
void bb_host_debug_break(void);
char *bb_host_realpath(const char *, char *);
char *bb_host_strsep(char **, const char *);
int bb_host_setenv(const char *, const char *, int);
int bb_host_unsetenv(const char *);
time_t bb_host_timegm(struct tm *);
struct tm *bb_host_gmtime_r(const time_t *, struct tm *);
struct tm *bb_host_localtime_r(const time_t *, struct tm *);
long bb_host_pagesize(int);
int bb_host_file_times(int, uint64_t[3]);
int bb_host_disk_space(uint32_t *, uint32_t *, uint32_t *, uint32_t *);
uint32_t bb_host_timezone(void *);
#define realpath bb_host_realpath
#define strsep bb_host_strsep
#define setenv bb_host_setenv
#define unsetenv bb_host_unsetenv
#define timegm bb_host_timegm
#define gmtime_r bb_host_gmtime_r
#define localtime_r bb_host_localtime_r
#define sysconf bb_host_pagesize
#define _SC_PAGESIZE 1
#define lstat stat
#define fsync _commit
#ifndef S_ISSOCK
#define S_ISSOCK(mode) 0
#endif
#endif
