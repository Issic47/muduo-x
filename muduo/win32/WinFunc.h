#ifndef MUDUO_WIN_FUNC_H
#define MUDUO_WIN_FUNC_H

#include <muduo/win32/WinTypes.h>
#include <vector>

/* process and thread management */
inline pid_t win_get_process_id() 
{
  return ::GetCurrentProcessId();
}

inline pid_t win_get_thread_id()
{
  return ::GetCurrentThreadId();
}

extern int win_get_thread_num();

extern std::vector<pid_t> win_get_threads();

/* user management */
extern int win_get_username(char *buf, size_t *size);

/* memory management */
inline int win_get_pagesize(void)
{
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  return system_info.dwPageSize;
}

inline int win_get_regionsize (void) {
  SYSTEM_INFO system_info;
  GetSystemInfo (&system_info);
  return system_info.dwAllocationGranularity;
}

/* debug management */
extern std::string win_stacktrace();

#endif // MUDUO_WIN_FUNC_H