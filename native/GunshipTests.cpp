#include <cstdint>
void* GunshipTestAddress(uintptr_t);
bool GunshipTestFocused();
bool GunshipTestKey(int);
#define GUNSHIP_TEST
#include "Gunship.cpp"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <fstream>
namespace Globals {uintptr_t ModuleBase=0x400000;}
namespace Logger {void Write(const std::string&) {}}
namespace {
using namespace GunshipPolicy;
using Gunship::Field;
int assertions=0,entries=0,exits=0,stops=0,gears=0,flights=0,views=0,aims=0,fallbacks=0;
int holds=0;
void Check(bool ok,const char* name) {++assertions;if(!ok) {std::fprintf(stderr,"FAILED: %s\n",name);std::exit(1);}}
bool Near(float a,float b,float tolerance=.0001f) {return std::abs(a-b)<tolerance;}
unsigned char human[0x4000]{},car[0x4200]{},ordinary[0x4200]{},info[0x4bc]{},turret[0x180]{},profile[0x80]{};
void* player=human;void* cameraProfile=profile;
std::unordered_map<uint32_t,void*> objects;
uint8_t no=0;float dot=0.01f;int cameraMode=0,orbitMode=1;uint32_t cameraTarget=10;
float cameraFloats[2]{},floorY=0;Vec speed{},lastLook{};
Vec cameraPosition{},cameraForward{};
uint32_t rayMask=1234;
float cameraDistance=0,cameraHeight=0;
float idealFov=1.1f;
int inputContext=0,aimAction=-1;bool aimDown=false,holdAim=true;
bool focused=true,keys[256]{};
bool retireOnExit=false;
bool refuseExit=false;
bool nativeExitPrompt=false;
unsigned char animationOwner[8]{},animationData[0x200]{};
int fallTransitions=0,animationReleases=0,angularWrites=0;
Vec angularVelocity{};
void __cdecl SetStance(void* h,int stance) {Field<int>(h,0x2fc)=stance;}
void __cdecl SetHumanState(void* h,int state,int movement,float rate) {
    Check(h==human&&state==2&&movement==2&&rate==-1,"air exit uses native falling state without overwriting input masks");
    Field<int>(h,0x2f4)=state;Field<int>(h,0x2f8)=movement;
}
void __cdecl SetMovement(void* h,int mode,int submode) {
    Check(Field<uint32_t>(h,0xac4)==None&&mode==5&&submode==4,"fall movement starts only after native seat cleanup");
    Field<int>(h,0x304)=mode;Field<int>(h,0x308)=submode;++fallTransitions;
}
void __cdecl PhysicsWake(void* h,char force) {Check(h==human&&force==1,"native airborne physics wake ABI");}
void __cdecl ReleaseAnimation(void* a,int slot,int value) {
    Check(a==animationData&&slot==0x400&&value==0,"release native vehicle root animation");++animationReleases;
}
void __cdecl FadeAnimation(void* a,int slot,int value) {Check(a==animationData&&slot==0x400&&value==10,"vehicle pose blends out using native exit timing");}
void __cdecl AlignHuman(void* a,void* h,const Vec* up,float duration) {Check(a==animationData&&h==human&&up->y==1&&duration>0,"air exit returns human to upright pose");}
void* __cdecl Body(uint32_t handle) {Check(handle==123,"angular correction resolves native physics handle");return car;}
void __cdecl AngularGet(void*,Vec* out) {*out=angularVelocity;}
void __cdecl AngularSet(void* body,const Vec* v) {Check(body==car&&Finite(*v),"angular control writes only owned native body");angularVelocity=*v;++angularWrites;}
float dt=1.0f/60,sensitivity=1,orbitInputs[9]{};double mouseScale=.0025;
int inputCalls=0;
char __cdecl Invert(int,int,char) {return 0;}
void __cdecl Mouse(int* x,int* y,int* wheel) {*x=*y=*wheel=0;}
void __cdecl InputOriginal() {++inputCalls;}
void __fastcall EngineStart(void*,void*,bool) {}
unsigned char weapon[0x280]{},weaponInfo[560]{},donor[560]{};
unsigned char* weaponTable=donor;unsigned weaponCount=1;
int shotSound=-1,shots=0;
int screen[2]{1920,1080},reticle=17,draws=0;
struct Sprite {const char* name;uint32_t sheet;int x,y,width,height;};
Sprite sprite{"ui_hud_reti_assault",0,114,424,28,28};unsigned spriteCount=1;
float atlasScale=.25f;int lastRed=0,lastGreen=0,lastBlue=0;
Vec muzzleOrigin{2,4,0};int muzzleCalls=0;
unsigned mapFlags=0;unsigned char renderState[0x80]{};void* renderContext=renderState;
bool otherInteraction=false;
void* __fastcall Lookup(void*,void*,uint32_t handle) {return objects.count(handle)?objects[handle]:nullptr;}
int __cdecl CampaignMode() {return 1;}
char __cdecl False() {return 0;}
char __cdecl PlayerReady(void*) {return 1;}
void __cdecl GetVelocity(uint32_t,Vec* out) {*out=speed;}
char __cdecl Ray(const Vec* start,const Vec* end,Vec* out,float radius,uint32_t mask) {
    Check(radius==0&&mask==rayMask,"ground/aim ray uses verified native collision ABI");
    if(start->y>=floorY && end->y<=floorY && start->y!=end->y) {
        *out=*start+(*end-*start)*((start->y-floorY)/(start->y-end->y));return 1;
    }
    return 0;
}
void __fastcall EngineStop(void* value,void*,bool a,bool b) {Check(value==car&&a&&!b,"native engine-stop ABI includes both arguments");Check(Field<int>(value,0x4190)==-1,"AI suspension cannot cancel explicit landing");++stops;}
void __fastcall Gear(void* value,void*,bool up) {Check(value==car&&!up,"native gear lowers on landing");++gears;}
char __cdecl Enter(void* h,void* v,Gunship::EnterData* entry) {
    Check(h==human&&v==car,"entry uses real human and flyer objects");
    Check(entry->seat==0&&entry->previous==-1&&entry->method==0&&entry->flags==1,"native driver entry uses instant transition only");
    ++entries;Field<uint32_t>(h,0xac4)=10;Field<int>(h,0xacc)=0;Field<uint32_t>(v,0x3d14)=20;return 1;
}
void __cdecl Exit(void* h,uint8_t* flags) {
    Check(h==human&&*flags==1,"native immediate exit retains full engine cleanup");
    if(Gunship::bailout.player==h) Check((Field<uint32_t>(h,0xc0)&Gunship::NoRagdoll)!=0,"ragdoll guard is active before native exit can inherit ship speed");
    if(refuseExit) return;
    ++exits;Field<uint32_t>(h,0xac4)=None;Field<uint32_t>(car,0x3d14)=None;
    Field<int>(h,0x2f4)=0;Field<int>(h,0x304)=1;Field<int>(h,0x308)=0;Field<int>(h,0x2fc)=1;
    if(retireOnExit) objects.erase(10);
}
void __cdecl InteractionsOriginal(void* h) {
    Field<Gunship::Useable>(h,0xce4)=otherInteraction?
        Gunship::Useable{30,7,4,dot+1,0,&Gunship::Use}:Gunship::Useable{None,-1,-1,-FLT_MAX,FLT_MAX,nullptr};
    if(nativeExitPrompt) Field<Gunship::Useable>(h,0xce4)={10,7,4,dot,0,&Gunship::Use};
}
char __cdecl InteractionAllowed(void* h,Gunship::Useable* p) {return (Field<uint32_t>(h,0x24b0)&(1u<<p->type))!=0;}
char __cdecl InteractionPreferred(void* h,Gunship::Useable* p) {
    const auto& current=Field<Gunship::Useable>(h,0xce4);
    return p->dot>current.dot || (p->dot==current.dot && p->distance<current.distance);
}
void* __cdecl NoTurret(void*) {return nullptr;}
int Collect(void* h,Gunship::Useable* p) {Gunship::Interactions(h);*p=Field<Gunship::Useable>(h,0xce4);return p->handle!=None;}
int __cdecl UseOriginal(void*,char) {++fallbacks;return 17;}
char __cdecl HeldOriginal(int,int) {++fallbacks;return 7;}
char __cdecl MappedAim(int action,int context) {
    aimAction=action;Check(context==inputContext,"aim uses current native input context");
    return action==0x1a&&aimDown;
}
char __cdecl HoldAim() {return holdAim;}
void __fastcall FlightOriginal(void*,void*,float) {++flights;}
void __fastcall Hold(void* value,void*,const Vec* point,bool stop) {Check(value==car&&stop&&Finite(*point),"forced driver loss requests native hover instead of leaving cruise active");++holds;}
void __cdecl Align(Vec* look,float seconds) {lastLook=*look;Check(seconds>0,"camera alignment has nonzero interpolation duration");}
void __cdecl CameraOriginal(float,char) {
    ++views;Check(orbitMode==0&&Near(Field<float>(profile,0x64),.3f),"native camera receives scoped pilot aim");
    cameraDistance=Field<float>(profile,0x68);cameraHeight=Field<float>(profile,0xc);
    idealFov=1.1f; // Native chase update resets ideal FOV before the hook zooms it.
}
void __fastcall AimOriginal(void*,void*) {++aims;}
int* __cdecl Screen() {return screen;}
void __cdecl Color(int r,int g,int b,int) {lastRed=r;lastGreen=g;lastBlue=b;Field<float>(renderState,0x14)=1;}
int __cdecl SpriteTexture(unsigned i) {Check(i==0,"sprite resolved from campaign VINT table");return reticle;}
void __cdecl TextureMetrics(int texture,unsigned* w,unsigned* h,float* sx,float* sy) {
    Check(texture==reticle,"native atlas metrics requested for resolved sheet");
    *w=*h=unsigned(1024/atlasScale);*sx=*sy=atlasScale;
}
void __fastcall MuzzleOriginal(void*,void*,Vec* origin,Vec* direction,Vec* muzzle) {
    ++muzzleCalls;*origin=muzzleOrigin;*muzzle=muzzleOrigin;*direction={0,0,1};
}
void __cdecl Bitmap(int texture,int x,int y,int w,int h,int sx,int sy,int sw,int sh,int flipX,int flipY,void*) {
    ++draws;Check(texture==reticle&&w==h&&w>=18&&w<=64,"reticle uses native atlas with resolution-scaled dimensions");
    Check(std::abs(x+w/2-screen[0]/2)<=1&&std::abs(y+h/2-screen[1]/2)<=1,"reticle remains centred on camera aim");
    Check(sx==int(114/atlasScale)&&sy==int(424/atlasScale)&&sw==int(28/atlasScale)&&sh==int(28/atlasScale)&&!flipX&&!flipY,
        "reticle samples only the correct sprite with remastered atlas scaling");
    Field<int>(renderState,0x24)=texture;Field<uint32_t>(renderState,0x4c)=99;
}
void __fastcall ShotEffectsOriginal(void* w,void*) {++shots;shotSound=Field<int>(Field<void*>(w,0x148),0x84);}
int __cdecl RayFilterOriginal(void*) {return 1;}
void Setup() {
    Gunship::Reset();objects={{10,car},{20,human},{30,ordinary}};player=human;floorY=0;speed={};otherInteraction=false;
    focused=true;std::memset(keys,0,sizeof(keys));aimDown=false;holdAim=true;retireOnExit=false;refuseExit=false;nativeExitPrompt=false;
    angularVelocity={};
    std::memset(car,0,sizeof(car));std::memset(ordinary,0,sizeof(ordinary));std::memset(human,0,sizeof(human));
    Field<uint8_t>(car,0x7e)=3;Field<uint8_t>(car,0x7f)=5;Field<uint8_t>(car,0x350c)=1;Field<uint8_t>(car,0x40b9)=1;
    Field<void*>(car,0x40bc)=reinterpret_cast<void*>(1);Field<void*>(car,0x3514)=info;
    Field<const char*>(info,0)="EDF_AAXAir_1";Field<float>(info,0x18)=-1.4f;
    Field<uint32_t>(car,0x6c)=10;Field<uint32_t>(car,0x4c)=123;Field<uint32_t>(car,0x3d14)=None;
    Field<Vec>(car,4)={0,1.4f,0};Field<Vec>(car,0x4104)={0,2,0};
    Field<float>(car,0x10)=1;Field<float>(car,0x20)=1;Field<float>(car,0x30)=1;
    Field<uint32_t>(human,0x6c)=20;Field<uint32_t>(human,0xac4)=None;Field<uint32_t>(human,0xce4)=10;
    Field<uint32_t>(human,0x24b0)=1u<<7;
    Field<void*>(human,0x32c)=animationOwner;Field<void*>(animationOwner,4)=animationData;
    Field<Vec>(human,4)={-2.418f,.09f,.56f};
    Gunship::originalInteractions=&InteractionsOriginal;Gunship::originalUse=&UseOriginal;Gunship::originalHeld=&HeldOriginal;
    Gunship::originalInput=&InputOriginal;
    Gunship::originalFlight=reinterpret_cast<Gunship::FlightFn>(&FlightOriginal);
    Gunship::originalCamera=&CameraOriginal;Gunship::originalAim=reinterpret_cast<Gunship::AimFn>(&AimOriginal);
    Gunship::originalShotEffects=reinterpret_cast<Gunship::ShotEffectsFn>(&ShotEffectsOriginal);
    Gunship::originalRayFilter=&RayFilterOriginal;
    Gunship::originalMuzzle=reinterpret_cast<Gunship::MuzzleFn>(&MuzzleOriginal);
    Gunship::Track(car);
}
void TestMotion() {
    DistanceFog::Toggle fog;
    Check(fog.visible&&!fog.Poll(true,true,false),"fog starts on and a held startup key cannot toggle it");
    fog.Poll(false,true,false);
    Check(fog.Poll(true,true,false)&&!fog.visible,"fresh F8 press disables distance fog");
    Check(!fog.Poll(true,true,false)&&!fog.visible,"holding F8 does not flicker fog");
    fog.Poll(false,true,false);
    Check(fog.Poll(true,true,false)&&fog.visible,"second F8 press restores fog");
    fog.Poll(false,true,false);fog.Poll(true,true,true);
    Check(!fog.Poll(true,true,false)&&fog.visible,"Ctrl+F8 and releasing its modifier while held cannot toggle fog");
    fog.Poll(false,true,false);fog.Poll(true,false,false);
    Check(!fog.Poll(true,true,false)&&fog.visible,"returning focus with F8 held cannot toggle fog");
    ExitInput exit;
    for(int n=0;n<120;++n) Check(!exit.Sample(true,true),"boarding key held across frames never requests exit");
    Check(!exit.Sample(false,true)&&exit.Sample(true,true),"release followed by a new press requests exit");
    Check(!exit.Sample(true,true)&&!exit.pressed,"held exit key cannot repeat its activation");
    Check(!exit.Sample(false,false)&&!exit.Sample(true,true),"loss of focus does not fake a release and rearm exit");
    Check(!exit.Sample(false,true)&&exit.Sample(true,true),"fresh release/press restores exit after focus returns");
    exit.Reset();Check(!exit.Sample(true,true),"reboarding disarms the previous exit press");
    Motion m;Input input;input.enabled=true;input.forward=1;
    m.Reset({},0);for(int n=0;n<240;++n) m.Step(input,{},0,1.0f/60);
    Check(m.velocity.z>50.3f&&m.velocity.z<=50.401f,"sustained forward flight reaches 80 percent faster cruise speed");
    Check(!m.holding&&Near(m.velocity.x,0),"forward input produces no unintended strafe");
    Motion diagonal;diagonal.Reset({},0);input.side=1;
    for(int n=0;n<240;++n) diagonal.Step(input,{},0,1.0f/60);
    Check(Length(diagonal.velocity)<=50.401f,"diagonal flight is not faster than straight flight");
    Motion slow,fast;slow.Reset({},0);fast.Reset({},0);input.side=0;
    for(int n=0;n<30;++n) slow.Step(input,{},0,1.0f/30);
    for(int n=0;n<144;++n) fast.Step(input,{},0,1.0f/144);
    Check(Near(slow.velocity.z,fast.velocity.z,.2f),"cruise ramp stays consistent across frame rates");
    Motion maneuver;maneuver.Reset({},0);
    for(int n=0;n<20;++n) maneuver.Step(input,{},0,1.0f/60);
    Check(maneuver.velocity.z<28,"short forward taps retain maneuvering speed");
    input={};input.enabled=true;input.side=1;input.vertical=1;
    for(int n=0;n<240;++n) maneuver.Step(input,{},0,1.0f/60);
    Check(Near(maneuver.velocity.x,28,.01f)&&Near(maneuver.velocity.y,12.0f,.01f),"strafe speed unchanged and climb speed increased 50 percent");
    input.side=0;input.forward=-1;input.vertical=-1;
    for(int n=0;n<240;++n) maneuver.Step(input,{},0,1.0f/60);
    Check(Near(maneuver.velocity.z,-28,.01f)&&Near(maneuver.velocity.y,-12.0f,.01f),"reverse speed unchanged and descent speed increased 50 percent");
    input.enabled=false;input.forward=1;input.vertical=1;input.mouseX=100;
    for(int n=0;n<300;++n) m.Step(input,{4,5,6},0,1.0f/60);
    Check(m.holding&&Length(m.velocity)==0&&Near(m.anchor.x,4)&&Near(m.yaw,0),"disabled input brakes and holds without mouse/key leakage");
    input={};input.enabled=true;input.mouseY=100;input.mouseX=100;
    for(int n=0;n<60;++n) m.Step(input,{},0,1.0f/60);
    Check(Near(m.pitch,-.65f)&&std::abs(m.yaw)<=.901f,"aim clamps to native gunship arcs");
    input.mouseY=-100;for(int n=0;n<60;++n) m.Step(input,{},0,1.0f/60);
    Check(Near(m.pitch,.32f),"upward aim bounded");
    m.Reset({},0);input={};input.enabled=true;
    for(int n=0;n<120;++n) m.Step(input,{},std::sin(float(n))*.2f,1.0f/60);
    Check(Near(m.yaw,0),"hull wobble cannot move the pilot's stationary aim");
    auto before=m;m.Step(input,{NAN,0,0},0,.01f);m.Step(input,{},NAN,.01f);m.Step(input,{},0,NAN);
    Check(Near(m.yaw,before.yaw)&&Finite(m.velocity),"invalid simulation samples do not corrupt controller state");
    Check(SafeParking(0,0,1)&&!SafeParking(20,0,1)&&!SafeParking(0,8,1)&&!SafeParking(0,0,.1f),"automatic parking requires landed slow upright craft");
    Check(DescentScale(0)<.1f&&DescentScale(4)<.4f&&DescentScale(100)==1,"descent tapers near ground for a gentle landing");
    Check(!SafeParking(NAN,0,1)&&!SafeParking(0,NAN,1)&&!SafeParking(0,0,NAN),"invalid surface and motion data fail closed for automatic parking");
    Check(Supported("EDF_AAXAir_1")&&Supported("EDF_AAXAir_2")&&Supported("EDF_AAXAir_Gauss")&&!Supported("EDF_ABXAir_1")&&!Supported(nullptr),"exact gunship allowlist excludes bomber and unknown assets");
}
void TestEntry() {
    Setup();Gunship::Useable prompt{};
    Gunship::rayIgnoredCraft=car;
    Check(Gunship::RayFilter(car)==0&&Gunship::RayFilter(ordinary)==1,"landing trace cannot mistake its own hull for ground after pilot loss");
    Gunship::rayIgnoredCraft=nullptr;
    Check(Gunship::RayFilter(car)==1,"normal camera collision filters are preserved outside a gunship trace");
    Check(Collect(human,&prompt)==1&&prompt.handle==10&&prompt.type==7&&prompt.icon==4,"gunship prompt resolves even when native car collector is skipped");
    Check(prompt.use==&Gunship::Use,"prompt invokes scoped native entry adapter");
    Check(Gunship::Use(human,0)==1&&entries==0,"query does not enter vehicle");
    Check(Gunship::Use(human,1)==1&&entries==1&&Gunship::Driven(human)==car,"activation enters native driver seat");
    Check(!Gunship::exitInput.Sample(true,true)&&Gunship::Use(human,1)==0&&exits==0,
        "boarding press cannot eject through either direct or native interaction path");
    Check(!Gunship::exitInput.Sample(false,true)&&Gunship::Use(human,1)==0,
        "release alone cannot activate the native exit callback");
    Check(Gunship::exitInput.Sample(true,true),"second deliberate E press is accepted after boarding");
    nativeExitPrompt=true;
    Check(Collect(human,&prompt)==0,"grounded pilot has no exit icon even if native resolver supplies one");
    Field<Vec>(car,4)={200,1.4f,200};
    Check(Gunship::CanPark(car),"local ground detection permits landing away from the garage parking plane");
    Field<Vec>(car,4)={0,50,0};
    speed={50,12,0};
    nativeExitPrompt=true;
    Check(Collect(human,&prompt)==0&&Gunship::Use(human,1)==1&&exits==1&&stops==1&&gears==1,
        "moving airborne pilot can bail out through native detach and engine cleanup");
    Check(Field<int>(human,0x2f4)==2&&Field<int>(human,0x304)==5&&Field<int>(human,0x308)==4&&
        Field<int>(human,0x2fc)==0&&animationReleases>0,"airborne exit frees seated pose and activates normal falling movement");
    nativeExitPrompt=false;
    Field<Vec>(car,4)={0,1.4f,0};speed={};
    Field<uint32_t>(human,0xce4)=30;
    Check(Gunship::Use(human,1)==17,"ordinary world vehicles use their original action");
    Field<uint32_t>(human,0xce4)=10;Field<uint32_t>(human,0xdcc)=0x80;
    Check(Collect(human,&prompt)==0&&!Gunship::CanEnter(human,car),"scripts disabling input also disable custom entry");
    Field<uint32_t>(human,0xdcc)=0;Field<uint32_t>(car,0x3d14)=999;
    Check(Collect(human,&prompt)==0,"occupied gunships are not hijacked by prompt");
    Field<uint32_t>(car,0x3d14)=None;Field<uint32_t>(human,0x24b0)=0;
    Check(Collect(human,&prompt)==0,"native interaction permission mask is respected");
    Field<uint32_t>(human,0x24b0)=1u<<7;otherInteraction=true;
    Check(Collect(human,&prompt)==1&&prompt.handle==30,"higher-priority native interaction stays selected");otherInteraction=false;
    Field<Vec>(car,4)={0,4,0};Field<Vec>(human,4)={2.418f,2.69f,.56f};
    Check(!Gunship::CanPark(car)&&Collect(human,&prompt)==1,"boarding from opposite side on raised pad does not require terrain exit clearance");
    Field<Vec>(car,4)={0,50,0};
    Check(Collect(human,&prompt)==0,"player cannot board a craft hovering above reach");
    Field<Vec>(car,4)={0,4,0};speed={3,0,0};
    Check(Collect(human,&prompt)==0,"fast-moving craft cannot be boarded");speed={};
    Field<float>(car,0x20)=.1f;
    Check(Collect(human,&prompt)==0,"overturned craft cannot be boarded");Field<float>(car,0x20)=1;
    Field<uint32_t>(human,0xb8)=0x8000;
    Check(Collect(human,&prompt)==0,"native no-interaction state is preserved");Field<uint32_t>(human,0xb8)=0;
    unsigned char action[16]{},state[40]{},actionInfo[16]{};
    Field<void*>(human,0xc90)=action;Field<void*>(action,8)=state;Field<void*>(state,0x1c)=actionInfo;actionInfo[0xd]=1;
    Check(Collect(human,&prompt)==0,"script action blocking interaction is preserved");Field<void*>(human,0xc90)=nullptr;
}
void TestAttitude() {
    auto rotate=[](Vec v,Vec angular,float step) {
        const float angle=Length(angular)*step;const Vec axis=Unit(angular);
        return v*std::cos(angle)+Cross(axis,v)*std::sin(angle)+axis*(Dot(axis,v)*(1-std::cos(angle)));
    };
    for(int hz:{30,60,144}) {
        Vec right{1,0,0},up{0,1,0},forward{0,0,1},angular{.3f,-.4f,.2f};
        const float step=1.0f/hz;
        for(int n=0;n<hz*60;++n) {
            angular=PilotAngularVelocity(right,up,forward,angular,.6f,Forward(.6f)*50.4f,step);
            right=Unit(rotate(right,angular,step));up=Unit(rotate(up,angular,step));forward=Unit(rotate(forward,angular,step));
            Check(Finite(angular)&&Length(angular)<1.0f,"sustained travel keeps angular corrections bounded");
            if(n>hz*10) Check(Length(angular)<.001f,"steady W converges instead of repeatedly rocking or turning");
        }
        Check(std::abs(Wrap(std::atan2(forward.x,forward.z)-.6f))<.001f&&std::abs(forward.y+std::sin(.08f))<.001f,
            "frame-rate-independent pilot attitude converges to heading with gentle forward lean");
        angular={.8f,.5f,-.6f}; // Collision disturbance, not a change in heading.
        for(int n=0;n<hz*8;++n) {
            angular=PilotAngularVelocity(right,up,forward,angular,.6f,{},step);
            right=Unit(rotate(right,angular,step));up=Unit(rotate(up,angular,step));forward=Unit(rotate(forward,angular,step));
        }
        Check(Length(angular)<.002f&&up.y>.999f,"hover recovers smoothly after an angular disturbance");
    }
}
void TestFlightScope() {
    Setup();Gunship::motion.Reset({1,2,3},.5f);Gunship::motion.holding=false;Gunship::motion.velocity={3,4,0};
    Gunship::Publish(car,true);Gunship::Flight(car,nullptr,.016f);
    Check(flights==1&&Field<uint8_t>(car,0x40ed)==1&&Near(Field<float>(car,0x40c4),5),"native flight receives requested speed and direction");
    Check(Near(Length(Field<Vec>(car,0x40d4)),1)&&Near(Field<Vec>(car,0x40d4).y,.8f),"native direction normalized instead of double scaling speed");
    Check(Field<uint8_t>(car,0x40ef)==0,"continuous travel must not brake toward the native 100m lookahead point");
    Check(Field<uint8_t>(car,0x40ee)==1&&Near(Field<Vec>(car,0x40e0).x,std::sin(.5f)),"native yaw controller follows mouse heading");
    Gunship::Flight(ordinary,nullptr,.016f);
    Check(flights==2&&Field<float>(ordinary,0x40c4)==0,"ambient enemy aircraft retain untouched native flight");
    Gunship::motion.holding=true;Gunship::motion.anchor={2,1.4f,3};Gunship::motion.velocity={};Gunship::Publish(car,false);
    Gunship::Flight(car,nullptr,.016f);
    Check(Field<uint8_t>(car,0x40ec)==1&&Field<uint8_t>(car,0x40ed)==0&&Near(Field<Vec>(car,0x40c8).y,2),"hover uses COM-adjusted native stopping point");
    Check(Near(Field<float>(car,0x40c4),2)&&Length(Field<Vec>(car,0x4164))==0,"gentle position hold removes AI hover displacement");
    Check(Field<uint8_t>(car,0x40ef)==1,"releasing flight input restores native stop-at-point for hover");
    Gunship::EnterData entry;Enter(human,car,&entry);
    Field<uint32_t>(car,0x3f24)|=1u<<13;
    Gunship::motion.yaw=0;Gunship::Publish(car,true);
    angularVelocity={.2f,.3f,-.2f};const int beforeAngular=angularWrites;
    Gunship::Flight(car,nullptr,.016f);
    Check(angularWrites==beforeAngular+1&&Length(angularVelocity)<.4124f,"pilot hook damps actual pre-step angular velocity");
    Gunship::Flight(ordinary,nullptr,.016f);
    Check(angularWrites==beforeAngular+1,"enemy aircraft never receive pilot attitude correction");
    Field<float>(car,0x20)=-1;Gunship::Flight(car,nullptr,.016f);
    Check(angularWrites==beforeAngular+1,"inverted crash retains native collision rotation");Field<float>(car,0x20)=1;
    Field<uint32_t>(human,0xdcc)=0x88;Gunship::Flight(car,nullptr,.016f);
    Check(angularWrites==beforeAngular+1,"scripted control restrictions disable attitude correction");Field<uint32_t>(human,0xdcc)=0;
    Gunship::Forget(car,999);Check(Gunship::Registered(car),"unrelated recycled handle cannot clear live registration");
    Gunship::Forget(car,10);Check(!Gunship::Registered(car)&&Gunship::command.object==nullptr,"native deletion cancels requests before object is freed");
    Gunship::Track(car);Gunship::Track(car);Check(Gunship::crafts.size()==1,"repeated tracking does not create duplicate lifetimes");
    Gunship::Reset();Check(Gunship::crafts.empty()&&Gunship::command.object==nullptr,"campaign teardown clears every pilot and object reference");
    Setup();Field<Vec>(car,4)={0,100,0};Gunship::Publish(car,true);Gunship::pilot=10;Gunship::ReleasePilot();
    Check(holds==1&&Gunship::pilot==None&&Gunship::command.object==nullptr,"script ejection clears pilot and stops autonomous cruise");
}
void TestPilotControls() {
    Setup();Gunship::EnterData entry;Enter(human,car,&entry);
    Gunship::originalHeld=&MappedAim;
    Gunship::Input(); // Acquires pilot and sees all buttons released.
    keys[VK_SHIFT]=true;Gunship::Input();
    Check(Gunship::motion.velocity.y>0,"Shift ascends through the actual input hook");
    keys[VK_SHIFT]=false;keys[VK_CONTROL]=true;
    Field<Vec>(car,4)={0,100,0};
    for(int n=0;n<120;++n) Gunship::Input();
    Check(Gunship::motion.velocity.y<-11.9f,"Ctrl descends through the actual input hook");
    keys[VK_CONTROL]=false;keys['R']=keys['F']=true;
    for(int n=0;n<180;++n) Gunship::Input();
    Check(std::abs(Gunship::motion.velocity.y)<.01f,"R and F no longer change altitude");
    aimDown=true;Gunship::Input();
    Check(aimAction==0x1a&&Gunship::precision,"gunship aim reads on-foot mapped action, not a hard-coded mouse button");
    aimDown=false;keys[VK_RBUTTON]=true;Gunship::Input();
    Check(!Gunship::precision,"unbound right click cannot enable aim");
    holdAim=false;aimDown=true;Gunship::Input();aimDown=false;Gunship::Input();
    Check(Gunship::precision,"native toggle-aim preference stays active after release");
    aimDown=true;Gunship::Input();
    Check(!Gunship::precision,"second mapped press toggles aim off");
    aimDown=false;Gunship::Input();aimDown=true;Gunship::Input();focused=false;Gunship::Input();
    Check(!Gunship::precision,"focus loss clears toggled aim");focused=true;Gunship::Input();
    Check(!Gunship::precision,"held aim on return from another app cannot retoggle");
    aimDown=false;Gunship::Input();
    // Both wings lost / flipped: flight readiness fails, but input and frame
    // callbacks must retain the same release/press state for escape.
    Field<float>(car,0x3f58)=1;Field<float>(car,0x20)=-1;
    Field<void*>(car,0x40bc)=nullptr;Field<uint8_t>(car,0x40b9)=0;
    Field<uint32_t>(car,0x3f24)|=1u<<10;
    Check(!Gunship::Driven(human)&&Gunship::Occupied(human)==car,"wrecked flipped gunship retains validated seat ownership");
    Gunship::Useable prompt{};Check(Collect(human,&prompt)==0,"airborne wreck has no persistent exit icon");
    const int beforeExits=exits,beforeStops=stops;
    Gunship::Input();Gunship::Frame(reinterpret_cast<Player*>(human));
    Check(Gunship::pilot==10,"frame update cannot reset exit debounce just because flight failed");
    keys['E']=true;Gunship::Input();
    Check(exits==beforeExits+1&&Gunship::Occupied(human)==nullptr&&Gunship::pilot==None,
        "fresh E exits a destroyed inverted craft through the real input path");
    Check(stops==beforeStops,"exit never calls flight-controller routines on wrecked craft");
    Check(Gunship::command.object==nullptr,"bailout clears flight command");
    Setup();Enter(human,car,&entry);keys['E']=true;const int beforeBoard=exits;
    Gunship::Input();Gunship::Input();
    Check(exits==beforeBoard,"holding E while boarding never causes immediate ejection");
    keys['E']=false;Gunship::Input();
    Field<Vec>(car,4)={0,100,0};speed={50,12,0};keys['E']=true;Gunship::Input();
    Check(exits==beforeBoard+1,"airborne high-speed bailout works through actual input hook");
    const int airTransitions=fallTransitions;
    Setup();Enter(human,car,&entry);Gunship::Input();keys['E']=true;Gunship::Input();
    Check(fallTransitions==airTransitions&&Field<int>(human,0x304)==1,"grounded exit keeps native walking transition");
    Field<int>(human,0x2f4)=10;Gunship::FinishAirborneExit(human);
    Check(fallTransitions==airTransitions,"unfinished native exit must not have its scripted transition interrupted");
    Setup();Enter(human,car,&entry);Gunship::Input();keys['E']=true;
    Field<uint32_t>(human,0xdcc)=0x88;const int beforeScript=exits;Gunship::Input();
    Check(exits==beforeScript,"mission script input blocks are respected");
    Field<uint32_t>(human,0xdcc)=0;Gunship::Input();
    Check(exits==beforeScript,"held key after script input block cannot immediately eject");
    Setup();Enter(human,car,&entry);Gunship::Input();retireOnExit=true;keys['E']=true;
    const int beforeRetire=stops;Gunship::Input();
    Check(stops==beforeRetire&&Gunship::pilot==None,"native exit retiring a wreck cannot cause a stale vehicle engine call");
}
void TestCameraWeapons() {
    Setup();Gunship::EnterData entry;Enter(human,car,&entry);
    Gunship::motion.Reset(Field<Vec>(car,4),.4f);Gunship::motion.pitch=-.3f;Gunship::Publish(car,true);
    Field<float>(profile,0x64)=.25f;Field<float>(profile,0xc)=2;Field<float>(profile,0x68)=8;Gunship::Camera(.016f,0);
    Check(views==1&&orbitMode==1&&Near(Field<float>(profile,0x64),.25f),"camera control restores user's settings after native chase update");
    Check(Near(lastLook.x,std::sin(.4f)),"camera points in the same heading as flight controller");
    Check(Near(cameraDistance,23)&&Near(cameraHeight,3),"chase camera has a modest one metre lift instead of the excessive five metre lift");
    Check(Near(Field<float>(profile,0xc),2)&&Near(Field<float>(profile,0x68),8),"height and distance restored without cumulative camera drift");
    Gunship::precision=true;
    Gunship::Camera(.016f,0);
    Check(idealFov<1.1f&&idealFov>1.1f*.65f,"aim zoom starts smoothly");
    for(int n=0;n<30;++n) Gunship::Camera(.016f,0);
    Check(Near(idealFov,1.1f*.65f)&&Near(cameraDistance,23)&&Near(cameraHeight,3),
        "aim narrows actual ideal FOV without moving the approved chase camera");
    for(int n=0;n<30;++n) Gunship::Camera(.016f,0);
    Check(Near(idealFov,1.1f*.65f),"zoom uses fresh native FOV and never compounds");
    Gunship::precision=false;
    for(int n=0;n<30;++n) Gunship::Camera(.016f,0);
    Check(Near(idealFov,1.1f),"aim release restores normal field of view");
    Gunship::precision=true;Gunship::Camera(.1f,0);Gunship::ClearZoom();
    Check(Near(idealFov,1.1f),"bailout restores pending zoom immediately");
    Gunship::Camera(.1f,0);idealFov=.9f;Gunship::ClearZoom();
    Check(Near(idealFov,.9f),"zoom cleanup never overwrites a new camera owner's FOV");
    Gunship::precision=false;
    Field<uint32_t>(turret,0x70)=10;Field<uint32_t>(turret,0xa0)=20;Field<int>(turret,0x150)=123;
    Gunship::Aim(turret,nullptr);
    Check(aims==1&&Field<Vec>(turret,0x10c).y<0&&Field<int>(turret,0x150)==-1,"native turret seeks downward aim with stale manual timer removed");
    cameraPosition={0,10,-20};cameraForward={0,-.2f,1};Gunship::Aim(turret,nullptr);
    Check(Near(Field<Vec>(turret,0x10c).y,0)&&Near(Field<Vec>(turret,0x10c).z,30,.01f),"crosshair camera ray and mounted turret converge on the same terrain point");
    objects[40]=turret;Field<uint32_t>(weapon,0x14c)=40;
    Vec origin{},direction{},barrel{};
    Gunship::Muzzle(weapon,nullptr,&origin,&direction,&barrel);
    const Vec target=Field<Vec>(turret,0x10c);
    Check(Near(Length(direction),1)&&Near(origin.x,2)&&Near(origin.y,4)&&Near(barrel.x,2),"aim correction preserves native projectile and muzzle origins");
    Vec impact=origin+direction*((target.z-origin.z)/direction.z);
    Check(Length(impact-target)<.001f,"offset muzzle converges on the crosshair terrain point rather than firing parallel to camera");
    Check(direction.x<0&&direction.y<0&&direction.z>0,"parallax correction includes lateral and vertical camera offsets");
    cameraForward={0,0,1};Gunship::Muzzle(weapon,nullptr,&origin,&direction,&barrel);
    Check(Near(Field<Vec>(turret,0x10c).z,980)&&Near(Field<Vec>(turret,0x10c).y,10),"sky aiming uses forward ray endpoint even when native trace leaves output untouched");
    Field<uint32_t>(turret,0x70)=30;Gunship::Muzzle(weapon,nullptr,&origin,&direction,&barrel);
    Check(direction.x==0&&direction.y==0&&direction.z==1,"world and enemy gunship shot directions remain native");
    Field<uint32_t>(turret,0x70)=10;Field<uint32_t>(human,0xdcc)=0x88;
    Gunship::Muzzle(weapon,nullptr,&origin,&direction,&barrel);
    Check(direction.x==0&&direction.y==0,"script-blocked pilot does not override native shots");Field<uint32_t>(human,0xdcc)=0;
    Check(muzzleCalls==4,"all shot paths run original muzzle calculation exactly once");
    cameraPosition={};cameraForward={};
    Field<uint32_t>(turret,0x70)=30;Field<Vec>(turret,0x10c)={1,2,3};Gunship::Aim(turret,nullptr);
    Check(Near(Field<Vec>(turret,0x10c).x,1),"unrelated turrets keep their native target");
    Field<uint32_t>(turret,0x70)=10;objects[40]=turret;
    Field<void*>(weapon,0x148)=weaponInfo;Field<uint32_t>(weapon,0x14c)=40;
    Field<const char*>(weaponInfo,0)="turret_edfrocket_flyer";Field<int>(weaponInfo,0x84)=-1;
    Field<const char*>(donor,0)="turret_rocket_vehicle";Field<int>(donor,0x84)=77;
    Gunship::ShotEffects(weapon,nullptr);
    Check(shots==1&&shotSound==77&&Field<void*>(weapon,0x148)==weaponInfo&&Field<int>(weaponInfo,0x84)==-1,
        "rocket shot borrows native cue then restores weapon identity and global data");
    Field<uint32_t>(turret,0x70)=30;Gunship::ShotEffects(weapon,nullptr);
    Check(shotSound==-1,"enemy rocket audio remains native");
    EquipmentTuning::ExplosionInfo explosion{};
    EquipmentTuning::Write(explosion,0x30,4.5f);EquipmentTuning::Write(explosion,0x60,19000);
    auto* tuned=Gunship::TuneExplosion(explosion.data(),human,weaponInfo);
    Check(tuned!=explosion.data()&&Field<int>(tuned,0x60)==38000&&Near(Field<float>(tuned,0x30),5.85f),"pilot rockets increase structural damage and crumble radius");
    Check(Gunship::TuneExplosion(explosion.data(),ordinary,weaponInfo)==explosion.data()&&EquipmentTuning::Read<int>(explosion,0x60)==19000,
        "enemy damage and original explosion definition are unchanged");
    Check(std::wcscmp(Gunship::ControlHint(L"Left click auto aim"),L"Left mouse: fire. Aim: F by default. Shift/Ctrl: ascend/descend. E: exit or bail out.")==0,
        "native auto-aim hint describes actual pilot controls");
    Gunship::dispatching=true;Gunship::fire=true;
    Check(Gunship::Held(0x21,0)==1&&Gunship::Held(0x2c,0)==0,"pilot mouse buttons fire native weapon without right-click zoom conflict");
    Gunship::fire=false;Check(Gunship::Held(0x21,0)==0,"releasing fire reaches native stop-fire path");
    Gunship::dispatching=false;Check(Gunship::Held(0x21,0)==7,"on-foot weapon input remains native");
    Field<float>(renderState,0x14)=.2f;Field<int>(renderState,0x24)=4;Field<uint32_t>(renderState,0x4c)=7;
    Gunship::RenderHud();
    Check(draws==2&&Near(Field<float>(renderState,0x14),.2f)&&Field<int>(renderState,0x24)==4&&Field<uint32_t>(renderState,0x4c)==7,
        "native crosshair and contrast outline preserve surrounding HUD render state");
    Check(lastRed==255&&lastGreen==160&&lastBlue==10,"campaign reticle uses orange tint");
    atlasScale=1;screen[0]=1280;screen[1]=720;Gunship::RenderHud();
    Check(draws==4,"sprite works for original-resolution atlas and lower screen resolution");
    atlasScale=.25f;screen[0]=1920;screen[1]=1080;
    cameraTarget=20;Gunship::RenderHud();
    Check(draws==6,"pilot reticle survives camera target changing from vehicle to local human");
    cameraPosition={0,10,-20};cameraForward={0,0,1};
    Check(Near(Gunship::AimTarget(Gunship::command).z,980),"turret still uses actual camera ray during camera target refresh");
    cameraTarget=10;cameraPosition={};cameraForward={};draws=4;
    cameraMode=1;Gunship::RenderHud();Check(draws==4,"script camera does not show pilot crosshair");cameraMode=0;
    Field<uint32_t>(human,0xdcc)=0x88;Gunship::RenderHud();Check(draws==4,"script input block still hides pilot sight");Field<uint32_t>(human,0xdcc)=0;
    mapFlags=0x60;Gunship::RenderHud();Check(draws==6,"stale fullscreen-map resource flags cannot hide a sight drawn by the minimap branch");mapFlags=0;draws=4;
    reticle=-1;Gunship::RenderHud();Check(draws==4,"missing native texture is never submitted to renderer");reticle=17;
    Gunship::RenderHud();Check(draws==6,"reticle resumes after native sheet becomes available again");
}
void TestBailout() {
    Setup();Gunship::EnterData entry;Enter(human,car,&entry);Gunship::Input();
    Field<Vec>(car,4)={0,100,0};speed={50.4f,12,0};
    Field<uint32_t>(human,0xc0)=0x104;keys['E']=true;Gunship::Input();
    Check(Gunship::bailout.player==human&&(Field<uint32_t>(human,0xc0)&Gunship::NoRagdoll),"full-speed bailout keeps native ragdoll disabled after detachment");
    for(int n=0;n<3600;++n) Gunship::UpdateBailout(1.0f/60);
    Check(Gunship::bailout.player==human,"long descent cannot time out jetpack-safe bailout");
    Field<int>(human,0x304)=4;Gunship::UpdateBailout(.1f);
    Check(Gunship::bailout.player==human,"jump or jetpack movement keeps bailout protection");
    Field<int>(human,0x2f4)=0;Field<int>(human,0x304)=1;Field<uint32_t>(human,0x2b4)=10;
    for(int n=0;n<30;++n) Gunship::UpdateBailout(.1f);
    Check(Gunship::bailout.player==human,"standing on own gunship cannot end airborne protection");
    Field<uint32_t>(human,0x2b4)=None;
    const Vec feet=Field<Vec>(human,4);Field<Vec>(human,4)={0,100,0};
    for(int n=0;n<20;++n) Gunship::UpdateBailout(.1f);
    Check(Gunship::bailout.player==human,"a direct-movement animation in midair is not mistaken for landing");
    Field<Vec>(human,4)=feet;
    Gunship::UpdateBailout(.1f);Field<int>(human,0x2f4)=2;Gunship::UpdateBailout(.1f);
    Check(Gunship::bailout.player==human&&Gunship::bailout.grounded==0,"brief contact does not count as a completed landing");
    Field<int>(human,0x2f4)=0;
    for(int n=0;n<4;++n) Gunship::UpdateBailout(.1f);
    Check(!Gunship::bailout.player&&Field<uint32_t>(human,0xc0)==0x104,"landing restores only the flag changed by bailout");
    Gunship::BeginBailout(human,10);Field<uint32_t>(human,0xbc)=0x80000000u;Gunship::UpdateBailout(.1f);
    Check(!Gunship::bailout.player&&!(Field<uint32_t>(human,0xc0)&Gunship::NoRagdoll),"death ends guard without blocking native death physics");
    Field<uint32_t>(human,0xbc)=0;Field<uint32_t>(human,0xc0)|=Gunship::NoRagdoll;
    Gunship::BeginBailout(human,10);Gunship::EndBailout();
    Check((Field<uint32_t>(human,0xc0)&Gunship::NoRagdoll)!=0,"preexisting native or RSL ragdoll setting is preserved");
    Field<uint32_t>(human,0xc0)=0;Gunship::BeginBailout(human,10);Field<uint32_t>(human,0xac4)=30;Gunship::UpdateBailout(.1f);
    Check(!Gunship::bailout.player&&Field<uint32_t>(human,0xc0)==0,"entering another vehicle ends bailout protection");
    Field<uint32_t>(human,0xac4)=None;Gunship::BeginBailout(human,10);Field<uint32_t>(human,0xdcc)=0x88;Gunship::UpdateBailout(.1f);
    Check(!Gunship::bailout.player,"scripted control transitions restore native ragdoll behavior");
    Field<uint32_t>(human,0xdcc)=0;Gunship::BeginBailout(human,10);objects[20]=ordinary;
    const auto staleFlags=Field<uint32_t>(human,0xc0);Gunship::UpdateBailout(.1f);
    Check(!Gunship::bailout.player&&Field<uint32_t>(ordinary,0xc0)==0&&Field<uint32_t>(human,0xc0)==staleFlags,"retired human or recycled handle is never written during cleanup");
    Setup();Enter(human,car,&entry);Gunship::Input();Field<Vec>(car,4)={0,100,0};refuseExit=true;keys['E']=true;Gunship::Input();
    Check(!Gunship::bailout.player&&!(Field<uint32_t>(human,0xc0)&Gunship::NoRagdoll),"failed exit immediately rolls back temporary ragdoll protection");
}
void TestNativeHooks(const char* path) {
    // Copy code into this test process only. Do not execute the game or attach to it.
    std::ifstream input(path,std::ios::binary);
    IMAGE_DOS_HEADER dos{};input.read(reinterpret_cast<char*>(&dos),sizeof(dos));
    Check(input.good()&&dos.e_magic==IMAGE_DOS_SIGNATURE,"read supported executable headers");
    input.seekg(dos.e_lfanew);IMAGE_NT_HEADERS32 pe{};input.read(reinterpret_cast<char*>(&pe),sizeof(pe));
    Check(pe.Signature==IMAGE_NT_SIGNATURE&&pe.FileHeader.Machine==IMAGE_FILE_MACHINE_I386,"native ABI is x86");
    std::vector<IMAGE_SECTION_HEADER> sections(pe.FileHeader.NumberOfSections);
    input.read(reinterpret_cast<char*>(sections.data()),sections.size()*sizeof(IMAGE_SECTION_HEADER));
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,pe.OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"allocate isolated hook-validation image");
    for(const auto& section:sections) if(section.SizeOfRawData) {
        input.seekg(section.PointerToRawData);
        input.read(reinterpret_cast<char*>(image+section.VirtualAddress),section.SizeOfRawData);
        Check(input.good(),"copy original code section for trampoline validation");
    }
    const auto code=[&](uintptr_t va){return image+va-pe.OptionalHeader.ImageBase;};
    // Relocate the private copy so the pure native speed planner can read its
    // constants and call its local float-comparison helper. No game is launched.
    const auto relocation=pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const uint32_t delta=uint32_t(reinterpret_cast<uintptr_t>(image))-pe.OptionalHeader.ImageBase;
    for(uint32_t offset=0;offset<relocation.Size;) {
        auto* block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(image+relocation.VirtualAddress+offset);
        Check(block->SizeOfBlock>=sizeof(*block)&&block->SizeOfBlock<=relocation.Size-offset,"valid isolated image relocation block");
        auto* entries=reinterpret_cast<uint16_t*>(block+1);
        for(unsigned n=0;n<(block->SizeOfBlock-sizeof(*block))/2;++n)
            if((entries[n]>>12)==IMAGE_REL_BASED_HIGHLOW)
                *reinterpret_cast<uint32_t*>(image+block->VirtualAddress+(entries[n]&0xfff))+=delta;
        offset+=block->SizeOfBlock;
    }
    unsigned char fallingHuman[0x4000]{};
    uintptr_t humanVtable[8]{};humanVtable[4]=reinterpret_cast<uintptr_t>(&False);
    Field<void*>(fallingHuman,0)=humanVtable;
    // Isolate the native single-player ragdoll predicate from game-mode services.
    const unsigned char falseStub[]{0x31,0xc0,0xc3};std::memcpy(code(0x00760440),falseStub,sizeof(falseStub));
    auto ragdollBlocked=reinterpret_cast<int(__cdecl*)(void*)>(code(0x00a2ea40));
    Check(ragdollBlocked(fallingHuman)==0,"actual native predicate permits normal human ragdolls");
    Field<uint32_t>(fallingHuman,0xc0)|=Gunship::NoRagdoll;
    Check(ragdollBlocked(fallingHuman)==1,"actual native predicate rejects ragdoll when bailout flag is set");
    const auto originalState=Field<int>(fallingHuman,0x33c);
    Check(reinterpret_cast<int(__cdecl*)(void*,char,int)>(code(0x00ac9b00))(fallingHuman,0,-1)==0&&Field<int>(fallingHuman,0x33c)==originalState,
        "actual native ragdoll entry aborts before changing human physics during protected bailout");
    Field<uint32_t>(fallingHuman,0xc0)&=~Gunship::NoRagdoll;
    Check(ragdollBlocked(fallingHuman)==0,"native ragdoll eligibility returns after guard cleanup");
    Field<int>(fallingHuman,0x304)=1;Field<int>(fallingHuman,0x2fc)=1;
    reinterpret_cast<void(__cdecl*)(void*,int)>(code(0x00a07910))(fallingHuman,0);
    reinterpret_cast<void(__cdecl*)(void*,int,int,float)>(code(0x00a078b0))(fallingHuman,2,2,-1.0f);
    reinterpret_cast<void(__cdecl*)(void*,int,int)>(code(0x00a07930))(fallingHuman,5,4);
    Check(Field<int>(fallingHuman,0x2f4)==2&&Field<int>(fallingHuman,0x304)==5&&Field<int>(fallingHuman,0x308)==4&&
        Field<int>(fallingHuman,0x30c)==1&&Field<int>(fallingHuman,0x2fc)==0,
        "actual native transition routines establish standing fall and preserve movement history");
    using SpeedPlanner=float(__cdecl*)(float,float,float,float,float);
    auto setFog=reinterpret_cast<void(__cdecl*)(bool)>(code(0x007c2c70));
    setFog(false);Check(*code(0x0165cbea)==0,"native RSL fog setter disables only the render fog flag");
    setFog(true);Check(*code(0x0165cbea)==1,"native RSL fog setter restores render fog");
    auto planner=reinterpret_cast<SpeedPlanner>(code(0x00b67790));
    auto cruise=[&](float distance,float limit) {
        float velocity=0;
        for(int n=0;n<6000;++n) velocity+=planner(distance,velocity,limit,2.5f,1.0f/60)/60;
        return velocity;
    };
    const float oldCruise=cruise(100,42),newCruise=cruise(1000000,50.4f);
    Check(oldCruise<30,"actual native speed planner reproduces braking below requested old cruise speed");
    Check(newCruise>=50&&newCruise<52,"actual native speed planner reaches new forward speed without fixed-distance braking");
    Check(cruise(1000000,12)>=11.9f&&cruise(1000000,12)<12.5f,"actual native planner accepts increased climb/descent speed");
    std::printf("Native planner: old travel %.2f, new travel %.2f, climb %.2f.\n",oldCruise,newCruise,cruise(1000000,12));
    Check(code(0x00b7e54d)[0]==0xc2&&code(0x00b7e54d)[1]==8,"native flyer engine-stop is RET 8, not the one-argument SDK typo");
    Check(code(0x00b7e4e3)[0]==0xc2&&code(0x00b7e4e3)[1]==4,"native engine-start is RET 4");
    Globals::ModuleBase=reinterpret_cast<uintptr_t>(image);
    Check(MH_Initialize()==MH_OK,"initialize isolated hook validator");
    IHookManager hooks;Check(Gunship::Install(hooks),"every gunship hook has a valid native-code trampoline");
    Check(MH_Uninitialize()==MH_OK,"remove all isolated validation hooks without enabling them");
    VirtualFree(image,0,MEM_RELEASE);Globals::ModuleBase=0x400000;
}
}
void* GunshipTestAddress(uintptr_t va) {
    switch(va) {
    case 0x0093c050:return reinterpret_cast<void*>(&Lookup);case 0x02f98490:return nullptr;
    case 0x03023874:return &player;case 0x02feb588:case 0x01e2a9b9:return &no;
    case 0x00760440:case 0x007c1690:case 0x00705ad0:return reinterpret_cast<void*>(&False);
    case 0x007bfcf0:return reinterpret_cast<void*>(&CampaignMode);case 0x00b689a0:return reinterpret_cast<void*>(&PlayerReady);
    case 0x008237a0:return reinterpret_cast<void*>(&GetVelocity);
    case 0x0080e870:return reinterpret_cast<void*>(&Body);
    case 0x008174e0:return reinterpret_cast<void*>(&AngularGet);
    case 0x00817430:return reinterpret_cast<void*>(&AngularSet);
    case 0x00a07910:return reinterpret_cast<void*>(&SetStance);
    case 0x00a078b0:return reinterpret_cast<void*>(&SetHumanState);
    case 0x00a07930:return reinterpret_cast<void*>(&SetMovement);
    case 0x00a53d90:return reinterpret_cast<void*>(&PhysicsWake);
    case 0x007ffc10:return reinterpret_cast<void*>(&ReleaseAnimation);
    case 0x007f7620:return reinterpret_cast<void*>(&FadeAnimation);
    case 0x007f9a40:return reinterpret_cast<void*>(&AlignHuman);
    case 0x006d6ce0:return reinterpret_cast<void*>(&Ray);case 0x0165fe88:return &rayMask;
    case 0x01de4b7c:return &cameraPosition;case 0x01de4bb8:return &cameraForward;
    case 0x03882c9c:return &weaponTable;case 0x03882c94:return &weaponCount;
    case 0x02bd9328:return &mapFlags;case 0x01cd26f8:return &renderContext;
    case 0x01a48620:return &spriteCount;case 0x01a3e798:return &sprite;
    case 0x0045ba00:return reinterpret_cast<void*>(&SpriteTexture);
    case 0x00597590:return reinterpret_cast<void*>(&TextureMetrics);
    case 0x0089bdb0:return reinterpret_cast<void*>(&Screen);case 0x00509170:return reinterpret_cast<void*>(&Color);
    case 0x005551b0:return reinterpret_cast<void*>(&Bitmap);case 0x0150a648:return nullptr;
    case 0x00b7e4f0:return reinterpret_cast<void*>(&EngineStop);case 0x00b673f0:return reinterpret_cast<void*>(&Gear);
    case 0x00bb81f0:return reinterpret_cast<void*>(&Enter);case 0x00bb6060:return reinterpret_cast<void*>(&Exit);
    case 0x012bc488:return &dot;case 0x01de4b50:return &cameraMode;case 0x01de4c18:return &cameraTarget;
    case 0x01de4c8c:return &cameraProfile;case 0x01de48b0:return &orbitMode;
    case 0x01de4c10:return &idealFov;case 0x01e2a9cc:return &inputContext;
    case 0x006fddc0:return reinterpret_cast<void*>(&HoldAim);
    case 0x01518a30:return &sensitivity;case 0x012d0298:return &mouseScale;case 0x01519560:return &dt;
    case 0x005690d0:return reinterpret_cast<void*>(&Mouse);case 0x006f95e0:return reinterpret_cast<void*>(&Invert);
    case 0x00b7e4b0:return reinterpret_cast<void*>(&EngineStart);
    case 0x01de4d3c:return &orbitInputs[0];case 0x01de4d40:return &orbitInputs[1];
    case 0x01de4d44:return &orbitInputs[2];case 0x01de4d48:return &orbitInputs[3];
    case 0x01de4d4c:return &orbitInputs[4];case 0x01de4d50:return &orbitInputs[5];
    case 0x01de4d54:return &orbitInputs[6];case 0x01de4cb0:return &orbitInputs[7];case 0x01de4cc0:return &orbitInputs[8];
    case 0x01de4cc4:return &cameraFloats[0];case 0x01de4ccc:return &cameraFloats[1];
    case 0x006c4810:return reinterpret_cast<void*>(&Align);
    case 0x00b7e380:return reinterpret_cast<void*>(&Hold);
    case 0x00a556f0:return reinterpret_cast<void*>(&NoTurret);
    case 0x00a103a0:return reinterpret_cast<void*>(&InteractionAllowed);
    case 0x00a103d0:return reinterpret_cast<void*>(&InteractionPreferred);
    case 0x00a9e160:return reinterpret_cast<void*>(&Gunship::Use);
    default:std::fprintf(stderr,"Unexpected native address: %08x\n",unsigned(va));std::exit(1);
    }
}
bool GunshipTestFocused() {return focused;}
bool GunshipTestKey(int key) {return key>=0&&key<256&&keys[key];}
int main(int argc,char** argv) {TestMotion();TestAttitude();TestEntry();TestFlightScope();TestCameraWeapons();TestPilotControls();TestBailout();if(argc>1) TestNativeHooks(argv[1]);std::printf("Gunship tests passed: %d assertions.\n",assertions);}
