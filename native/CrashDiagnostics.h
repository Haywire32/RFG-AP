#pragma once
#include <windows.h>
#include <cstdio>

namespace CrashDiagnostics {
inline HANDLE file=INVALID_HANDLE_VALUE;
inline PVOID registration=nullptr;
inline LONG records=0;
inline uintptr_t gameBase=0,modBase=0;
inline DWORD gameSize=0,modSize=0;
inline thread_local const char* stage="native game";
struct Stage {
    const char* previous;
    explicit Stage(const char* value):previous(stage){stage=value;}
    ~Stage(){stage=previous;}
};
inline bool FatalCode(DWORD code) {
    return code==EXCEPTION_ACCESS_VIOLATION || code==EXCEPTION_IN_PAGE_ERROR ||
        code==EXCEPTION_ILLEGAL_INSTRUCTION || code==EXCEPTION_INT_DIVIDE_BY_ZERO ||
        code==EXCEPTION_STACK_OVERFLOW || code==0xc0000374 || code==0xc0000409;
}
inline LONG CALLBACK Record(EXCEPTION_POINTERS* info) {
    if(file==INVALID_HANDLE_VALUE || !info || !info->ExceptionRecord || !info->ContextRecord ||
       !FatalCode(info->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
    if(InterlockedIncrement(&records)>8) return EXCEPTION_CONTINUE_SEARCH;
    const auto& e=*info->ExceptionRecord;
    const auto& c=*info->ContextRecord;
    char output[2048];
    int length=snprintf(output,sizeof(output),
        "Exception=%08lX Thread=%lu Stage=%s\r\nEIP=%08lX GameBase=%08lX ModBase=%08lX\r\n"
        "EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX ESP=%08lX EBP=%08lX\r\n",
        e.ExceptionCode,GetCurrentThreadId(),stage,c.Eip,static_cast<DWORD>(gameBase),static_cast<DWORD>(modBase),
        c.Eax,c.Ebx,c.Ecx,c.Edx,c.Esi,c.Edi,c.Esp,c.Ebp);
    if(e.NumberParameters>=2 && (e.ExceptionCode==EXCEPTION_ACCESS_VIOLATION || e.ExceptionCode==EXCEPTION_IN_PAGE_ERROR))
        length+=snprintf(output+length,sizeof(output)-length,"Access=%lu Address=%08lX\r\n",
            static_cast<DWORD>(e.ExceptionInformation[0]),static_cast<DWORD>(e.ExceptionInformation[1]));
    // Only addresses inside the game/mod images are retained, not stack data.
    DWORD stack[64]{};SIZE_T read=0;
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(c.Esp),stack,sizeof(stack),&read);
    for(size_t i=0;i<read/sizeof(DWORD) && length<1500;++i) {
        const uintptr_t address=stack[i];
        if(gameBase && address>=gameBase && address-gameBase<gameSize)
            length+=snprintf(output+length,sizeof(output)-length,"Stack+%02X game:%08lX\r\n",static_cast<unsigned>(i*4),static_cast<DWORD>(address-gameBase+0x400000));
        else if(modBase && address>=modBase && address-modBase<modSize)
            length+=snprintf(output+length,sizeof(output)-length,"Stack+%02X mod+%08lX\r\n",static_cast<unsigned>(i*4),static_cast<DWORD>(address-modBase));
    }
    DWORD written=0;
    WriteFile(file,output,static_cast<DWORD>(length),&written,nullptr);
    FlushFileBuffers(file);
    // This observer never suppresses a fault or replaces the game's reporter.
    return EXCEPTION_CONTINUE_SEARCH;
}
inline DWORD ImageSize(uintptr_t base) {
    if(!base) return 0;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    return reinterpret_cast<const IMAGE_NT_HEADERS*>(base+dos->e_lfanew)->OptionalHeader.SizeOfImage;
}
inline void Start(const wchar_t* path,HMODULE mod) {
    if(registration) return;
    gameBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));modBase=reinterpret_cast<uintptr_t>(mod);
    gameSize=ImageSize(gameBase);modSize=ImageSize(modBase);
    file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    const char header[]="RF:G AP 0.5.1. Local exception observations; handled faults may also appear.\r\n";
    DWORD written=0;WriteFile(file,header,sizeof(header)-1,&written,nullptr);
    registration=AddVectoredExceptionHandler(1,Record);
    if(!registration){CloseHandle(file);file=INVALID_HANDLE_VALUE;}
}
}
