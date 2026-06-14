/* BigOS bounded time declarations.
 *
 * Only second-resolution wall-clock time from SYS_GET_TIME is exposed here.
 * No timezone, locale, clocks, timers, nanosleep, or complete POSIX time API. */
#ifndef _BIGOS_USER_TIME_H
#define _BIGOS_USER_TIME_H

typedef long time_t;

time_t time(time_t *out);

#endif /* _BIGOS_USER_TIME_H */
