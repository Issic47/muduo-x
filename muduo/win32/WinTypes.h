#ifndef MUDUO_WIN_TYPES_H
#define MUDUO_WIN_TYPES_H

#include <time.h>
#include <stdio.h>
#include <errno.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#ifndef __func__
#define __func__ __FUNCTION__
#endif

#ifndef pid_t
#define pid_t DWORD
#endif // !pid_t

#ifndef uid_t
#define uid_t int
#endif

/* Format for printing 64-bit signed decimal numbers */
#ifndef PRId64
#ifdef _MSC_EXTENSIONS
#define PRId64  "I64d"
#else /* _MSC_EXTENSIONS */
#define PRId64  "lld"
#endif /* _MSC_EXTENSIONS */
#endif /* PRId64 */

/* Format for printing 64-bit unsigned octal numbers */
#ifndef PRIo64
#ifdef _MSC_EXTENSIONS
#define PRIo64  "I64o"
#else /* _MSC_EXTENSIONS */
#define PRIo64  "llo"
#endif /* _MSC_EXTENSIONS */   
#endif /* PRIo64 */   

/* Format for printing 64-bit unsigned decimal numbers */
#ifndef PRIu64
#ifdef _MSC_EXTENSIONS
#define PRIu64  "I64u"
#else /* _MSC_EXTENSIONS */
#define PRIu64  "llu"  
#endif /* _MSC_EXTENSIONS */
#endif /* PRIu64 */

/* Formats for printing 64-bit unsigned hexadecimal numbers */
/* XXX - it seems that GLib has problems with the MSVC like I64x.
   As we use GLib's g_sprintf and alike, it should be safe to use
   llx everywhere now, making the macros pretty useless. For details see:
   http://bugs.wireshark.org/bugzilla/show_bug.cgi?id=1025 */
#ifndef PRIx64
#ifdef _MSC_EXTENSIONS
/*#define PRIx64  "I64x"*/
#define PRIx64  "llx"
#else /* _MSC_EXTENSIONS */
#define PRIx64  "llx"
#endif /* _MSC_EXTENSIONS */
#endif /* PRIx64 */

#ifndef PRIX64
#ifdef _MSC_EXTENSIONS
/*#define PRIX64  "I64X"*/
#define PRIX64  "llX"
#else /* _MSC_EXTENSIONS */
#define PRIX64  "llX"
#endif /* _MSC_EXTENSIONS */
#endif /* PRIX64 */

#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

// Use fread/fwrite/fflush on platforms without _unlocked variants
#define fread_unlocked fread
#define fwrite_unlocked fwrite
#define fflush_unlocked fflush

#ifndef strerror_r
#define strerror_r(errno,buf,len) strerror_s(buf,len,errno)
#endif

#ifndef snprintf
#define snprintf c99_snprintf
inline int c99_vsnprintf(char* str, size_t size, const char* format, va_list ap)
{
  int count = -1;

  if (size != 0)
    count = _vsnprintf_s(str, size, _TRUNCATE, format, ap);
  if (count == -1)
    count = _vscprintf(format, ap);

  return count;
}

inline int c99_snprintf(char* str, size_t size, const char* format, ...)
{
  int count;
  va_list ap;

  va_start(ap, format);
  count = c99_vsnprintf(str, size, format, ap);
  va_end(ap);

  return count;
}
#endif // !snprintf

struct timeval;
extern int gettimeofday(struct timeval *tv, void* tz);

inline struct tm* gmtime_r(const time_t *_Time, struct tm * _Tm) 
{
  errno_t err = gmtime_s(_Tm, _Time);
  return err == EINVAL ? NULL : _Tm;
}

extern time_t timegm(struct tm *tm);

inline void bzero(void *b, size_t len) 
{
  memset(b, 0, len);
}

#endif // !MUDUO_WIN_TYPES_H
