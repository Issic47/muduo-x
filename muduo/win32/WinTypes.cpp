#include "WinTypes.h"

#include <cstdint>

int gettimeofday(struct timeval *tv, void* tz) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  uint64_t value = ((uint64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  tv->tv_usec = (long) ((value / 10LL) % 1000000LL);
  tv->tv_sec = (long) ((value - 116444736000000000LL) / 10000000LL);
  return (0);
}

/*
int gettimeofday(struct timeval *tp, void *tzp)
{
  time_t clock;
  struct tm tm;
  SYSTEMTIME wtm;
  GetLocalTime(&wtm);
  tm.tm_year     = wtm.wYear - 1900;
  tm.tm_mon     = wtm.wMonth - 1;
  tm.tm_mday     = wtm.wDay;
  tm.tm_hour     = wtm.wHour;
  tm.tm_min     = wtm.wMinute;
  tm.tm_sec     = wtm.wSecond;
  tm. tm_isdst    = -1;
  clock = mktime(&tm);
  tp->tv_sec = static_cast<long>(clock);
  tp->tv_usec = wtm.wMilliseconds * 1000;
  return (0);
}
*/

static int is_leap(unsigned y) {
  y += 1900;
  return (y % 4) == 0 && ((y % 100) != 0 || (y % 400) == 0);
}

time_t timegm(struct tm *tm) {
  static const unsigned ndays[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
  };
  time_t res = 0;
  int i;
  for (i = 70; i < tm->tm_year; ++i)
    res += is_leap(i) ? 366 : 365;
  for (i = 0; i < tm->tm_mon; ++i)
    res += ndays[is_leap(tm->tm_year)][i];
  res += tm->tm_mday - 1;
  res *= 24;
  res += tm->tm_hour;
  res *= 60;
  res += tm->tm_min;
  res *= 60;
  res += tm->tm_sec;
  return res;
}
