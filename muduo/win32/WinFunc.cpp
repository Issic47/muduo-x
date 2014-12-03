#include "WinFunc.h"

#include <muduo/base/Singleton.h>
#include <muduo/base/Mutex.h>

#include <tlhelp32.h>
#include <DbgHelp.h>

namespace win 
{

class SymManager
{
public:
  SymManager()
  {
    process_ = GetCurrentProcess();
    SymInitialize(process_, NULL, TRUE);
  }

  ~SymManager()
  {
    SymCleanup(process_);
  }

  muduo::MutexLock& getMutex()
  {
    return mutex_;
  }

private:
  HANDLE process_;
  // NOTE: all dbghelper functions are single threaded.
  muduo::MutexLock mutex_;
};

} // !namespace win

static int ListProcessThreads( DWORD dwOwnerPID, std::vector<pid_t> *t_pids ) 
{ 
  HANDLE hThreadSnap = INVALID_HANDLE_VALUE; 
  THREADENTRY32 te32; 

  // Take a snapshot of all running threads  
  hThreadSnap = ::CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 ); 
  if( hThreadSnap == INVALID_HANDLE_VALUE ) 
    return -1; 

  // Fill in the size of the structure before using it. 
  te32.dwSize = sizeof(THREADENTRY32); 

  // Retrieve information about the first thread,
  // and exit if unsuccessful
  if( !::Thread32First( hThreadSnap, &te32 ) ) 
  {
    ::CloseHandle( hThreadSnap );          // clean the snapshot object
    return -1;
  }

  // Now walk the thread list of the system,
  // and display information about each thread
  // associated with the specified process
  int thread_num = 0;
  do 
  { 
    if( te32.th32OwnerProcessID == dwOwnerPID )
    {
      ++thread_num;
      if (t_pids) {
        t_pids->push_back(te32.th32ThreadID);
      }
    }
  } while( ::Thread32Next(hThreadSnap, &te32 ) ); 

  ::CloseHandle( hThreadSnap );
  return 0;
}

int win_get_thread_num()
{
  return ListProcessThreads(::GetCurrentProcessId(), NULL);
}

extern std::vector<pid_t> win_get_threads()
{
  std::vector<pid_t> t_pids;
  ListProcessThreads(GetCurrentProcessId(), &t_pids);
  return t_pids;
}

extern int win_get_username( char *buf, size_t *size )
{
  DWORD username_len = *size;
  if (::GetUserNameA(buf, &username_len)) {
    *size = username_len;
    return 0;
  }
  return -1;
}

std::string win_stacktrace()
{
  void *stack[100];
  unsigned short frames = CaptureStackBackTrace(0, 100, stack, NULL);
  SYMBOL_INFO *symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
  symbol->MaxNameLen = 255;
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

  HANDLE process = GetCurrentProcess();

  std::string result;
  result.reserve(128);
  
  {
    muduo::MutexLockGuard guard(
      muduo::Singleton<win::SymManager>::instance().getMutex());

    SymRefreshModuleList(process);

    char buf[512];
    int count = 0;
    for(unsigned int i = 0; i < frames; i++)
    {
      SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
      count = snprintf(buf, sizeof(buf)-1,
        "%i: %s - 0x%0X\n", frames-i-1, symbol->Name, symbol->Address);
      if (count != -1) result.append(buf);
    }
  }

  free(symbol);

  return result;
}