#include "NativeSupport.h"
#include "LoadoutPolicy.h"
#include "ApShop.h"
#include <bcrypt.h>
#include <sddl.h>
#include <fstream>
#include <intrin.h>
#include <thread>

namespace {
HMODULE selfModule=nullptr;
std::once_flag systemOnce,runtimeOnce;
HMODULE systemInput=nullptr;
std::string exeFolder;
std::mutex logMutex;
std::ofstream logFile;

HMODULE SystemInput() {
    std::call_once(systemOnce,[]{
        wchar_t directory[MAX_PATH];
        const UINT size=GetSystemDirectoryW(directory,MAX_PATH);
        if(size && size<MAX_PATH) {
            const auto path=fs::path(directory)/L"dinput8.dll";
            systemInput=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
    });
    return systemInput;
}
template<class F> F SystemFunction(const char* name) {
    const auto module=SystemInput();
    return module ? reinterpret_cast<F>(GetProcAddress(module,name)) : nullptr;
}
bool SupportedExecutable(const fs::path& path) {
    if(_wcsicmp(path.filename().c_str(),L"rfg.exe")!=0) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    std::ifstream input(path,std::ios::binary);
    ok=ok && input.is_open();
    std::array<unsigned char,65536> buffer{};
    while(ok && input) {
        input.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
        auto count=input.gcount();
        if(count>0) ok=BCryptHashData(hash,buffer.data(),static_cast<ULONG>(count),0)>=0;
    }
    ok=ok && input.eof();
    std::array<unsigned char,32> digest{};
    if(ok) ok=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    // Canonical text keeps the supported build auditable in the source.
    std::string actual;
    for(unsigned char value:digest) actual+=fmt::format("{:02X}",value);
    return ok && actual=="0D52039E7F2D3F25A4BE52A2ABA83919456FB3F00E52E75051726247471A2DF4";
}
using FrameFn=void(__fastcall*)(Player*);
using AddFn=void*(__cdecl*)(void*,inv_item_info*,int,int,int,char,char,char);
FrameFn originalFrame=nullptr;
AddFn originalAdd=nullptr;
LoadoutPolicy loadout;

bool WeaponTableReady() {
    return *reinterpret_cast<weapon_info**>(Globals::ModuleBase+0x3482c9c)!=nullptr
        && *reinterpret_cast<unsigned int*>(Globals::ModuleBase+0x3482c94)>=96;
}
void __fastcall Frame(Player* player) {
    if(player && WeaponTableReady()) ApShop::Frame(player);
    originalFrame(player);
}
void* __cdecl AddItem(void* human,inv_item_info* info,int count,int ammo,int slot,char a,char b,char c) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-Globals::ModuleBase+0x400000;
    inv_item_info* starting=nullptr;
    const bool configured=Globals::ApStartingWeaponConfigured.load();
    const auto definition=Globals::ApStartingWeaponDefinition;
    if(configured && definition>=0 && definition<96 && WeaponTableReady())
        starting=Globals::WeaponInfos[definition].weapon_inv_item_info;
    bool suppress=false;
    info=loadout.Choose(human,info,slot,caller,configured,starting,suppress,ammo);
    return suppress ? nullptr : originalAdd(human,info,count,ammo,slot,a,b,c);
}

bool ReadExactly(HANDLE pipe,void* destination,DWORD length) {
    DWORD total=0;
    while(total<length) {
        DWORD count=0;
        if(!ReadFile(pipe,static_cast<char*>(destination)+total,length-total,&count,nullptr) || !count) return false;
        total+=count;
    }
    return true;
}
bool WriteExactly(HANDLE pipe,const void* source,DWORD length) {
    DWORD total=0;
    while(total<length) {
        DWORD count=0;
        if(!WriteFile(pipe,static_cast<const char*>(source)+total,length-total,&count,nullptr) || !count) return false;
        total+=count;
    }
    return true;
}
void ClientSession(HANDLE pipe) {
    int header[3];
    while(ReadExactly(pipe,header,sizeof(header))) {
        if(header[0]!=0x31475041 || header[1]!=100 || header[2]<1 || header[2]>1048576) break;
        std::string payload(static_cast<size_t>(header[2]),'\0');
        if(!ReadExactly(pipe,payload.data(),header[2])) break;
        std::string error;
        const bool accepted=ApShop::AcceptSnapshot(payload,error);
        const int reply[]={0x31415041,100,accepted ? 1 : -1};
        if(!accepted) Logger::LogError("Snapshot rejected: {}\n",error);
        if(!WriteExactly(pipe,reply,sizeof(reply))) break;
    }
}
void PipeServer(const std::wstring& pipeName=L"\\\\.\\pipe\\RFGArchipelago") {
    // Only the account running RF:G may send mod commands. Remote pipes are
    // explicitly disabled. The allocation lives for the server thread lifetime.
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return;
    DWORD size=0;
    GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<unsigned char> tokenBuffer(size);
    const bool haveUser=GetTokenInformation(token,TokenUser,tokenBuffer.data(),size,&size)!=FALSE;
    CloseHandle(token);
    if(!haveUser) return;
    LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenBuffer.data())->User.Sid,&sid)) return;
    const auto sddl=std::wstring(L"D:P(A;;GA;;;")+sid+L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)) return;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),descriptor,FALSE};
    for(;;) {
        const HANDLE pipe=CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&security);
        if(pipe==INVALID_HANDLE_VALUE) {
            Logger::LogError("Cannot create game connection: Windows error {}.\n",GetLastError());
            break;
        }
        const BOOL connected=ConnectNamedPipe(pipe,nullptr) || GetLastError()==ERROR_PIPE_CONNECTED;
        if(connected) {
            Logger::Log("RF:G client connected.\n");
            try { ClientSession(pipe); }
            catch(const std::exception& error) { Logger::LogError("Connection error: {}\n",error.what()); }
            DisconnectNamedPipe(pipe);
            Logger::Log("RF:G client disconnected; game checks remain journaled. Waiting for reconnection.\n");
        }
        CloseHandle(pipe);
    }
    LocalFree(descriptor);
}
void InitializeRuntime() {
    wchar_t filename[32768];
    const auto length=GetModuleFileNameW(nullptr,filename,32768);
    if(!length || length>=32768) return;
    const fs::path executable(filename);
    exeFolder=executable.parent_path().string()+"/";
    fs::create_directories(executable.parent_path()/"RFGArchipelago"/"Logs");
    logFile.open(executable.parent_path()/"RFGArchipelago"/"Logs"/"General Log.log",std::ios::trunc);
    Logger::Log("RF:G Archipelago 0.5.0 standalone runtime.\n");
    if(!SupportedExecutable(executable)) {
        Logger::LogError("Unsupported executable. AP hooks are disabled; system DirectInput is unchanged.\n");
        return;
    }
    if(GetModuleHandleW(L"RSL.dll")) {
        Logger::LogError("RSL is already loaded. Remove the development mod loader before using this standalone release.\n");
        return;
    }
    Globals::ModuleBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Globals::RfgMaxCharges=reinterpret_cast<int*>(Globals::ModuleBase+0x1251568);
    if(MH_Initialize()!=MH_OK) {Logger::LogError("Cannot initialize native hooks.\n");return;}
    IHookManager hooks;
    bool installed=ApShop::Install(hooks)
        && hooks.CreateHook("APPlayerFrame",static_cast<DWORD>(Globals::ModuleBase+0x6d5a80),Frame,originalFrame)
        && hooks.CreateHook("APIntroLoadout",static_cast<DWORD>(Globals::ModuleBase+0x6b5210),AddItem,originalAdd);
    installed=installed && MH_EnableHook(MH_ALL_HOOKS)==MH_OK;
    if(!installed) {
        MH_DisableHook(MH_ALL_HOOKS);MH_RemoveHook(MH_ALL_HOOKS);MH_Uninitialize();
        Logger::LogError("AP hooks could not be installed. No game connection started.\n");
        return;
    }
    // The proxy remains resident until process exit; the detached server never
    // outlives its module. A client disconnect does not reset AP state or hooks.
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                      reinterpret_cast<LPCWSTR>(&InitializeRuntime),&pinned);
    Logger::Log("RF:G native hooks ready; waiting for the Archipelago client.\n");
    std::thread([]{PipeServer();}).detach();
}
void StartRuntime() {
    std::call_once(runtimeOnce,[]{
        try { InitializeRuntime(); }
        catch(const std::exception& error) { Logger::LogError("Initialization failed: {}\n",error.what()); }
    });
}
}

namespace Globals {
uintptr_t ModuleBase=0;
int* RfgMaxCharges=nullptr;
int ApStartingWeaponDefinition=-1,ApStartingWeaponUpgrade=-1;
std::atomic<bool> ApStartingWeaponConfigured{false};
std::atomic<bool> ApGrantedWeaponDefinitions[96]{};
std::atomic<bool> ApGrantedUpgradeRows[62]{};
std::atomic<int> ApGrantedUpgradeLevels[62]{};
WeaponTable WeaponInfos;
weapon_info& WeaponTable::operator[](size_t index) const {
    return (*reinterpret_cast<weapon_info**>(ModuleBase+0x3482c9c))[index];
}
std::string GetEXEPath(bool) {return exeFolder;}
}
namespace Logger {
void Write(const std::string& text) {
    std::lock_guard<std::mutex> lock(logMutex);
    if(logFile.is_open()) {logFile<<text;logFile.flush();}
}
}
namespace rfg {
void* upgrade_info_get(unsigned int row) {
    using Function=void*(__cdecl*)(unsigned int);
    return reinterpret_cast<Function>(Globals::ModuleBase+0x35f600)(row);
}
}

extern "C" HRESULT WINAPI APDirectInput8Create(HINSTANCE instance,DWORD version,REFIID id,LPVOID* output,LPUNKNOWN outer) {
    using Function=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
    auto function=SystemFunction<Function>("DirectInput8Create");
    StartRuntime();
    return function ? function(instance,version,id,output,outer) : E_FAIL;
}
extern "C" HRESULT WINAPI APDllCanUnloadNow() {
    // AP keeps worker threads and native callbacks alive for the process lifetime.
    return S_FALSE;
}
extern "C" HRESULT WINAPI APDllGetClassObject(REFCLSID clsid,REFIID id,void** output) {
    using Function=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
    auto function=SystemFunction<Function>("DllGetClassObject");
    return function ? function(clsid,id,output) : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI APDllRegisterServer() {
    using Function=HRESULT(WINAPI*)();auto function=SystemFunction<Function>("DllRegisterServer");
    return function ? function() : E_FAIL;
}
extern "C" HRESULT WINAPI APDllUnregisterServer() {
    using Function=HRESULT(WINAPI*)();auto function=SystemFunction<Function>("DllUnregisterServer");
    return function ? function() : E_FAIL;
}
extern "C" const void* WINAPI APGetdfDIJoystick() {
    using Function=const void*(WINAPI*)();auto function=SystemFunction<Function>("GetdfDIJoystick");
    return function ? function() : nullptr;
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {selfModule=module;DisableThreadLibraryCalls(module);}
    return TRUE;
}
