#include "os/os.h"
#include "os/os_windows.h"

extern std::string GetFormatTime();

#ifdef WIN32

#include <Windows.h>  
#include <DbgHelp.h> 

#pragma comment(lib,"DbgHelp.lib")  

// 创建Dump文件  
void CreateDumpFile(LPCSTR lpstrDumpFilePathName, EXCEPTION_POINTERS *pException)  
{  
    HANDLE hDumpFile = CreateFile(lpstrDumpFilePathName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);  

    // Dump信息  
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo;  
    dumpInfo.ExceptionPointers = pException;  
    dumpInfo.ThreadId = GetCurrentThreadId();  
    dumpInfo.ClientPointers = TRUE;  

    // 写入Dump文件内容  
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, NULL, NULL);  
    CloseHandle(hDumpFile);  
} 

// 处理Unhandled Exception的回调函数  
LONG ApplicationCrashHandler(EXCEPTION_POINTERS *pException)  
{     
    CreateDumpFile((GetFormatTime() + ".dmp").c_str(), pException); 
    return EXCEPTION_EXECUTE_HANDLER;  
}  

void os::register_exception_handler()
{
    //注册异常处理函数  
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);   
}

#endif
