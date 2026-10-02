#include "NativeSupport.h"
#include "Gunship.h"
#include "GunshipPolicy.h"
#include "DistanceFog.h"
#include "CrashDiagnostics.h"
#include "EquipmentTuning.h"
#include <map>
#include <cwctype>
#include <limits>

namespace Gunship {
namespace {
using namespace GunshipPolicy;
template<class T> T At(uintptr_t va) {
#ifdef GUNSHIP_TEST
    return reinterpret_cast<T>(GunshipTestAddress(va));
#else
    return reinterpret_cast<T>(Globals::ModuleBase+va-0x400000);
#endif
}
template<class T> T& Field(void* p,size_t off) {return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+off);}
struct Craft {void* object=nullptr;uint32_t handle=None;Vec spawn{};bool parked=false;};
struct Command {void* object=nullptr;uint32_t handle=None;Motion motion{};bool enabled=false;};
std::mutex stateMutex;
std::vector<Craft> crafts;
Command command;
struct Bailout {void* player=nullptr;uint32_t handle=None,craft=None;bool alreadyProtected=false;float elapsed=0,grounded=0;};
Bailout bailout;
constexpr uint32_t NoRagdoll=0x20000000u; // human+0xc0; native 00a2ea40 / RSL DisallowFlinchesAndRagdolls.
uint32_t pilot=None;
Motion motion;
bool fire=false,precision=false,landRequested=false;
ExitInput exitInput;
AimInput aimInput;
AimZoom aimZoom;
float unzoomedFov=0,lastZoomedFov=0;
bool zoomApplied=false;
uint64_t nextDiagnostic=0;
uint64_t nextEntryDiagnostic=0;
std::atomic<uint64_t> lastHudDraw{0},lastPilotShot{0};
enum class SightState {NotPiloting,ControlsBlocked,NoRenderer,NoSprite,NoTexture,InvalidAtlas,Drawn};
std::atomic<SightState> sightState{SightState::NotPiloting};
thread_local bool dispatching=false;
thread_local bool entering=false;
thread_local void* rayIgnoredCraft=nullptr;
struct EnterData {int seat=0,method=0,previous=-1;uint8_t flags=1,padding[3]{};};
struct Useable {uint32_t handle;int type,icon;float dot,distance;int(__cdecl* use)(void*,char);};
static_assert(sizeof(EnterData)==16 && sizeof(Useable)==24);
using InputFn=void(__cdecl*)(); InputFn originalInput=nullptr;
using HeldFn=char(__cdecl*)(int,int); HeldFn originalHeld=nullptr;
using InteractionsFn=void(__cdecl*)(void*); InteractionsFn originalInteractions=nullptr;
using UseFn=int(__cdecl*)(void*,char); UseFn originalUse=nullptr;
using FlightFn=void(__thiscall*)(void*,float); FlightFn originalFlight=nullptr;
using CameraFn=void(__cdecl*)(float,char); CameraFn originalCamera=nullptr;
using LookFn=void(__cdecl*)(float,int); LookFn originalYaw=nullptr,originalPitch=nullptr;
using AimFn=void(__thiscall*)(void*); AimFn originalAim=nullptr;
using MuzzleFn=void(__thiscall*)(void*,Vec*,Vec*,Vec*); MuzzleFn originalMuzzle=nullptr;
using TurretInputFn=void(__thiscall*)(void*,float,float,int,int); TurretInputFn originalTurretInput=nullptr;
using ShotEffectsFn=void(__thiscall*)(void*); ShotEffectsFn originalShotEffects=nullptr;
using RayFilterFn=int(__cdecl*)(void*); RayFilterFn originalRayFilter=nullptr;
int __cdecl RayFilter(void* object) {
    return object && object==rayIgnoredCraft?0:originalRayFilter(object);
}
bool Trace(void* car,const Vec& start,const Vec& end,Vec& hit) {
    auto* saved=rayIgnoredCraft;rayIgnoredCraft=car;
    const bool found=At<char(__cdecl*)(const Vec*,const Vec*,Vec*,float,uint32_t)>(0x006d6ce0)
        (&start,&end,&hit,0,*At<uint32_t*>(0x0165fe88))!=0;
    rayIgnoredCraft=saved;return found;
}

void* Object(uint32_t handle) {
    return handle==None?nullptr:At<void*(__thiscall*)(void*,uint32_t)>(0x0093c050)(At<void*>(0x02f98490),handle);
}
void* LocalPlayer() {return *At<void**>(0x03023874);}
bool Campaign() {return !*At<uint8_t*>(0x02feb588) && !At<char(__cdecl*)()>(0x00760440)() && At<int(__cdecl*)()>(0x007bfcf0)()==1;}
bool Registered(void* object) {
    if(!object) return false;
    std::lock_guard<std::mutex> lock(stateMutex);
    return std::any_of(crafts.begin(),crafts.end(),[=](const Craft& c){return c.object==object && c.handle==Field<uint32_t>(object,0x6c);});
}
bool Ready(void* object) {
    return object && Field<uint8_t>(object,0x7e)==3 && Field<uint8_t>(object,0x7f)==5 &&
        !(Field<uint8_t>(object,0x54)&0x10) && Field<uint8_t>(object,0x350c)==1 &&
        Field<uint8_t>(object,0x40b9) && Field<void*>(object,0x40bc) &&
        Field<uint32_t>(object,0x4c)!=None && Field<float>(object,0x3f58)<1.0f &&
        !(Field<uint32_t>(object,0x3f24)&(1u<<10));
}
void* Occupied(void* player) {
    if(!player || player!=LocalPlayer() || Field<int>(player,0xacc)!=0) return nullptr;
    auto* car=Object(Field<uint32_t>(player,0xac4));
    // Exit must survive destroyed wings, a stopped physics controller and a
    // flipped hull. Require a live registered object and actual seat ownership.
    return Registered(car) && Field<uint8_t>(car,0x7e)==3 && Field<uint8_t>(car,0x7f)==5 &&
        !(Field<uint8_t>(car,0x54)&0x10) && Field<uint32_t>(car,0x3d14)==Field<uint32_t>(player,0x6c)?car:nullptr;
}
void* Driven(void* player) {
    auto* car=Occupied(player);return Ready(car)?car:nullptr;
}
bool Controls(void* player) {
    return player && Campaign() && !(Field<uint32_t>(player,0xdcc)&0x88) &&
        !(Field<uint32_t>(player,0xbc)&0x80000000u) && !*At<uint8_t*>(0x01e2a9b9) &&
        !At<char(__cdecl*)()>(0x007c1690)() && !At<char(__cdecl*)()>(0x00705ad0)();
}
void EndBailout() {
    if(bailout.player && Object(bailout.handle)==bailout.player && !bailout.alreadyProtected)
        Field<uint32_t>(bailout.player,0xc0)&=~NoRagdoll;
    bailout={};
}
void BeginBailout(void* player,uint32_t craft) {
    EndBailout();
    bailout={player,Field<uint32_t>(player,0x6c),craft,(Field<uint32_t>(player,0xc0)&NoRagdoll)!=0,0,0};
    Field<uint32_t>(player,0xc0)|=NoRagdoll;
}
void UpdateBailout(float dt) {
    auto* player=bailout.player;
    if(!player) return;
    if(player!=LocalPlayer() || Object(bailout.handle)!=player || !Campaign() ||
        (Field<uint32_t>(player,0xbc)&0x80000000u) || (Field<uint32_t>(player,0xdcc)&0x88) ||
        *At<uint8_t*>(0x01e2a9b9) || Field<uint32_t>(player,0xac4)!=None) {EndBailout();return;}
    // Keep the native no-ragdoll flag for the entire descent, including contact
    // with the gunship. Do not overwrite velocity, jetpack state or damage flags.
    Field<uint32_t>(player,0xc0)|=NoRagdoll;
    if(!std::isfinite(dt)||dt<=0) return;
    dt=std::min(dt,.1f);bailout.elapsed+=dt;
    bool onGround=Field<int>(player,0x2f4)==0 && Field<int>(player,0x304)==1 &&
        Field<uint32_t>(player,0x2b4)!=bailout.craft;
    if(onGround) {
        const Vec pos=Field<Vec>(player,4);Vec hit{};
        onGround=Finite(pos) && Trace(Object(bailout.craft),pos+Vec{0,.4f,0},pos-Vec{0,.6f,0},hit) &&
            Finite(hit) && hit.y<=pos.y+.15f && hit.y>=pos.y-.5f;
    }
    bailout.grounded=onGround?bailout.grounded+dt:0;
    if(bailout.elapsed>.6f && bailout.grounded>.25f) EndBailout();
}
bool Focused() {
#ifdef GUNSHIP_TEST
    return GunshipTestFocused();
#else
    DWORD process=0;const auto window=GetForegroundWindow();
    if(window) GetWindowThreadProcessId(window,&process);
    return process==GetCurrentProcessId();
#endif
}
bool Key(int code) {
#ifdef GUNSHIP_TEST
    return GunshipTestKey(code);
#else
    return (GetAsyncKeyState(code)&0x8000)!=0;
#endif
}
void PollDistanceFog(void* player) {
    // Independent of the vehicle/pilot state. The choice lasts for this launch;
    // vanilla fog remains untouched until F8 is deliberately pressed.
    static DistanceFog::Toggle toggle;
    const bool active=Controls(player)&&Focused();
    const bool modified=Key(VK_CONTROL)||Key(VK_SHIFT)||Key(VK_MENU)||Key(VK_LWIN)||Key(VK_RWIN);
    const bool changed=toggle.Poll(Key(VK_F8),active,modified);
    if(changed || (active && !toggle.visible && *At<uint8_t*>(0x0165cbea)))
        At<void(__cdecl*)(bool)>(0x007c2c70)(toggle.visible); // RSL GameRenderSetFogEnabled
    if(changed) Logger::Log("Distance fog: {} (F8).\n",toggle.visible?"on":"off");
}
float Heading(void* car) {const auto f=Field<Vec>(car,0x28);return std::atan2(f.x,f.z);}
Vec Velocity(void* car) {
    Vec value{};At<void(__cdecl*)(uint32_t,Vec*)>(0x008237a0)(Field<uint32_t>(car,0x4c),&value);return value;
}
float Clearance(void* car) {
    const auto pos=Field<Vec>(car,4);auto* info=Field<void*>(car,0x3514);
    if(!info || !Finite(pos)) return std::numeric_limits<float>::infinity();
    // Query down from this hull, not from above the entire mountain/garage.
    // Exclude this hull explicitly, including after a mission ejects its pilot.
    const float bottom=pos.y+Field<float>(info,0x18);
    const Vec start{pos.x,pos.y+.5f,pos.z},end{pos.x,bottom-40.0f,pos.z};Vec hit{};
    const bool found=Trace(car,start,end,hit);
    float floor=found&&Finite(hit)?hit.y:-std::numeric_limits<float>::infinity();
    // A recorded safehouse pad may sit above the terrain height query.
    // Only use its known parking plane in the immediate vicinity of that pad.
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        for(const auto& c:crafts) if(c.object==car && c.handle==Field<uint32_t>(car,0x6c)) {
            const Vec d=pos-c.spawn;
            if(d.x*d.x+d.z*d.z<16.0f) floor=std::max(floor,c.spawn.y+Field<float>(info,0x18));
        }
    }
    return pos.y+Field<float>(info,0x18)-floor;
}
bool CanPark(void* car) {return SafeParking(Clearance(car),Length(Velocity(car)),Field<float>(car,0x20));}
const char* EntryBlock(void* player,void* car) {
    if(player!=LocalPlayer() || !Controls(player)) return "player controls blocked";
    if(!Registered(car) || !Ready(car)) return "vehicle not active";
    if(Field<uint32_t>(player,0xac4)!=None) return "player already in vehicle";
    if(Field<uint32_t>(car,0x3d14)!=None) return "driver seat occupied";
    if(!(Field<uint32_t>(player,0x24b0)&(1u<<7))) return "native vehicle interactions disabled";
    if(!At<char(__cdecl*)(void*)>(0x00b689a0)(player)) return "native player entry state blocked";
    if(At<void*(__cdecl*)(void*)>(0x00a556f0)(player)) return "player operating turret";
    if(*At<int*>(0x01de4b50)==1 || Field<int>(player,0xd58)==3 ||
        Field<int>(player,0x97c)!=0 || (Field<uint32_t>(player,0xb8)&0x8000)) return "native interaction state blocked";
    auto* action=Field<void*>(player,0xc90);
    if(action) {
        auto* state=Field<void*>(action,8);
        auto* info=state?Field<void*>(state,0x1c):nullptr;
        if(info && (Field<uint8_t>(info,0xd)&1)) return "script action blocks interaction";
    }
    if(!EntryRange(Field<Vec>(player,4),Field<Vec>(car,4),&Field<float>(car,0x10))) return "outside boarding range";
    const float speed=Length(Velocity(car)),up=Field<float>(car,0x20);
    if(!std::isfinite(speed)||speed>=2.5f) return "vehicle moving";
    if(!std::isfinite(up)||up<=.65f) return "vehicle overturned";
    return nullptr;
}
bool CanEnter(void* player,void* car) {return EntryBlock(player,car)==nullptr;}
void StopEngine(void* car) {
    // An AI engine-off suspension must not overrule an explicit parking/landing request.
    Field<int>(car,0x4190)=-1;
    At<void(__thiscall*)(void*,bool,bool)>(0x00b7e4f0)(car,true,false);
    At<void(__thiscall*)(void*,bool)>(0x00b673f0)(car,false);
    Field<uint8_t>(car,0x41e4)=static_cast<uint8_t>((Field<uint8_t>(car,0x41e4)&~4u)|32u);
    Field<uint8_t>(car,0x40ec)=0;Field<uint8_t>(car,0x40ed)=0;
    Field<uint8_t>(car,0x40ee)=0;Field<uint8_t>(car,0x40f0)=0;
}
void ClearZoom() {
    // Only undo our last output, never a value installed by a cutscene or a new
    // camera owner. The normal camera produces a fresh base FOV every update.
    if(zoomApplied && *At<float*>(0x01de4c10)==lastZoomedFov)
        *At<float*>(0x01de4c10)=unzoomedFov;
    zoomApplied=false;aimZoom.Reset();
}
void ClearPilot() {
    ClearZoom();aimInput.Reset();
    pilot=None;fire=false;precision=false;landRequested=false;exitInput.Reset();
    std::lock_guard<std::mutex> lock(stateMutex);command={};
}
void ReleasePilot() {
    Command c;{std::lock_guard<std::mutex> lock(stateMutex);c=command;}
    if(c.object && Object(c.handle)==c.object && Registered(c.object) && Ready(c.object)) {
        if(CanPark(c.object)) StopEngine(c.object);
        else {
            // A mission/death can remove the driver without invoking the E action.
            // Cancel the last cruise command instead of leaving an unmanned craft accelerating.
            const Vec com=Field<Vec>(c.object,0x4104);
            if(Finite(com)) At<void(__thiscall*)(void*,const Vec*,bool)>(0x00b7e380)(c.object,&com,true);
        }
    }
    ClearPilot();
}
void FinishAirborneExit(void* player) {
    if(!Controls(player) || Field<uint32_t>(player,0xac4)!=None ||
        Field<int>(player,0x2f4)!=0 || Field<int>(player,0x304)!=1) return;
    // Match the normal airborne vehicle exit (00bbebba), after the immediate
    // exit has detached the seat/turret/camera. Teleport exit alone ends crouched
    // in direct movement, without the falling transition used by a normal jump.
    At<void(__cdecl*)(void*,int)>(0x00a07910)(player,0); // HST_STAND
    At<void(__cdecl*)(void*,int,int,float)>(0x00a078b0)(player,2,2,-1.0f); // HS_FALL, HMS_RUN
    At<void(__cdecl*)(void*,int,int)>(0x00a07930)(player,5,4); // HMM_FALL, HMSM_FALL_START
    At<void(__cdecl*)(void*,char)>(0x00a53d90)(player,1);
    auto* animation=Field<void*>(player,0x32c);
    animation=animation?Field<void*>(animation,4):nullptr;
    if(animation) {
        At<void(__cdecl*)(void*,int,int)>(0x007ffc10)(animation,0x400,0);
        At<void(__cdecl*)(void*,int,int)>(0x007f7620)(animation,0x400,10);
        const Vec up{0,1,0};
        At<void(__cdecl*)(void*,void*,const Vec*,float)>(0x007f9a40)(animation,player,&up,.15f);
    }
    Logger::Log("Gunship: airborne exit state {}, movement {}/{}, stance {}.\n",
        Field<int>(player,0x2f4),Field<int>(player,0x304),Field<int>(player,0x308),Field<int>(player,0x2fc));
}
int __cdecl Use(void* player,char activate) {
    if(auto* car=Occupied(player)) {
        if(!Controls(player)) return 0;
        if(activate && !exitInput.pressed) return 0;
        if(activate) {
            exitInput.Reset();
            CrashDiagnostics::Stage stage("Gunship native exit");
            const auto handle=Field<uint32_t>(car,0x6c);
            const bool airborne=Clearance(car)>2.0f;
            if(airborne) BeginBailout(player,handle);
            uint8_t flags=1; // Native immediate exit: clears seat, turret and camera ownership.
            At<void(__cdecl*)(void*,uint8_t*)>(0x00bb6060)(player,&flags);
            const bool exited=Field<uint32_t>(player,0xac4)!=handle;
            if(exited) {
                if(airborne) FinishAirborneExit(player);
                if(Object(handle)==car && Registered(car) && Ready(car)) StopEngine(car);
                ClearPilot();
            } else if(airborne) EndBailout();
            Logger::Log("Gunship: exit {:08x}, completed {}.\n",handle,exited);
        }
        return 1;
    }
    auto* car=player?Object(Field<uint32_t>(player,0xce4)):nullptr;
    if(Registered(car)) {
        if(!CanEnter(player,car)) return 0;
        if(!activate) return 1;
        CrashDiagnostics::Stage stage("Gunship native entry");
        EnterData entry;
        exitInput.Reset();
        entering=true;
        const int entered=At<char(__cdecl*)(void*,void*,EnterData*)>(0x00bb81f0)(player,car,&entry);
        entering=false;
        Logger::Log("Gunship: native entry {:08x} returned {}, seat {}, player vehicle {:08x}.\n",
            Field<uint32_t>(car,0x6c),entered,Field<int>(player,0xacc),Field<uint32_t>(player,0xac4));
        return entered;
    }
    return originalUse(player,activate);
}
void __cdecl Interactions(void* player) {
    originalInteractions(player);
    if(auto* car=Occupied(player)) {
        if(Field<Useable>(player,0xce4).handle==Field<uint32_t>(car,0x6c))
            Field<Useable>(player,0xce4)={None,-1,-1,-FLT_MAX,FLT_MAX,nullptr};
        return;
    }
    if(!player || player!=LocalPlayer() || Field<uint32_t>(player,0xac4)!=None) return;
    // The native resolver conditionally skips its car collector in campaign.
    // Add our candidate at the resolver boundary and use its normal mask/ranking
    // validators. Retain the engine callback address for native UI/input identity.
    auto& result=Field<Useable>(player,0xce4);
    std::vector<Craft> copy;{std::lock_guard<std::mutex> lock(stateMutex);copy=crafts;}
    for(const auto& c:copy) {
        auto* car=Object(c.handle);if(car!=c.object) continue;
        const auto* blocked=EntryBlock(player,car);
        bool selected=false;
        if(!blocked) {
            Useable candidate{c.handle,7,4,*At<float*>(0x012bc488),
                EntryDistance(Field<Vec>(player,4),Field<Vec>(car,4),&Field<float>(car,0x10)),At<UseFn>(0x00a9e160)};
            if(At<char(__cdecl*)(void*,Useable*)>(0x00a103a0)(player,&candidate) &&
                At<char(__cdecl*)(void*,Useable*)>(0x00a103d0)(player,&candidate)) {result=candidate;selected=true;}
        }
        const auto now=GetTickCount64();
        if(now>=nextEntryDiagnostic && Length(Field<Vec>(player,4)-Field<Vec>(car,4))<30.0f) {
            nextEntryDiagnostic=now+5000;
            Logger::Log("Gunship: boarding {:08x}: {}; prompt {:08x}, controls {:08x}, mask {:08x}, speed {:.2f}, clearance {:.2f}, up {:.2f}.\n",
                c.handle,blocked?blocked:(selected?"entry prompt selected":"another interaction has priority"),result.handle,
                Field<uint32_t>(player,0xdcc),Field<uint32_t>(player,0x24b0),Length(Velocity(car)),
                Ready(car)?Clearance(car):-999.0f,Field<float>(car,0x20));
        }
    }
}
void Publish(void* car,bool enabled) {
    std::lock_guard<std::mutex> lock(stateMutex);
    command={car,Field<uint32_t>(car,0x6c),motion,enabled};
}
bool ReadAim(bool enabled) {
    // 0x1a is on-foot fine aim. Native lookup follows the player's saved
    // keyboard/mouse mapping; 006fddc0 supplies its hold/toggle preference.
    if(!enabled) return aimInput.Sample(false,false,false);
    return aimInput.Sample(originalHeld(0x1a,*At<int*>(0x01e2a9cc))!=0,true,
        At<char(__cdecl*)()>(0x006fddc0)()!=0);
}
void __cdecl Input() {
    auto* player=LocalPlayer();
    PollDistanceFog(player);
    auto* car=Occupied(player);
    fire=false;precision=false;
    if(car) {
        const auto handle=Field<uint32_t>(car,0x6c);
        if(pilot!=handle) {
            pilot=handle;motion.Reset(Field<Vec>(car,4),Heading(car));
            exitInput.Reset();
            aimInput.Reset();ClearZoom();
            // Clear only transient orbit input; never change the saved control settings.
            for(const auto va:{0x01de4d3cu,0x01de4d40u,0x01de4d44u,0x01de4d48u,0x01de4d4cu,0x01de4d50u,0x01de4d54u,0x01de4cb0u,0x01de4cc0u}) *At<float*>(va)=0;
            Logger::Log("Gunship: pilot acquired {:08x}, native flyer control {:08x}, turret {:08x}.\n",
                handle,reinterpret_cast<uintptr_t>(Field<void*>(car,0x40bc)),Field<uint32_t>(player,0xa08));
        }
        GunshipPolicy::Input input;input.enabled=Controls(player)&&Focused();
        // Check before flight readiness: even a wreck must keep its exit key.
        if(exitInput.Sample(Key('E'),input.enabled)) {
            Use(player,1);return; // Do not reuse this same E edge for boarding again.
        }
        if(!Ready(car)) {
            ReadAim(false);ClearZoom();landRequested=false;
            {std::lock_guard<std::mutex> lock(stateMutex);command={};}
            originalInput();return;
        }
        precision=ReadAim(input.enabled);
        if(input.enabled) {
            input.forward=float(Key('W'))-float(Key('S'));input.side=float(Key('D'))-float(Key('A'));
            input.vertical=float(Key(VK_SHIFT))-float(Key(VK_CONTROL));
            int x=0,y=0,wheel=0;At<void(__cdecl*)(int*,int*,int*)>(0x005690d0)(&x,&y,&wheel);
            const int context=*At<int*>(0x01e2a9cc);
            if(At<char(__cdecl*)(int,int,char)>(0x006f95e0)(0,context,1)) x=-x;
            if(At<char(__cdecl*)(int,int,char)>(0x006f95e0)(1,context,1)) y=-y;
            input.mouseX=float(x);input.mouseY=float(y);
            input.sensitivity=*At<float*>(0x01518a30)*static_cast<float>(*At<double*>(0x012d0298));
            fire=Key(VK_LBUTTON);
            if(precision) input.sensitivity*=.4f;
        }
        landRequested=input.enabled&&input.vertical<0;
        if(landRequested) input.vertical*=DescentScale(Clearance(car));
        if(input.vertical>0 && !(Field<uint32_t>(car,0x3f24)&(1u<<13)))
            At<void(__thiscall*)(void*,bool)>(0x00b7e4b0)(car,true);
        if(input.vertical>0) Field<uint8_t>(car,0x41e4)&=static_cast<uint8_t>(~32u);
        motion.Step(input,Field<Vec>(car,4),Heading(car),*At<float*>(0x01519560));
        Publish(car,input.enabled);
    } else if(pilot!=None) ReleasePilot();
    dispatching=car!=nullptr;
    originalInput();
    dispatching=false;
}
char __cdecl Held(int action,int context) {
    if(dispatching) {
        if(action==0x21 || action==0x29) return fire?1:0;
        if(action==0x2a || action==0x2c || action==0x30) return 0;
    }
    return originalHeld(action,context);
}
void __fastcall Flight(void* car,void*,float dt) {
    Command c;{std::lock_guard<std::mutex> lock(stateMutex);c=command;}
    void* body=nullptr;Vec angular{};
    if(c.object==car && c.handle==Field<uint32_t>(car,0x6c) && Ready(car)) {
        const Vec com=Field<Vec>(car,0x4104);
        if(Finite(com)) {
            const float speed=Length(c.motion.velocity);
            Field<float>(car,0x40c0)=.45f;
            Field<float>(car,0x40c4)=c.motion.holding?2.0f:speed;
            Field<Vec>(car,0x4164)={}; // AI's random hover displacement fights precise pilot input.
            Field<Vec>(car,0x40c8)=c.motion.anchor+(com-Field<Vec>(car,4));
            Field<Vec>(car,0x40d4)=Unit(c.motion.velocity);
            Field<Vec>(car,0x40e0)=Forward(c.motion.yaw);
            Field<uint8_t>(car,0x40ec)=c.motion.holding?1:0;
            Field<uint8_t>(car,0x40ed)=c.motion.holding?0:1;
            Field<uint8_t>(car,0x40ee)=1;
            // Native velocity requests project a point 100m ahead. Stopping at
            // that moving point caps speed by braking distance, not req_max_vel.
            Field<uint8_t>(car,0x40ef)=c.motion.holding?1:0;
            Field<uint8_t>(car,0x40f0)=0;
            if(Driven(LocalPlayer())==car && Controls(LocalPlayer()) &&
                (Field<uint32_t>(car,0x3f24)&(1u<<13)) && Field<float>(car,0x20)>.35f) {
                body=At<void*(__cdecl*)(uint32_t)>(0x0080e870)(Field<uint32_t>(car,0x4c));
                if(body) At<void(__cdecl*)(void*,Vec*)>(0x008174e0)(body,&angular);
            }
        }
    }
    originalFlight(car,dt);
    if(body) {
        // Retain native linear thrust, turbine effects and collision response;
        // replace only the AI rotational command for the occupied pilot craft.
        const float step=dt<0?*At<float*>(0x01519560):dt;
        const Vec controlled=PilotAngularVelocity(Field<Vec>(car,0x10),Field<Vec>(car,0x1c),
            Field<Vec>(car,0x28),angular,c.motion.yaw,c.motion.velocity,step);
        if(Finite(controlled)) At<void(__cdecl*)(void*,const Vec*)>(0x00817430)(body,&controlled);
    }
}
bool PilotView(Command& c) {
    {std::lock_guard<std::mutex> lock(stateMutex);c=command;}
    return c.object && c.handle!=None && *At<int*>(0x01de4b50)==0 &&
        Driven(LocalPlayer())==c.object;
}
bool OwnCamera(Command& c) {
    return PilotView(c) && *At<uint32_t*>(0x01de4c18)==c.handle;
}
void __cdecl Camera(float dt,char unused) {
    Command c;
    if(!OwnCamera(c)) {ClearZoom();originalCamera(dt,unused);return;}
    auto* profile=*At<void**>(0x01de4c8c);
    if(!profile) {ClearZoom();originalCamera(dt,unused);return;}
    const int savedMode=*At<int*>(0x01de48b0);
    const float savedPitch=Field<float>(profile,0x64);
    const float savedHeight=Field<float>(profile,0xc),savedDistance=Field<float>(profile,0x68);
    *At<int*>(0x01de48b0)=0;
    // Set the profile consumed by this chase update, not only the setup routine
    // that can run before pilot acquisition. Keep a modest lift, not a top-down view.
    Field<float>(profile,0xc)=savedHeight+1.0f;
    Field<float>(profile,0x68)=23.0f;
    Field<float>(profile,0x64)=-c.motion.pitch;
    *At<float*>(0x01de4cc4)=-c.motion.pitch;*At<float*>(0x01de4ccc)=-c.motion.pitch;
    Vec look=Forward(c.motion.yaw);
    At<void(__cdecl*)(Vec*,float)>(0x006c4810)(&look,0.001f);
    originalCamera(dt,unused); // Native chase placement, collision and smoothing.
    // Chase distance can be replaced internally from the vehicle camera table.
    // Zoom the freshly calculated ideal FOV consumed by the native camera
    // update instead; keep the approved viewpoint and centre-of-screen aim ray.
    auto& fov=*At<float*>(0x01de4c10);
    const float scale=aimZoom.Step(precision&&c.enabled&&Controls(LocalPlayer()),dt);
    zoomApplied=false;
    if(std::isfinite(fov)&&fov>0&&scale<1) {
        unzoomedFov=fov;fov*=scale;lastZoomedFov=fov;zoomApplied=true;
    }
    Field<float>(profile,0x64)=savedPitch;*At<int*>(0x01de48b0)=savedMode;
    Field<float>(profile,0xc)=savedHeight;Field<float>(profile,0x68)=savedDistance;
}
void __cdecl Yaw(float value,int axis) {Command c;if(!OwnCamera(c)) originalYaw(value,axis);}
void __cdecl Pitch(float value,int axis) {Command c;if(!OwnCamera(c)) originalPitch(value,axis);}
bool OwnTurret(void* turret,Command& c) {
    {std::lock_guard<std::mutex> lock(stateMutex);c=command;}
    auto* player=LocalPlayer();
    return turret && c.object && player && Field<uint32_t>(turret,0x70)==c.handle &&
        Field<uint32_t>(turret,0xa0)==Field<uint32_t>(player,0x6c) && Driven(player)==c.object;
}
void __fastcall ShotEffects(void* weapon,void*) {
    auto* info=Field<void*>(weapon,0x148);Command c;
    const char* name=info?Field<const char*>(info,0):nullptr;
    if(name && !std::strcmp(name,"turret_edfrocket_flyer") &&
       OwnTurret(Object(Field<uint32_t>(weapon,0x14c)),c)) {
        auto* table=*At<unsigned char**>(0x03882c9c);
        const auto count=*At<unsigned*>(0x03882c94);
        if(table && count<=1024) for(unsigned n=0;n<count;++n) {
            auto* donor=table+n*sizeof(weapon_info);const char* donorName=Field<const char*>(donor,0);
            if(!donorName || std::strcmp(donorName,"turret_rocket_vehicle") || Field<int>(donor,0x84)<0) continue;
            // The flyer has no player firing cue. Reuse the native rocket-turret
            // cue for this synchronous sound/effect call only. Projectile and
            // global weapon definitions retain their original identity.
            std::array<unsigned char,sizeof(weapon_info)> soundInfo;
            std::memcpy(soundInfo.data(),info,soundInfo.size());
            Field<int>(soundInfo.data(),0x84)=Field<int>(donor,0x84);
            std::memcpy(soundInfo.data()+0xa4,donor+0xa4,0x44);
            Field<void*>(weapon,0x148)=soundInfo.data();
            originalShotEffects(weapon);
            Field<void*>(weapon,0x148)=info;
            static uint32_t reported=None;
            if(reported!=Field<uint32_t>(weapon,0x6c)) {
                reported=Field<uint32_t>(weapon,0x6c);
                Logger::Log("Gunship: rocket weapon {:08x} uses native rocket-turret firing cue {}.\n",reported,Field<int>(donor,0x84));
            }
            return;
        }
    }
    originalShotEffects(weapon);
}
void __fastcall TurretInput(void* turret,void*,float x,float y,int axisX,int axisY) {
    Command c;if(!OwnTurret(turret,c)) originalTurretInput(turret,x,y,axisX,axisY);
}
Vec AimTarget(const Command& c) {
    Vec target=Field<Vec>(c.object,4)+Forward(c.motion.yaw,c.motion.pitch)*1000.0f;
    Command camera;
    if(PilotView(camera)) {
        const auto origin=*At<Vec*>(0x01de4b7c),forward=*At<Vec*>(0x01de4bb8);
        if(Finite(origin) && Finite(forward) && Length(forward)>.9f) {
            // Native weapon aiming (00beaaf0) uses camera +Z as forward.
            // 006c4810 negates BOTH headings for its angle conversion, not the sight ray.
            target=origin+Unit(forward)*1000.0f;
            Vec hit{};
            if(Trace(c.object,origin,target,hit) && Finite(hit)) target=hit;
        }
    }
    return target;
}
void __fastcall Muzzle(void* weapon,void*,Vec* origin,Vec* direction,Vec* muzzle) {
    originalMuzzle(weapon,origin,direction,muzzle);
    Command c;
    if(!OwnTurret(Object(Field<uint32_t>(weapon,0x14c)),c) || !Controls(LocalPlayer())) return;
    lastPilotShot=GetTickCount64();
    // Keep the native barrel origin, shot timing, spread, projectile and effects.
    // The flyer firing path otherwise uses barrel orientation, independently of
    // the turret target. Converge each actual muzzle on the camera's sight point.
    const Vec target=AimTarget(c),aim=target-*origin;
    if(Finite(*origin) && Finite(aim) && Length(aim)>1.0f) {
        *direction=Unit(aim);
        Field<Vec>(Object(Field<uint32_t>(weapon,0x14c)),0x10c)=target;
        static uint64_t nextAimDiagnostic=0;
        const auto now=GetTickCount64();
        if(now>=nextAimDiagnostic) {
            nextAimDiagnostic=now+5000;
            Logger::Log("Gunship: sight target {:.2f}/{:.2f}/{:.2f}, muzzle {:.2f}/{:.2f}/{:.2f}, shot direction {:.3f}/{:.3f}/{:.3f}.\n",
                target.x,target.y,target.z,origin->x,origin->y,origin->z,direction->x,direction->y,direction->z);
        }
    }
}
void __fastcall Aim(void* turret,void*) {
    Command c;
    if(OwnTurret(turret,c)) {
        // Use the real mounted turret's articulation, limits and weapon muzzle.
        Field<Vec>(turret,0x10c)=AimTarget(c);
        Field<int>(turret,0x150)=-1; // Cancel manual-input idle timer; follow the requested point.
    }
    originalAim(turret);
}
}
void RenderHud() {
    Command c;
    // The sight belongs to the pilot, not the chase camera's current target.
    // Use the actual camera ray even if native camera ownership is refreshed.
    sightState=SightState::NotPiloting;
    if(!PilotView(c)) return;
    sightState=SightState::ControlsBlocked;
    if(!Controls(LocalPlayer())) return;
    // Called only from the minimap render branch (map +0x604 == 0).
    // Fullscreen-map resource flags survive closing that screen: they are not
    // a visibility test and must not permanently suppress the flight sight.
    const auto* screen=At<int*(__cdecl*)()>(0x0089bdb0)();
    auto* context=*At<void**>(0x01cd26f8);
    sightState=SightState::NoRenderer;
    if(!screen || screen[0]<=0 || screen[1]<=0 || !context) return;
    // Campaign reticles are regions of a bitmap sheet, not standalone bitmaps.
    // Resolve the existing VINT table so remastered atlas scaling is respected.
    struct Sprite {const char* name;uint32_t sheet;int x,y,width,height;};
    static_assert(sizeof(Sprite)==24);
    const auto count=*At<unsigned*>(0x01a48620);
    sightState=SightState::NoSprite;
    if(count>1536) return;
    const auto* sprites=At<Sprite*>(0x01a3e798);
    const Sprite* sprite=nullptr;unsigned index=0;
    for(;index<count;++index) if(sprites[index].name && !std::strcmp(sprites[index].name,"ui_hud_reti_assault")) {
        sprite=&sprites[index];break;
    }
    if(!sprite || sprite->width<=0 || sprite->height<=0) return;
    const int texture=At<int(__cdecl*)(unsigned)>(0x0045ba00)(index);
    sightState=SightState::NoTexture;
    if(texture<0) return;
    unsigned textureWidth=0,textureHeight=0;float scaleX=0,scaleY=0;
    At<void(__cdecl*)(int,unsigned*,unsigned*,float*,float*)>(0x00597590)
        (texture,&textureWidth,&textureHeight,&scaleX,&scaleY);
    sightState=SightState::InvalidAtlas;
    if(!textureWidth || !textureHeight || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX<=0 || scaleY<=0) return;
    const int sx=int(std::lround(sprite->x/scaleX)),sy=int(std::lround(sprite->y/scaleY));
    const int sw=int(std::lround(sprite->width/scaleX)),sh=int(std::lround(sprite->height/scaleY));
    if(sx<0 || sy<0 || sw<=0 || sh<=0 || sx+sw>int(textureWidth) || sy+sh>int(textureHeight)) return;
    static int reportedTexture=-1;
    if(reportedTexture!=texture) {
        reportedTexture=texture;
        Logger::Log("Gunship: campaign reticle {} on atlas {}, source {}/{}/{}x{}, atlas {}x{}.\n",
            sprite->name,texture,sx,sy,sw,sh,textureWidth,textureHeight);
    }
    const int height=int(std::clamp(screen[1]*.030f,18.0f,64.0f));
    const int width=int(std::lround(float(height)*sprite->width/sprite->height));
    const int x=(screen[0]-width)/2,y=(screen[1]-height)/2;
    std::array<unsigned char,0x24> saved;
    std::memcpy(saved.data(),static_cast<unsigned char*>(context)+0x14,saved.size());
    const auto flags=Field<uint32_t>(context,0x4c);
    using Draw=void(__cdecl*)(int,int,int,int,int,int,int,int,int,int,int,void*);
    auto color=At<void(__cdecl*)(int,int,int,int)>(0x00509170);auto draw=At<Draw>(0x005551b0);
    color(0,0,0,190);draw(texture,x+1,y+1,width,height,sx,sy,sw,sh,0,0,At<void*>(0x0150a648));
    color(255,160,10,255);draw(texture,x,y,width,height,sx,sy,sw,sh,0,0,At<void*>(0x0150a648));
    std::memcpy(static_cast<unsigned char*>(context)+0x14,saved.data(),saved.size());
    Field<uint32_t>(context,0x4c)=flags;Field<uint32_t>(context,0x10)|=0x40;
    lastHudDraw=GetTickCount64();sightState=SightState::Drawn;
}
const wchar_t* ControlHint(const wchar_t* text) {
    if(!text || (!entering && !Driven(LocalPlayer()))) return text;
    std::wstring lower=text;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
    if(lower.find(L"auto")==std::wstring::npos || lower.find(L"aim")==std::wstring::npos) return text;
    return L"Left mouse: fire. Aim: F by default. Shift/Ctrl: ascend/descend. E: exit or bail out.";
}
void* TuneExplosion(void* info,void* owner,void* weapon) {
    if(!info || !weapon || !owner || !Campaign()) return info;
    const auto* name=Field<const char*>(weapon,0);
    if(!name || std::strcmp(name,"turret_edfrocket_flyer") ||
       (owner!=LocalPlayer() && owner!=Driven(LocalPlayer()))) return info;
    static std::map<void*,EquipmentTuning::ExplosionInfo> definitions;
    auto found=definitions.find(info);
    if(found!=definitions.end()) return found->second.data();
    EquipmentTuning::ExplosionInfo base,tuned;
    std::memcpy(base.data(),info,base.size());tuned=base;
    EquipmentTuning::Scale(tuned,base,0x30,1.3f);
    const int damage=EquipmentTuning::Read<int>(base,0x60);
    EquipmentTuning::Write(tuned,0x60,damage>0&&damage<1000000?damage*2:damage);
    Logger::Log("Gunship: local rocket explosion tuned: structural damage {} -> {}, crumble radius {:.2f} -> {:.2f}.\n",
        damage,EquipmentTuning::Read<int>(tuned,0x60),EquipmentTuning::Read<float>(base,0x30),EquipmentTuning::Read<float>(tuned,0x30));
    return definitions.emplace(info,tuned).first->second.data();
}
void Track(void* car) {
    if(!car || Field<uint8_t>(car,0x7e)!=3 || Field<uint8_t>(car,0x7f)!=5) return;
    auto* info=Field<void*>(car,0x3514);
    if(!info || !Supported(Field<const char*>(info,0))) return;
    const auto handle=Field<uint32_t>(car,0x6c);
    {std::lock_guard<std::mutex> lock(stateMutex);
        for(const auto& c:crafts) if(c.object==car && c.handle==handle) return;
        crafts.push_back({car,handle,Field<Vec>(car,4),false});
    }
    Logger::Log("Gunship: registered {} {:08x} at {:.2f}, {:.2f}, {:.2f}.\n",Field<const char*>(info,0),handle,
        Field<float>(car,4),Field<float>(car,8),Field<float>(car,12));
}
void Forget(void* car,uint32_t handle) {
    std::lock_guard<std::mutex> lock(stateMutex);
    crafts.erase(std::remove_if(crafts.begin(),crafts.end(),[=](const Craft& c){return c.object==car && c.handle==handle;}),crafts.end());
    if(command.object==car && command.handle==handle) command={};
}
void Reset() {
    EndBailout();
    ClearPilot();nextEntryDiagnostic=0;lastHudDraw=0;lastPilotShot=0;sightState=SightState::NotPiloting;
    std::lock_guard<std::mutex> lock(stateMutex);crafts.clear();
}
void Frame(Player* player) {
    UpdateBailout(*At<float*>(0x01519560));
    if(!player || !Campaign()) return;
    std::vector<Craft> copy;{std::lock_guard<std::mutex> lock(stateMutex);copy=crafts;}
    for(const auto& c:copy) {
        auto* car=Object(c.handle);
        if(car!=c.object) {Forget(c.object,c.handle);continue;}
        if(!Ready(car)) continue;
        if(!c.parked && Field<uint32_t>(car,0x3d14)==None) {
            StopEngine(car);
            {std::lock_guard<std::mutex> lock(stateMutex);for(auto& entry:crafts) if(entry.object==car&&entry.handle==c.handle) entry.parked=true;}
            Logger::Log("Gunship: {:08x} parking initialized; boarding eligibility is checked by the interaction resolver.\n",c.handle);
        }
    }
    if(auto* car=Driven(player)) {
        if(landRequested && Clearance(car)<0.3f && CanPark(car)) {
            StopEngine(car);motion.Reset(Field<Vec>(car,4),Heading(car));Publish(car,false);landRequested=false;
        }
        const auto now=GetTickCount64();
        if(now>=nextDiagnostic) {
            nextDiagnostic=now+5000;
            Logger::Log("Gunship: flight {:08x}: speed {:.2f}, clearance {:.2f}, engine {:.2f}, input {:.2f}/{:.2f}/{:.2f}, turret {:08x}, camera {}.\n",
                Field<uint32_t>(car,0x6c),Length(Velocity(car)),Clearance(car),Field<float>(car,0x41d4),
                motion.velocity.x,motion.velocity.y,motion.velocity.z,Field<uint32_t>(player,0xa08),*At<int*>(0x01de4b50));
            const auto hudTime=lastHudDraw.load(),shotTime=lastPilotShot.load();
            Command owned;
            auto* turret=Object(Field<uint32_t>(player,0xa08));
            const bool turretOwned=OwnTurret(turret,owned);
            auto* weapon=turretOwned?Object(Field<uint32_t>(turret,0xe8)):nullptr;
            if(weapon && (Field<uint8_t>(weapon,0x7e)!=1 || Field<uint8_t>(weapon,0x7f)!=7)) weapon=nullptr;
            Logger::Log("Gunship: sight state {}, last draw {} ms, camera target {:08x}, map {:08x}; controls {}, fire {}, aim {}, permissions {:08x}, turret owned {}, ammo {}/{}, heat {:.3f}, overheated {}, last shot {} ms.\n",
                int(sightState.load()),hudTime?now-hudTime:0,*At<uint32_t*>(0x01de4c18),*At<unsigned*>(0x02bd9328),
                Controls(player),fire,precision,Field<uint32_t>(player,0x24ac),turretOwned,
                weapon?Field<int>(weapon,0x20c):-1,weapon?Field<int>(weapon,0x210):-1,
                weapon?Field<float>(weapon,0x220):-1,weapon?int(Field<uint8_t>(weapon,0x224)):-1,shotTime?now-shotTime:0);
        }
    } else if(pilot!=None && !Occupied(player)) ReleasePilot();
}
bool Install(IHookManager& hooks) {
    const auto address=[](uintptr_t va){return static_cast<DWORD>(Globals::ModuleBase+va-0x400000);};
    return hooks.CreateHook("GunshipInput",address(0x00700c50),Input,originalInput)
        && hooks.CreateHook("GunshipWeaponInput",address(0x006fe970),Held,originalHeld)
        && hooks.CreateHook("GunshipEntryPrompt",address(0x00ade470),Interactions,originalInteractions)
        && hooks.CreateHook("GunshipEntryExit",address(0x00a9e160),Use,originalUse)
        && hooks.CreateHook("GunshipFlight",address(0x00b7e800),Flight,originalFlight)
        && hooks.CreateHook("GunshipCamera",address(0x006d9780),Camera,originalCamera)
        && hooks.CreateHook("GunshipCollisionFilter",address(0x006d0bd0),RayFilter,originalRayFilter)
        && hooks.CreateHook("GunshipCameraYaw",address(0x006cafb0),Yaw,originalYaw)
        && hooks.CreateHook("GunshipCameraPitch",address(0x006cb1a0),Pitch,originalPitch)
        && hooks.CreateHook("GunshipTurretAim",address(0x00a42040),Aim,originalAim)
        && hooks.CreateHook("GunshipMuzzleAim",address(0x00beaaf0),Muzzle,originalMuzzle)
        && hooks.CreateHook("GunshipTurretMouse",address(0x00a43150),TurretInput,originalTurretInput)
        && hooks.CreateHook("GunshipRocketSound",address(0x00be65a0),ShotEffects,originalShotEffects);
}
}
