// Workspace-only test host. Never part of the public mod archive.
#include "NativeRuntime.cpp"
#include <cstdio>
#include <cstdlib>
#include <dinput.h>

namespace {
int assertions=0;
void Check(bool ok,const char* description) {
    ++assertions;
    if(!ok) {fprintf(stderr,"FAILED: %s (Windows error %lu)\n",description,GetLastError());std::exit(1);}
}
std::atomic<int> acceptedSnapshots{0};
}
namespace ApShop {
bool Install(IHookManager&) {return false;}
void Frame(Player*) {}
bool Active() {return false;}
bool AcceptSnapshot(const std::string& payload,std::string& error) {
    if(payload!="{\"protocol\":3}") {error="test rejected malformed payload";return false;}
    ++acceptedSnapshots;return true;
}
}
int main(int argc,char** argv) {
    Check(argc==3,"arguments: standalone DLL and supported RF:G executable");
    Check(SupportedExecutable(fs::path(argv[2])),"supported executable SHA256 accepted");
    Check(!SupportedExecutable(fs::path(argv[0])),"foreign executable rejected");

    const auto dll=LoadLibraryA(argv[1]);
    Check(dll!=nullptr,"standalone DLL loads without RSL");
    const char* exports[]={"DirectInput8Create","DllCanUnloadNow","DllGetClassObject","DllRegisterServer","DllUnregisterServer","GetdfDIJoystick"};
    for(int i=0;i<6;++i) {
        const auto named=GetProcAddress(dll,exports[i]);
        Check(named!=nullptr,"required export exists");
        Check(named==GetProcAddress(dll,MAKEINTRESOURCEA(i+1)),"system-compatible export ordinal");
    }
    using InputFn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
    const auto create=reinterpret_cast<InputFn>(GetProcAddress(dll,"DirectInput8Create"));
    // SDK IID_IDirectInput8W, supplied locally so the test host has no DirectInput import.
    const GUID inputId={0xbf798031,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};
    IUnknown* input=nullptr;
    const HRESULT result=create(GetModuleHandleW(nullptr),0x0800,inputId,reinterpret_cast<void**>(&input),nullptr);
    Check(SUCCEEDED(result) && input,"DirectInput8Create forwards to the Windows DLL");
    input->Release();
    using FormatFn=const void*(WINAPI*)();
    Check(reinterpret_cast<FormatFn>(GetProcAddress(dll,"GetdfDIJoystick"))()!=nullptr,"joystick format forwards");
    std::ifstream hostLog(fs::path(argv[0]).parent_path()/"RFGArchipelago"/"Logs"/"General Log.log");
    const std::string logged((std::istreambuf_iterator<char>(hostLog)),{});
    Check(logged.find("Unsupported executable")!=std::string::npos,"foreign host disables AP hooks while DirectInput works");

    inv_item_info hammer{"sledgehammer"},charge{"charge_placer"},m16{"m16"},starting{"test_start"},script{"scripted_mission_gun"};
    int human=0,npc=0;
    LoadoutPolicy policy;
    bool suppress=false;
    Check(policy.Choose(&human,&hammer,3,0x00adafd8,false,nullptr,suppress)==&hammer,"intro hammer retained before AP connect");
    Check(policy.Choose(&human,&charge,1,0x00adafd8,true,&starting,suppress)==&starting,"selected starting weapon replaces default remote charges");
    Check(!suppress,"starting weapon add permitted");
    Check(policy.Choose(&human,&charge,1,0x00adafd8,true,&charge,suppress)==&charge,"remote charges supported as starting weapon");
    Check(policy.Choose(&human,&charge,1,0x00b21f05,true,&starting,suppress,10)==&charge && !suppress,"tutorial remote charge refill is a temporary mission grant");
    policy.Choose(&human,&charge,1,0x00b21f05,true,&starting,suppress);
    Check(suppress,"Parker vanilla remote charges blocked");
    policy.Choose(&human,&m16,1,0x00b21f05,true,&starting,suppress);
    Check(suppress,"Parker vanilla assault rifle blocked");
    Check(policy.Choose(&human,&charge,1,0x00b21f05,true,&starting,suppress)==&charge && !suppress,"later script remote charges pass through same dispatcher");
    Check(policy.Choose(&human,&m16,1,0x00b21f05,true,&starting,suppress)==&m16 && !suppress,"later script assault rifle passes through same dispatcher");
    policy.Choose(&human,&hammer,3,0x00adafd8,true,&starting,suppress);
    Check(policy.Choose(&human,&m16,1,0x00b21f05,true,&starting,suppress)==&m16 && !suppress,"later default-loadout rebuild does not reset opening grant mask");
    for(auto* person:{static_cast<void*>(&human),static_cast<void*>(&npc)}) {
        for(auto* item:{&charge,&m16,&script}) {
            Check(policy.Choose(person,item,1,0x00abcd00,true,&starting,suppress)==item && !suppress,"mission and activity scripted loadouts preserved");
        }
    }
    Check(policy.Choose(&npc,&charge,1,0x00adafd8,true,&starting,suppress)==&charge && !suppress,"NPC inventory unaffected");
    inv_item_info golden{"golden_hammer"};
    LoadoutPolicy goldenPolicy;
    Check(goldenPolicy.Choose(&human,&golden,3,0x00adafd8,true,&starting,suppress,-1,&hammer)==&hammer,"profile golden hammer replaced only at AP intro");
    Check(goldenPolicy.Choose(&human,&charge,1,0x00adafd8,true,&starting,suppress)==&starting,"golden hammer intro still receives AP starter");
    Check(goldenPolicy.Choose(&human,&golden,3,0x00ac4090,true,&starting,suppress,-1,&hammer)==&golden,"later golden hammer equip preserved");
    Check(goldenPolicy.Choose(&human,&golden,3,0x00adafd8,false,nullptr,suppress,-1,&hammer)==&golden,"unconfigured vanilla intro preserved");

    const auto pipeName=std::wstring(L"\\\\.\\pipe\\RFGArchipelago-Test-")+std::to_wstring(GetCurrentProcessId());
    std::thread([pipeName]{PipeServer(pipeName);}).detach();
    auto connect=[&]{
        HANDLE handle=INVALID_HANDLE_VALUE;
        for(int attempt=0;attempt<100 && handle==INVALID_HANDLE_VALUE;++attempt) {
            WaitNamedPipeW(pipeName.c_str(),100);
            handle=CreateFileW(pipeName.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
            if(handle==INVALID_HANDLE_VALUE) Sleep(10);
        }
        Check(handle!=INVALID_HANDLE_VALUE,"same-user pipe connects or reconnects");return handle;
    };
    const std::string payload="{\"protocol\":3}";
    for(int cycle=0;cycle<20;++cycle) {
        HANDLE pipe=connect();
        const int header[]={0x31475041,100,static_cast<int>(payload.size())};
        for(size_t byte=0;byte<sizeof(header);++byte)
            Check(WriteExactly(pipe,reinterpret_cast<const char*>(header)+byte,1),"fragmented header accepted");
        for(const char value:payload) Check(WriteExactly(pipe,&value,1),"fragmented payload accepted");
        int reply[3]{};Check(ReadExactly(pipe,reply,sizeof(reply)),"complete acknowledgement returned");
        Check(reply[0]==0x31415041 && reply[1]==100 && reply[2]==1,"positive protocol acknowledgement");
        CloseHandle(pipe);
    }
    Check(acceptedSnapshots==20,"client reconnect retains game-side protocol state");
    {
        HANDLE pipe=connect();const int partial[]={0x31475041,100,100};
        Check(WriteExactly(pipe,partial,sizeof(partial)),"partial packet header written");
        Check(WriteExactly(pipe,"{",1),"partial packet byte written");CloseHandle(pipe);
    }
    {
        HANDLE pipe=connect();const int bad[]={0x31475041,100,1048577};
        Check(WriteExactly(pipe,bad,sizeof(bad)),"oversized packet written");
        int reply[3];Check(!ReadExactly(pipe,reply,sizeof(reply)),"oversized packet disconnected");CloseHandle(pipe);
    }
    {
        HANDLE pipe=connect();const int header[]={0x31475041,100,2};
        Check(WriteExactly(pipe,header,sizeof(header)) && WriteExactly(pipe,"{}",2),"invalid snapshot sent");
        int reply[3];Check(ReadExactly(pipe,reply,sizeof(reply)) && reply[2]==-1,"invalid snapshot rejected without killing server");
        CloseHandle(pipe);
    }
    Check(acceptedSnapshots==20,"invalid and partial packets did not apply state");
    printf("Standalone native runtime: %d checks passed.\n",assertions);
    return 0;
}
