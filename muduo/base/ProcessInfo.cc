// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen
//

#include <muduo/base/ProcessInfo.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/FileUtil.h>

#if defined(NATIVE_WIN32)
#include <muduo/win32/WinFunc.h>
#else
#include <dirent.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/times.h>
#endif

#include <algorithm>

#include <assert.h>
#include <stdio.h> // snprintf
#include <stdlib.h>

namespace muduo
{
namespace detail
{

Timestamp g_startTime = Timestamp::now();

#if defined(NATIVE_WIN32)
// TODO: 
int g_clockTicks = CLOCKS_PER_SEC;
int g_pageSize = win_get_pagesize();

#else

thread_local int t_numOpenedFiles = 0;
int fdDirFilter(const struct dirent* d)
{
  if (::isdigit(d->d_name[0]))
  {
    ++t_numOpenedFiles;
  }
  return 0;
}

thread_local std::vector<pid_t>* t_pids = NULL;
int taskDirFilter(const struct dirent* d)
{
  if (::isdigit(d->d_name[0]))
  {
    t_pids->push_back(atoi(d->d_name));
  }
  return 0;
}

int scanDir(const char *dirpath, int (*filter)(const struct dirent *))
{
  struct dirent** namelist = NULL;
  int result = ::scandir(dirpath, &namelist, filter, alphasort);
  assert(namelist == NULL);
  return result;
}
// assume those won't change during the life time of a process.
int g_clockTicks = static_cast<int>(::sysconf(_SC_CLK_TCK));
int g_pageSize = static_cast<int>(::sysconf(_SC_PAGE_SIZE));

#endif // ! NATIVE_WIN32

}
}

using namespace muduo;
using namespace muduo::detail;

pid_t ProcessInfo::pid()
{
  return ::getpid();
}

string ProcessInfo::pidString()
{
  char buf[32];
  snprintf(buf, sizeof buf, "%d", pid());
  return buf;
}

uid_t ProcessInfo::uid()
{
#if defined(NATIVE_WIN32)
  return -1;
#else
  return ::getuid();
#endif
}

string ProcessInfo::username()
{
  const char* name = "unknownuser";
#if defined(NATIVE_WIN32)
  char buf[256];
  size_t buf_size = sizeof buf;
  if (win_get_username(buf, &buf_size) == 0) {
    name = buf;
  }
#else
  struct passwd pwd;
  struct passwd* result = NULL;
  char buf[8192];
  getpwuid_r(uid(), &pwd, buf, sizeof buf, &result);
  if (result)
  {
    name = pwd.pw_name;
  }
#endif
  
  return name;
}

uid_t ProcessInfo::euid()
{
#if defined(NATIVE_WIN32)
  return -1;
#else
  return ::geteuid();
#endif
}

Timestamp ProcessInfo::startTime()
{
  return g_startTime;
}

int ProcessInfo::clockTicksPerSecond()
{
  return g_clockTicks;
}

int ProcessInfo::pageSize()
{
  return g_pageSize;
}

bool ProcessInfo::isDebugBuild()
{
#ifdef NDEBUG
  return false;
#else
  return true;
#endif
}

string ProcessInfo::hostname()
{
  // HOST_NAME_MAX 64
  // _POSIX_HOST_NAME_MAX 255
  char buf[256];
  if (::gethostname(buf, sizeof buf) == 0)
  {
    buf[sizeof(buf)-1] = '\0';
    return buf;
  }
  else
  {
    return "unknownhost";
  }
}

string ProcessInfo::procname()
{
#if defined(_WIN32)
  char buf[256];
  uv_get_process_title(buf, sizeof buf);
  buf[sizeof(buf)-1] = '\0';
  return buf;
#else
  return procname(procStat()).as_string();
#endif
}

StringPiece ProcessInfo::procname(const string& stat)
{
  StringPiece name;
  size_t lp = stat.find('(');
  size_t rp = stat.rfind(')');
  if (lp != string::npos && rp != string::npos && lp < rp)
  {
    name.set(stat.data()+lp+1, static_cast<int>(rp-lp-1));
  }
  return name;
}

string ProcessInfo::procStatus()
{
#if defined(NATIVE_WIN32)
  // TODO: use win api
  return "currently not support process status";
#else
  string result;
  FileUtil::readFile("/proc/self/status", 65536, &result);
  return result;
#endif
}

string ProcessInfo::procStat()
{
#if defined(_WIN32)
  // TODO: use uv_getrusage
  return "currently not support process stat\n";
#else
  string result;
  FileUtil::readFile("/proc/self/stat", 65536, &result);
  return result;
#endif
}

string ProcessInfo::threadStat()
{
#if defined(NATIVE_WIN32)
  // TODO: use win32
  return "currently not support thread stat\n";
#else
  char buf[64];
  snprintf(buf, sizeof buf, "/proc/self/task/%d/stat", CurrentThread::tid());
  string result;
  FileUtil::readFile(buf, 65536, &result);
  return result;
#endif
}

string ProcessInfo::exePath()
{
  string result;
  char buf[1024];
  size_t path_size = 0;
  if (uv_exepath(buf, &path_size) == 0 && path_size > 0) {
    result.assign(buf, path_size);
  }
  return result;
}

int ProcessInfo::openedFiles()
{
#if defined(NATIVE_WIN32)
  return -1;
#else
  t_numOpenedFiles = 0;
  scanDir("/proc/self/fd", fdDirFilter);
  return t_numOpenedFiles;
#endif
}

int ProcessInfo::maxOpenFiles()
{
#if defined(NATIVE_WIN32)
  return -1;
#else
  struct rlimit rl;
  if (::getrlimit(RLIMIT_NOFILE, &rl))
  {
    return openedFiles();
  }
  else
  {
    return static_cast<int>(rl.rlim_cur);
  }
#endif
}

ProcessInfo::CpuTime ProcessInfo::cpuTime()
{
  ProcessInfo::CpuTime t;
  uv_rusage_t resource_usage;
  if (uv_getrusage(&resource_usage) == 0) {
    t.userSeconds = resource_usage.ru_utime.tv_sec + 
      resource_usage.ru_utime.tv_usec / 1000000.0;
    t.systemSeconds = resource_usage.ru_stime.tv_sec +
      resource_usage.ru_stime.tv_usec / 1000000.0;
  }

  return t;
}

int ProcessInfo::numThreads()
{
  int result = 0;
#if defined(NATIVE_WIN32)
  result = win_get_thread_num();
#else
  string status = procStatus();
  size_t pos = status.find("Threads:");
  if (pos != string::npos)
  {
    result = ::atoi(status.c_str() + pos + 8);
  }
#endif
  return result;
}

std::vector<pid_t> ProcessInfo::threads()
{
#if defined(NATIVE_WIN32)
  return win_get_threads();
#else
  std::vector<pid_t> result;
  t_pids = &result;
  scanDir("/proc/self/task", taskDirFilter);
  t_pids = NULL;
  std::sort(result.begin(), result.end());
  return result;
#endif
}

