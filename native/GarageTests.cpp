#include <cstdint>
void* GarageTestAddress(uintptr_t);
#define GARAGE_TEST
#include "Garage.cpp"
#include "EquipmentTuning.h"
#include "ApShopTiers.h"
#include <set>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
namespace Globals {uintptr_t ModuleBase=0x400000;}
namespace ApShop {bool allVehicles=true;std::set<int> unlocked;bool VehicleUnlocked(int id) {return id>0 && (allVehicles || unlocked.count(id));}}
namespace Logger {std::string captured;void Write(const std::string& s) {captured+=s;}}
namespace {
int assertions=0;
void Check(bool value,const char* label) {
    ++assertions;if(!value) {std::fprintf(stderr,"FAILED: %s\n",label);std::exit(1);}
}
unsigned char node[0x200],oldCar[0x4100],newCar[0x4100],vehicleInfo[0x4bc],secondInfo[0x4bc],slot[0x30],resourceData[0x80],marker[0x200];
void* lastRequested=nullptr;
unsigned char passenger[0xb00];
Player player{};
void* catalog=vehicleInfo;int catalogCount=1,slotCount=1,carCount=1;
void* slots=slot;uint16_t carIndices[2]={0,1};uint16_t* indices=carIndices;
void* carArray[2]={oldCar,newCar};void** cars=carArray;
unsigned char multiplayer=0;
DWORD resourceThread=0;int capacity=2,loads=0,releases=0;
float markerPosition[3]{};
int activity=-1;
int mode=1,resourceState=3,spawnResult=1,spawns=0,deletes=0,creates=0,uses=0;
bool room=true,requestAccepted=true,limitedPool=false;
int ambientAdmissionCalls=0;
bool managerAvailable=true;int managerRequests=0;
unsigned char manager[0x30];void* activeSlots[2]{};int evictions=0;
void __fastcall EvictSlot(void*,void*,void* entry) {
    ++evictions;managerAvailable=true;
    auto* resource=Garage::Field<void*>(entry,8);
    Garage::Field<unsigned short>(resource,0x22)|=4;
    Garage::Field<int>(manager,0x20)=0;
}
unsigned char __fastcall RequestSlot(void*,void*,void* entry,int reason) {
    ++managerRequests;
    Check(reason==16,"native manager receives garage request reason");
    if(!managerAvailable) return 0;
    Garage::Field<uint32_t>(entry,0xc)|=1u<<16;
    return 1;
}
unsigned char popupRecord[0x430];int popupRows[16],enumRows=0,actionRows=0,highlight=-1,closedMenus=0;
unsigned char inputMenu[0x150];
float acceptValue=0;int consumed=0,forwarded=0;bool topGarage=true,blocked=false;
const char* footer=nullptr;
std::unordered_map<uint32_t,void*> objects;
using Garage::Field;
void* __fastcall Find(void*,void*,uint32_t handle) {auto it=objects.find(handle);return it==objects.end()?nullptr:it->second;}
unsigned char __cdecl Request(void* resource) {
    Check(resource==resourceData || resource==resourceData+0x40,"load pins selected asset without using traffic pool");
    lastRequested=resource;
    if(limitedPool) {
        Check(loads==releases,"replacement frees previous allocation before requesting next asset");
        resourceState=3;
    }
    ++loads;return requestAccepted;
}
void __cdecl ReleaseAsset(void* resource) {
    Check(resource==resourceData || resource==resourceData+0x40,"release balances owned resource reference");++releases;
}
int __cdecl Resource(void*) {return resourceState;}
unsigned char __cdecl SpawnVehicle(Garage::Spawn* p) {
    ++spawns;
    Check(p->flags[0]==0 && p->flags[1]==0,"native garage spawn does not claim player ownership or bypass activation fade");
    Check(p->parent==(Field<int>(p->info,0xc)==2 || Garage::OutdoorVehicle(p->info)?Garage::None:Field<uint32_t>(node,0x70)),"walkers and outdoor tanks are unparented");
    // Native 00b562c0 skips ambient admission AND overlap cleanup for authored
    // parking (bit 100). Model the actual branch, not only the expected mask.
    if(!(p->spawnFlags&0x100)) {++ambientAdmissionCalls;if(!room) return 0;}
    Check(p->spawnFlags==(Garage::OutdoorVehicle(p->info)?0x147u:(0x143u | (p->parent!=Garage::None?4u:0u))),"outdoor vehicles use static authored parking");
    // Same static surface-to-model-centre conversion as 00b562c0.
    p->pos[1]-=Field<float>(p->info,0x18);
    Check(!Garage::replacement.object || objects.find(11)==objects.end(),"old vehicle fully removed before spawn");
    if(spawnResult || spawns>1) {
        Field<unsigned char>(newCar,0x54)&=~0x10;
        Field<void*>(newCar,0x3514)=p->info;
        std::memcpy(newCar+4,p->pos,12);
        std::memcpy(newCar+0x10,p->orient,36);
        p->vehicle=newCar;objects[12]=newCar;
    }
    return static_cast<unsigned char>(spawnResult || spawns>1);
}
int __fastcall Delete(void*,void*,void* object,char) {
    Check(object==oldCar || object==marker || object==newCar || object==passenger,"known native object deletion forwarded");
    Field<unsigned char>(object,0x54)|=0x10;++deletes;return 1;
}
uint32_t __cdecl Hash(const char* s) {uint32_t h=0;while(*s) h=h*33+*s++;return h;}
int __cdecl State() {return mode;}
unsigned char __cdecl False() {return 0;}
int __cdecl MinusOne() {return activity;}
int __cdecl Use(void*,char activate) {++uses;Check(!activate,"custom marker queries native eligibility without opening shop");return 1;}
int __cdecl Popup(int,const char*,const char*,void*,int,char,const char*,const char*,int) {
    std::memset(popupRecord,0,sizeof(popupRecord));Field<int*>(popupRecord,0xc)=popupRows;
    Field<int>(popupRecord,4)=42;Field<int>(popupRecord,0x10)=16;enumRows=actionRows=0;return 99;
}
int __cdecl AddRow(int,const char*,int,int) {++actionRows;return Field<int>(popupRecord,0x14)++;}
int __cdecl List(int) {return 42;}
void* __cdecl Record(int) {return popupRecord;}
int __cdecl AddEnum(int list,const char*,const char* const* labels,int count,void* callback,int value,char enabled) {
    Check(list==42 && labels && count>0 && value>=0 && value<count && enabled==1,"native enum contract");
    Check(callback==reinterpret_cast<void*>(&Garage::VariantChanged),"native left/right callback registered");
    Check(Field<int>(popupRecord,0x14)<15,"enum count respects native row limit");
    ++enumRows;return 100+enumRows;
}
void __cdecl Footer(int,const char* text,int action) {Check(action==0x49,"native primary action legend");footer=text;}
void __cdecl Highlight(int,int row) {highlight=row;}
unsigned char __cdecl Close(int,int) {Check(!Garage::families.empty(),"native screen dismissed before rebuilding catalogue");++closedMenus;return 1;}
void __cdecl StateChange(int,char) {}
void* __cdecl TopPopup() {return topGarage?popupRecord:nullptr;}
unsigned char __cdecl InputBlocked() {return blocked;}
float __cdecl Action(int action,int group) {Check(action==10 && group==123,"uses native Accept and menu input group");return acceptValue;}
void __cdecl Consume() {++consumed;acceptValue=0;}
void __fastcall ForwardInput(void*,void*) {++forwarded;}
void NativeAccept(int row) {
    Field<int>(inputMenu,0x1c)=row;acceptValue=1;
    Garage::MenuInput(inputMenu,nullptr);
}
void __fastcall ClearHouseFlag(void* object,void*,unsigned char bit) {Field<uint32_t>(object,0xa0)&=~(1u<<bit);}
void* __fastcall Props(void*,void*,const float* position,const float*,const char*) {
    std::memcpy(markerPosition,position,sizeof(markerPosition));return nullptr;
}
void __fastcall DestroyProps(void*,void*) {}
void* __fastcall Create(void*,void*,uint32_t cls,void*,void*,uint32_t) {
    Check(cls==0x9526dd04,"native upgrade-node interaction class");++creates;objects[88]=marker;return marker;
}
void Confirm() {
    using namespace Garage;
    Check(arrival.handle!=None && selected>=0,"creation alone is not success");
    SpawnSelected(&player);arrival.activeSince=GetTickCount64()-1600;SpawnSelected(&player);
}
void Reset() {
    using namespace Garage;
    CancelRequest();pins.clear();ClearOwnedVehicles();objects.clear();std::memset(node,0,sizeof(node));std::memset(oldCar,0,sizeof(oldCar));
    std::memset(passenger,0,sizeof(passenger));
    std::memset(newCar,0,sizeof(newCar));std::memset(slot,0,sizeof(slot));std::memset(vehicleInfo,0,sizeof(vehicleInfo));
    std::memset(secondInfo,0,sizeof(secondInfo));std::memset(resourceData,0,sizeof(resourceData));
    for(auto& bay:bays) {bay.marker=None;bay.car=None;}
    bayIndex=0;popup=-1;rowCount=0;selected=0;nextPage=false;message.clear();category=-1;
    std::fill(std::begin(rememberedRow),std::end(rememberedRow),0);
    choices={{vehicleInfo,"AP_TEST",L"Test vehicle"}};requestedAt=GetTickCount64();
    families={{"EDF APC","AP_TEST",2,0,{0},{choices[0].token.c_str()}}};
    objects[bays[0].node]=node;objects[11]=oldCar;
    Field<uint32_t>(node,0x6c)=bays[0].node;
    Field<uint32_t>(oldCar,0x6c)=11;Field<uint32_t>(newCar,0x6c)=12;
    Field<void*>(oldCar,0x3514)=vehicleInfo;Field<void*>(slot,8)=resourceData;
    Field<void*>(newCar,0x3514)=vehicleInfo;Field<void*>(vehicleInfo,0x34)=resourceData;
    Field<uint32_t>(oldCar,0x4014)=bays[0].node;Field<unsigned char>(oldCar,0x7e)=3;
    Field<unsigned char>(newCar,0x7e)=3;
    Field<unsigned char>(newCar,0x55)=0x10;Field<unsigned char>(newCar,0x350c)=1;Field<uint32_t>(newCar,0x4c)=3;
    Field<uint32_t>(node,0x70)=12345;Field<const char*>(vehicleInfo,0)="EDF_APC-A_1";
    Field<const char*>(vehicleInfo,8)="test.carx";Field<float>(vehicleInfo,0x18)=-1.0f;
    Field<unsigned char>(marker,0x7e)=0x33;Field<uint32_t>(marker,0x6c)=88;
    player={};room=true;ambientAdmissionCalls=0;requestAccepted=true;resourceState=3;spawnResult=1;
    spawns=deletes=creates=uses=0;mode=1;multiplayer=0;carCount=1;activity=-1;
    originalUse=Use;originalSetState=StateChange;originalDelete=reinterpret_cast<DeleteFn>(Delete);
    loads=releases=0;resourceThread=GetCurrentThreadId();capacity=2;limitedPool=false;slotCount=1;lastRequested=nullptr;
    std::memset(inputMenu,0,sizeof(inputMenu));Field<int>(inputMenu,0x12c)=1;
    Field<int>(inputMenu,0x10)=42;Field<int>(inputMenu,0x18)=123;
    originalMenuInput=reinterpret_cast<MenuInputFn>(ForwardInput);
    acceptValue=0;consumed=forwarded=0;topGarage=true;blocked=false;
    closedMenus=enumRows=0;highlight=-1;footer=nullptr;
    Logger::captured.clear();
    managerAvailable=true;managerRequests=0;
    std::memset(manager,0,sizeof(manager));evictions=0;
    Field<void**>(manager,0x18)=activeSlots;Field<int>(manager,0x1c)=7;
}
}
void* GarageTestAddress(uintptr_t address) {
    switch(address) {
#define FN(a,f) case a:return reinterpret_cast<void*>(&f)
#define DATA(a,f) case a:return &f
    FN(0x0093c050,Find);FN(0x004aef40,Request);FN(0x004b02a0,ReleaseAsset);FN(0x004ac250,Resource);FN(0x00b56420,SpawnVehicle);
    FN(0x00b524c0,RequestSlot);
    FN(0x00b523e0,EvictSlot);
    FN(0x0091d070,Delete);FN(0x00bef520,Hash);FN(0x007bfcf0,State);
    FN(0x00aabad0,ClearHouseFlag);
    FN(0x00760440,False);FN(0x007c1690,False);FN(0x007cc8f0,MinusOne);
    FN(0x008f6c70,Popup);FN(0x008f06e0,AddRow);FN(0x008cc820,List);
    FN(0x008cc2e0,TopPopup);FN(0x0094a7d0,InputBlocked);FN(0x008c7c80,Action);FN(0x0089bd60,Consume);
    FN(0x008cc350,Record);FN(0x008b9260,AddEnum);FN(0x008cbfe0,Footer);FN(0x008b9490,Highlight);FN(0x008dce30,Close);
    FN(0x00a38b00,Props);FN(0x00921950,DestroyProps);FN(0x0093e6b0,Create);
    DATA(0x01ac3b4c,resourceThread);DATA(0x02f984f8,capacity);DATA(0x02feb588,multiplayer);DATA(0x016ba5fc,catalog);DATA(0x016ba604,catalogCount);
    DATA(0x016b8ae0,slotCount);DATA(0x016b8ad8,slots);DATA(0x02f9853c,carCount);
    DATA(0x02f98534,indices);DATA(0x02f984f4,cars);
    case 0x016b8ad0:return manager;
    case 0x02f98490:case 0x01336388:return node;
    default:std::fprintf(stderr,"Unexpected native address %08x\n",static_cast<unsigned>(address));std::exit(1);
    }
}
int main() {
    using namespace Garage;
    Reset();SpawnSelected(&player);
    Check(!spawns && deletes==1 && selected==0,"replacement waits for deferred deletion");
    SpawnSelected(&player);Check(!spawns && deletes==1,"does not duplicate deletion or spawn into old car");
    objects.erase(11);carCount=0;SpawnSelected(&player);Confirm();
    Check(spawns==1 && bays[0].car==12 && selected<0,"successful replacement after native deletion");
    Check(Field<float>(node,0xa0)==0 && Field<float>(node,0xa4)==0,"ambient duplicate disabled");
    Check(loads==2 && releases==1 && pins.size()==1,"replacement retains live vehicle asset and releases backup pin");
    SweepPins();Check(releases==1,"live vehicle resource is retained");
    objects.erase(12);SweepPins();Check(releases==2 && pins.empty(),"asset released after vehicle despawns");
    Reset();resourceState=1;SpawnSelected(&player);SpawnSelected(&player);
    Check(!spawns && !deletes && selected==0 && loads==1,"asynchronous load requested once; parked car retained");
    resourceState=3;SpawnSelected(&player);Check(deletes==1,"completed asynchronous load begins replacement");
    Reset();requestAccepted=false;SpawnSelected(&player);Check(!spawns && !deletes && selected<0 && !message.empty(),"asset refusal restores menu availability");
    Reset();resourceState=4;SpawnSelected(&player);Check(!deletes && selected<0 && releases==0 && !(Field<uint32_t>(slot,0xc)&(1u<<16)),"asset error releases request without acquiring a failed resource");
    Reset();spawnResult=0;SpawnSelected(&player);objects.erase(11);carCount=0;SpawnSelected(&player);Confirm();
    Check(spawns==2 && bays[0].car==12 && selected<0 && message.find("restored")!=std::string::npos,"failed replacement restores original vehicle type");
    Check(Field<float>(newCar,8)==Field<float>(oldCar,8),"failed outdoor replacement restores the original model height without adding its ground offset twice");
    Reset();resourceState=1;SpawnSelected(&player);requestedAt=GetTickCount64()-20001;SpawnSelected(&player);
    Check(!spawns && !deletes && selected==-1 && releases==1,"loading timeout cancels and retains parked vehicle");
    Reset();SpawnSelected(&player);replacement.since=GetTickCount64()-5001;SpawnSelected(&player);
    Check(selected<0 && !message.empty() && pins.size()==1,"stalled deletion reports failure while retaining asset until cleanup");
    objects.erase(11);SweepPins();Check(pins.empty(),"deferred deletion pin eventually released");
    Reset();Field<float>(&player,4)=100;SpawnSelected(&player);Check(!spawns && selected==-1,"leaving garage cancels request");
    Reset();Field<uint32_t>(oldCar,0x3f24)=1u<<29;SpawnSelected(&player);Check(!deletes && selected<0,"occupied car never removed");
    Reset();Field<uint32_t>(oldCar,0x4014)=123;SpawnSelected(&player);Check(!deletes && spawns==1,"unrelated nearby car never removed; no ambient clearance query");
    Reset();Field<float>(oldCar,4)=50;SpawnSelected(&player);Check(!deletes && spawns==1,"driven-away garage car never removed");
    Reset();Field<uint32_t>(oldCar,0x3f24)=1u<<24;SpawnSelected(&player);Check(!deletes,"mission-protected vehicle never removed");
    Reset();resourceThread=0;SpawnSelected(&player);Check(!loads && !deletes && selected<0,"wrong resource thread cannot silently stall or touch assets");
    Reset();capacity=0;SpawnSelected(&player);Check(!spawns && !deletes && selected<0,"object index capacity checked");
    Reset();selected=-1;CreateMarkers(&player);CreateMarkers(&player);Check(creates==1,"marker reconciliation does not duplicate circles");
    Check(Field<unsigned char>(marker,0x54)&0x20,"garage interaction is not saved into campaign");
    Check(std::memcmp(markerPosition,bays[0].position,sizeof(markerPosition))==0,"marker uses authored foot position, not parking right-vector offset");
    Reset();selected=-1;choices.clear();BuildCatalog();Check(choices.size()==1,"runtime catalog loads available vehicle");
    const wchar_t* label=nullptr;Check(LookupText(Hash(choices[0].token.c_str()),&label) && label && *label,"native menu localization resolves");
    Check(!LookupText(0xaaaaaaaa,&label),"other native localization is untouched");
    Reset();selected=-1;popup=99;rowCount=1;rows[0]=-12;Select(99,0,0);Check(nextPage && category==2 && selected<0,"category selection queues native browser");
    Reset();selected=-1;popup=99;rowCount=1;rows[0]=0;Select(99,0,1);Check(selected<0 && !nextPage,"cancel never spawns");
    Reset();selected=-1;popup=99;rowCount=1;rows[0]=0;Select(99,0,0);Check(selected==0 && !spawns,"selection queues work after popup closes");
    Reset();selected=-1;bays[0].marker=88;Field<uint32_t>(&player,0xce4)=88;
    Check(Garage::Use(&player,1)==1 && popup==99 && uses==1,"native E interaction opens garage instead of shop");
    Reset();CreateMarkers(&player);resourceState=1;SpawnSelected(&player);SetState(2,0);
    Check(selected<0 && bays[0].marker==None && choices.empty() && !heldResource,"campaign load cancels pending request and clears transient marker");
    Reset();resourceState=1;SpawnSelected(&player);bays[0].marker=88;Field<uint32_t>(&player,0xce4)=88;
    Check(Garage::Use(&player,1)==1 && popup==99 && rows[0]==-4,"pending request can reopen menu and offer cancellation");
    Select(99,0,0);Check(selected<0 && !heldResource && releases==1 && nextPage,"cancel releases request and returns to vehicle choices");
    Reset();resourceState=1;SpawnSelected(&player);popup=99;rowCount=1;rows[0]=-4;
    Select(99,0,1);Check(selected>=0 && heldResource,"back closes loading menu without losing active request");
    Reset();resourceState=1;SpawnSelected(&player);activity=4;Garage::Frame(&player);
    Check(selected<0 && !heldResource && !deletes,"starting activity cancels loading before any vehicle mutation");
    Reset();carCount=0;Garage::Frame(&player);Confirm();
    Check(selected<0 && pins.size()==1 && spawns==1,"frame dispatcher completes an empty-bay request");
    bays[0].marker=88;Field<uint32_t>(&player,0xce4)=88;
    Check(Garage::Use(&player,1)==1 && popup==99 && rows[0]==-12 && category==-1,"menu usable again at owned category overview after successful spawn");
    Reset();resourceState=1;SpawnSelected(&player);resourceThread=0;CancelRequest();
    Check(releases==0 && pins.size()==2,"UI cancellation defers resource and manager releases outside owner thread");
    resourceThread=GetCurrentThreadId();SweepPins();Check(releases==1 && pins.empty(),"deferred resource release runs on owner thread");
    Reset();SpawnSelected(&player);objects.erase(11);carCount=0;SpawnSelected(&player);
    objects.erase(12);SweepPins();SpawnSelected(&player);
    Check(spawns==2 && arrival.restoring && replacement.resource==nullptr,"vehicle lost on first update triggers original-car recovery");
    Confirm();Check(selected<0 && message.find("restored")!=std::string::npos,"recovery confirmed only after activation");
    Reset();carCount=0;SpawnSelected(&player);objects.erase(12);SweepPins();SpawnSelected(&player);
    Check(selected<0 && !message.empty(),"empty-bay activation loss is reported instead of silently claiming success");
    Reset();carCount=0;SpawnSelected(&player);Field<unsigned char>(newCar,0x350c)=0;
    arrival.since=GetTickCount64()-8001;SpawnSelected(&player);
    Check(selected<0 && !message.empty(),"activation timeout releases menu without waiting forever");
    Reset();carCount=0;SpawnSelected(&player);ObserveDelete(node,nullptr,newCar,0);
    Check(deletes==1 && (Field<unsigned char>(newCar,0x54)&0x10),"hook forwards ordinary native cleanup");
    // Reproduce the observed resource-manager eviction, including its own
    // reference release and the following occupant cleanup pass.
    Reset();carCount=0;SpawnSelected(&player);
    Request(resourceData); // Ambient manager also owns a reference to this asset.
    Check(DeleteFrom(node,newCar,0,0x00b51b88)==0 && deletes==0,"ambient eviction preserves independently pinned garage car");
    Check(DeleteFrom(node,oldCar,0,0x00b51b88)==1 && deletes==1,"same-asset ambient car is still deleted");
    Field<uint32_t>(passenger,0xac4)=12;
    Check(DeleteFrom(node,passenger,0,0x00b51c08)==0 && deletes==1,"ambient eviction preserves occupants of retained garage vehicle");
    ReleaseAsset(resourceData); // Slot eviction finishes normally.
    Check(loads-releases==1 && pins.size()==1,"slot eviction releases only manager reference, retaining garage asset");
    Confirm();Check(selected<0 && message.empty() && spawns==1,"eviction no longer causes rollback or false failure");
    Check(PreserveForEviction(newCar),"protection lasts beyond activation confirmation");
    Field<float>(newCar,4)=500;Check(PreserveForEviction(newCar),"driving away retains independent resource ownership");
    Check(DeleteFrom(node,passenger,0,0x0093d8df)==1,"ordinary occupant cleanup is not suppressed");
    Check(DeleteFrom(node,newCar,0,0x0093d8df)==1 && ownedVehicles.empty(),"world stream-out deletes owned car and revokes protection");
    objects.erase(12);SweepPins();Check(loads==releases && pins.empty(),"world cleanup releases last asset reference exactly once");
    Reset();carCount=0;SpawnSelected(&player);Confirm();
    DeleteFrom(node,newCar,0,0x00401234);
    Check(deletes==1 && !PreserveForEviction(newCar),"explicit replacement and other deletion callers remain effective");
    Reset();carCount=0;SpawnSelected(&player);Confirm();SetState(2,0);
    Check(DeleteFrom(node,newCar,0,0x00b51b88)==1,"resource eviction also deletes garage cars during a load transition");
    objects.erase(12);SweepPins();Check(pins.empty() && loads==releases,"load transition does not leak garage resource");
    Reset();carCount=0;SpawnSelected(&player);
    Field<uint32_t>(oldCar,0x6c)=12;
    Check(DeleteFrom(node,oldCar,0,0x00b51b88)==1,"matching handle alone does not protect another object");
    Field<uint32_t>(newCar,0x6c)=13;
    Check(DeleteFrom(node,newCar,0,0x00b51b88)==1,"matching pointer alone does not protect a recycled object");
    Reset();carCount=0;SpawnSelected(&player);Field<void*>(vehicleInfo,0x34)=nullptr;
    Check(DeleteFrom(node,newCar,0,0x00b51b88)==1,"protection requires the exact pinned resource");
    Reset();carCount=0;SpawnSelected(&player);objects.erase(12);SweepPins();
    Check(ownedVehicles.empty() && releases==1,"disappearing object loses protection before asset release");
    Reset();carCount=0;SpawnSelected(&player);Field<uint32_t>(passenger,0xac4)=11;
    Check(DeleteFrom(node,passenger,0,0x00b51c08)==1,"occupants of an ordinary traffic vehicle are not protected");
    Field<uint32_t>(passenger,0xac4)=None;
    Check(DeleteFrom(node,passenger,0,0x00b51c08)==1,"unattached human cleanup is unchanged");
    Reset();selected=-1;
    Check(!CapturePosition(&player),"production disables placement capture");
    UnlockPlacementTravel(&player);
    Check(player.Metadata.upgrades[29].current_level==0,"production does not grant free fast travel");
    // Repeat requests in the same live garage, with only one free asset slot.
    Reset();carCount=0;limitedPool=true;SpawnSelected(&player);Confirm();
    slotCount=2;Field<void*>(slot,0x20)=resourceData+0x40;
    Field<int>(secondInfo,0x30)=1;Field<void*>(secondInfo,0x34)=resourceData+0x40;
    Field<const char*>(secondInfo,0)="SecondCar";
    choices.push_back({secondInfo,"AP_SECOND",L"Second vehicle"});
    for(int repeat=0;repeat<3;++repeat) {
        Field<float>(newCar,4)=repeat?500.0f:0.0f;
        const int nextChoice=repeat%2==0?1:0;
        selected=nextChoice;requestedAt=GetTickCount64();const int before=loads;
        SpawnSelected(&player);
        Check(retirement.handle==12 && !heldResource && loads==before,"second request retires old selection before loading, even if driven away");
        SpawnSelected(&player);Check(loads==before,"waits for deferred car destruction");
        objects.erase(12);resourceState=2;SpawnSelected(&player);
        Check(loads==releases && loads==before && pins.empty(),"waits for native resource unload after releasing reference");
        auto* retiringAsset=retirement.resource;
        resourceState=0;Field<unsigned short>(retiringAsset,0x22)=4;SpawnSelected(&player);
        Check(loads==before,"queued unload is not yet a free allocation");
        Field<unsigned short>(retiringAsset,0x22)=0;SpawnSelected(&player);Confirm();
        Check(spawns==repeat+2 && pins.size()==1 && loads-releases==1 && ownedVehicles.size()==1,"successive replacement retains exactly one vehicle and asset");
        Check(Field<void*>(newCar,0x3514)==choices[nextChoice].info && lastRequested==ResourceFor(choices[nextChoice].info),"successive replacements alternate between distinct models and native resources");
    }
    selected=0;Field<uint32_t>(newCar,0x3f24)=1u<<29;const int before=deletes;
    SpawnSelected(&player);Check(deletes==before && pins.size()==1 && selected<0,"occupied previous selection is never deleted");
    Reset();carCount=0;SpawnSelected(&player);Confirm();selected=0;SpawnSelected(&player);
    objects.erase(12);resourceState=4;SpawnSelected(&player);
    Check(recovering && selected==0 && !heldResource,"replacement load failure queues previous selection recovery once");
    resourceState=3;SpawnSelected(&player);Confirm();
    Check(spawns==2 && message.find("restored")!=std::string::npos && loads-releases==1,"recovery produces one stable vehicle and usable menu");
    Reset();carCount=0;SpawnSelected(&player);Confirm();selected=0;SpawnSelected(&player);SetState(2,0);
    objects.erase(12);SweepPins();Check(pins.empty() && loads==releases,"load transition during retirement releases all references");
    // Exercise the actual catalogue and every native family/variant mapping.
    Reset();selected=-1;choices.clear();families.clear();
    std::vector<unsigned char> allInfo(std::size(GarageCatalog::Entries)*0x4bc);
    std::map<std::string,int> nativeIds;
    for(size_t i=0;i<std::size(GarageCatalog::Entries);++i) {
        auto* info=allInfo.data()+i*0x4bc;
        Field<const char*>(info,0)=GarageCatalog::Entries[i].id;Field<const char*>(info,8)="test.carx";
        Check(++nativeIds[GarageCatalog::Entries[i].id]==1,"catalogue native IDs are unique");
    }
    catalog=allInfo.data();catalogCount=static_cast<int>(std::size(GarageCatalog::Entries));BuildCatalog();
    const size_t allowedCount=std::count_if(std::begin(GarageCatalog::Entries),std::end(GarageCatalog::Entries),
        [](const GarageCatalog::Entry& e){return e.category!=4;});
    Check(choices.size()==allowedCount,"only supported campaign vehicles retained");
    size_t covered=0;
    for(const auto& e:GarageCatalog::Entries) {
        Check(std::strlen(e.family)<=16 && std::strlen(e.variant)<=13,"compact labels fit the stock columns without rendering mutations");
    }
    for(int group=0;group<6;++group) {
        if(group==4) continue;
        category=group;selected=-1;OpenMenu();
        Check(enumRows>0 && enumRows<=7 && rowCount==enumRows && actionRows==0,"vehicle browser contains no action row that would swallow Enter");
        Check(footer && std::strcmp(footer,"AP_GARAGE_SPAWN")==0 && highlight==0,"native spawn footer and initial focus");
        for(int row=0;row<rowCount;++row) {
            auto& family=families[rows[row]];
            Check(family.category==group && family.labels.size()==family.choices.size(),"family belongs to correct category");
            for(int variant=0;variant<static_cast<int>(family.choices.size());++variant) {
                ++covered;VariantChanged(row,variant);
                Check(std::strcmp(family.labels[variant],choices[family.choices[variant]].token.c_str())==0,"native variant token pointer remains valid");
                popup=99;NativeAccept(row);
                Check(selected==family.choices[variant] && rememberedRow[group]==row,"variant selection dispatches exact native definition");
                selected=-1;
            }
            const int value=family.variant;VariantChanged(row,9999);Check(family.variant==value,"invalid variant ignored");
        }
        OpenMenu();Check(highlight==rowCount-1,"browser remembers last family row");
        popup=99;Select(99,0,1);Check(category==-1 && nextPage && selected<0,"Escape returns to categories without spawning");
    }
    Check(covered==allowedCount,"every supported variant is reachable");
    ApShop::allVehicles=false;ApShop::unlocked.clear();selected=-1;category=-1;OpenMenu();
    Check(rowCount==1 && rows[0]==-1,"empty garage shows no locked vehicles");
    ApShop::unlocked.insert(ApVehicles::Find("Combat walker"));category=-1;OpenMenu();
    Check(rowCount==1 && rows[0]==-13,"only unlocked walker category is shown");
    Select(popup,0,0);OpenMenu();
    Check(rowCount==1 && families[rows[0]].name=="Combat walker","only received vehicle family appears");
    const int combatFamily=rows[0];ApShop::unlocked.clear();Select(popup,0,0);
    Check(selected<0,"stale menu cannot spawn an unowned vehicle");
    ApShop::allVehicles=true;selected=-1;popup=-1;category=3;bayIndex=0;
    bays[0].marker=88;Field<uint32_t>(&player,0xce4)=88;
    Garage::Use(&player,1);
    Check(category==-1 && rowCount==5,"fresh interaction opens main categories, never previous walker page");
    (void)combatFamily;
    category=0;OpenMenu();
    const auto& retained=RetainedVariants(families[rows[0]]);const std::string oldLabel=retained[0];
    const auto retainedCount=menuVariants.size();OpenMenu();
    Check(menuVariants.size()==retainedCount,"reopening menu reuses immutable variant labels");
    const int previousCloses=closedMenus;SetState(2,0);Check(closedMenus==previousCloses+1 && families.empty(),"load dismisses native browser and resets catalogue");
    Check(oldLabel==retained[0],"native fade can safely access labels after catalogue destruction");
    catalog=vehicleInfo;catalogCount=1;
    Reset();selected=-1;category=2;OpenMenu();
    MenuInput(inputMenu,nullptr);Check(selected<0 && forwarded==1,"navigation without Accept stays native");
    topGarage=false;NativeAccept(0);Check(selected<0 && consumed==0,"overlay prevents garage Accept");topGarage=true;
    blocked=true;NativeAccept(0);Check(selected<0,"blocked input cannot spawn");blocked=false;
    Field<unsigned char>(inputMenu,7)=1;NativeAccept(0);Check(selected<0,"opening animation cannot spawn");Field<unsigned char>(inputMenu,7)=0;
    Field<int>(inputMenu,0x10)=43;NativeAccept(0);Check(selected<0,"other menu cannot spawn");Field<int>(inputMenu,0x10)=42;
    NativeAccept(-1);Check(selected<0,"invalid focus cannot spawn");
    NativeAccept(0);Check(selected==0 && consumed==1 && closedMenus==1,"Accept queues selected vehicle and closes native popup once");
    MenuInput(inputMenu,nullptr);Check(consumed==1,"dismissed popup cannot repeat Accept");
    carCount=0;SpawnSelected(&player);Confirm();
    Check(spawns==1 && selected<0,"native Accept dispatches through to an active vehicle");
    Reset();Field<int>(vehicleInfo,0xc)=2;Field<const char*>(vehicleInfo,0)="Walker_Civilian";
    auto walkerSpawn=ParkingSpawn(node,vehicleInfo);
    Check(walkerSpawn.parent==None && (walkerSpawn.spawnFlags&4),"walker avoids native parented-cargo entry rejection");
    Check(walkerSpawn.pos[0]==GarageOutdoor::Find(bays[0].node)->position[0] && walkerSpawn.flags[0]==0 && walkerSpawn.flags[1]==0,"walker uses recorded outdoor location and ordinary vehicle flags");
    carCount=0;SpawnSelected(&player);Confirm();
    Check(spawns==1 && selected<0 && pins.size()==1,"unparented walker completes ordinary spawn lifecycle");
    Check(EquipmentTuning::Ammo(32,1)==40 && EquipmentTuning::Ammo(32,5)==72,"ammo grows linearly from normal maximum");
    Check(EquipmentTuning::Ammo(12,5)==27 && EquipmentTuning::Ammo(12,0)==12,"remote reserve extensions do not need placed-charge state");
    Check(EquipmentTuning::Ammo(0x7fffffffu,5)==0x7fffffffu,"native unlimited limit preserved");
    Check(EquipmentTuning::Ammo(65535,5)==65535,"capacity remains representable");
    EquipmentTuning::PackInfo base{},out{},again{};
    EquipmentTuning::Write(base,0x6f4,0.25f);EquipmentTuning::Write(base,0x6f8,0.12f);
    EquipmentTuning::Write(base,0x154,1.45f);EquipmentTuning::Write(base,0x15c,1.5f);
    EquipmentTuning::Write(base,0x160,1.5f);EquipmentTuning::Write(base,0x1e0,100);
    EquipmentTuning::Write(base,0x1d0,5.0f);EquipmentTuning::Write(base,0x1e8,10.0f);
    const auto original=base;
    for(int type=0;type<10;++type) {
        EquipmentTuning::Backpack(base,out,type,0,0);Check(out==base,"zero upgrades preserve complete native backpack definition");
        EquipmentTuning::Backpack(base,out,type,5,5);EquipmentTuning::Backpack(base,again,type,5,5);
        Check(out==again && base==original,"backpack updates never compound or mutate global defaults");
        Check(EquipmentTuning::Read<float>(out,0x6f8)>0.59f,"finite recharge multiplier; maximum tier uses explicit inactive refill");
        Check(EquipmentTuning::Read<float>(out,0x6f4)>0,"duration extension never creates infinite activation");
    }
    EquipmentTuning::Backpack(base,out,6,0,5);
    Check(EquipmentTuning::Read<int>(out,0x1e0)==225 && EquipmentTuning::Read<float>(out,0x1d0)==7.5f,"Concussion integer impulse and bounded radius");
    EquipmentTuning::Backpack(base,out,8,0,5);
    Check(std::abs(EquipmentTuning::Read<float>(out,0x6f4)-0.25f/2.25f)<0.0001f,"Rhino charge lasts 2.25 times normal");
    Check(ApShop::UnlockedShopTier(1)==1 && ApShop::UnlockedShopTier(3)==2 && ApShop::UnlockedShopTier(7)==3,"Parker counts as shop sector one");
    Reset();managerAvailable=false;
    SpawnSelected(&player);
    Check(!loads && !spawns && !deletes && selected>=0,"busy native budget or conflicting variant never falls back to an unsafe direct load");
    managerAvailable=true;SpawnSelected(&player);
    Check(loads>0,"request resumes when native budget becomes available");
    CancelRequest();Check(!(Field<uint32_t>(slot,0xc)&(1u<<16)),"cancellation clears only this request's manager reason");
    Reset();Field<uint32_t>(slot,0xc)=1u<<16;SpawnSelected(&player);CancelRequest();
    Check(Field<uint32_t>(slot,0xc)&(1u<<16),"preexisting engine reason survives cancellation");
    Reset();Field<unsigned short>(resourceData,0x22)=0x10;
    SpawnSelected(&player);Check(!loads && !managerRequests,"pending unload cannot acquire an unbalanced reference");
    Field<unsigned short>(resourceData,0x22)=0;SpawnSelected(&player);Check(loads>0,"unload completion permits retry");
    Reset();carCount=0;Field<const char*>(vehicleInfo,0)="EDF_HeavyTank-A_1";
    Field<float>(node,0x10)=1;Field<float>(node,0x20)=1;Field<float>(node,0x30)=1;
    auto outdoor=ParkingSpawn(node,vehicleInfo);
    Check(outdoor.parent==None && outdoor.spawnFlags==0x147,"heavy tank uses static parking, authored height and no parent");
    for(const auto& bay:bays) {
        Field<uint32_t>(node,0x6c)=bay.node;
        const auto* point=GarageOutdoor::Find(bay.node);Check(point!=nullptr,"all eight garages have authored outdoor positions");
        auto placed=ParkingSpawn(node,vehicleInfo);
        Check(std::memcmp(placed.pos,point->position,sizeof(placed.pos))==0 &&
            std::memcmp(placed.orient,point->orientation,sizeof(placed.orient))==0,"outdoor centre and facing match user capture exactly");
        std::memcpy(reinterpret_cast<char*>(&player)+4,placed.pos,12);
        Check(!OutdoorPlayerClear(placed,&player),"player at outdoor centre blocks spawn");
        Field<float>(&player,4)+=15;
        Check(OutdoorPlayerClear(placed,&player),"player well clear permits spawn without moving authored position");
    }
    player={};Field<uint32_t>(node,0x6c)=bays[0].node;
    SpawnSelected(&player);Confirm();Check(spawns==1,"heavy tank uses normal activation and resource lifetime");
    // Reproduce both reported locations with assets ready and the ambient
    // admission test refusing. Old outdoor flags 43/47 fail this scenario.
    for(int garage: {1,4}) {
        Reset();bayIndex=garage;room=false;
        auto& bay=bays[garage];objects[bay.node]=node;
        Field<uint32_t>(node,0x6c)=bay.node;
        Field<uint32_t>(oldCar,0x4014)=bay.node;
        Field<const char*>(vehicleInfo,0)="EDF_HeavyTank-A_1";
        Field<float>(vehicleInfo,0x10)=6.634f;
        std::memcpy(node+4,bay.position,12);
        std::memcpy(oldCar+4,bay.position,12);
        std::memcpy(reinterpret_cast<char*>(&player)+4,bay.position,12);
        SpawnSelected(&player);
        Check(deletes==1 && !spawns,"Parker/Dust replacement still waits for the parked car to be removed");
        objects.erase(11);carCount=0;SpawnSelected(&player);Confirm();
        Check(spawns==1 && !ambientAdmissionCalls && message.empty(),"authored tank parking succeeds despite ambient admission rejection");
        Check(bay.car==12 && pins.size()==1 && loads-releases==1,"authored spawn retains one live vehicle and resource reference");
        selected=0;SpawnSelected(&player);
        Check(retirement.handle==12 && deletes==2,"second outdoor request retires the previously spawned tank");
        objects.erase(12);SpawnSelected(&player);Confirm();
        Check(spawns==2 && !ambientAdmissionCalls && pins.size()==1,"second outdoor spawn also bypasses ambient admission without leaking resources");
        // A third request while the player stands on the authored point must
        // stop BEFORE deleting the existing tank or loading another resource.
        const auto* point=GarageOutdoor::Find(bay.node);
        std::memcpy(reinterpret_cast<char*>(&player)+4,point->position,12);
        selected=0;const int beforeLoads=loads;SpawnSelected(&player);
        Check(spawns==2 && deletes==2 && loads==beforeLoads && selected<0,"static parking never spawns on the player or removes their tank first");
    }
    for(const auto& e:GarageCatalog::Entries) {
        Field<const char*>(vehicleInfo,0)=e.id;
        if(e.category==1 || e.category==3) Check(OutdoorVehicle(vehicleInfo),"all truck, bus and walker variants use outdoor points");
        if(e.category==0) Check(!OutdoorVehicle(vehicleInfo),"ordinary cars retain indoor parking");
    }
    Reset();managerAvailable=false;carCount=0;
    auto* spare=slot+0x18;auto* spareResource=resourceData+0x40;
    Field<void*>(spare,8)=spareResource;Field<int>(spare,0x10)=3;Field<uint32_t>(spare,0x14)=8;
    Field<short>(spareResource,8)=1;activeSlots[0]=spare;Field<int>(manager,0x20)=1;
    SpawnSelected(&player);
    Check(evictions==1 && !loads && !spawns && !deletes,"full manager reclaims an unused asset without removing world vehicles");
    SpawnSelected(&player);Check(!loads,"reclaimed slot's queued unload must complete before admitting new asset");
    Field<unsigned short>(spareResource,0x22)=0;SpawnSelected(&player);Confirm();
    Check(spawns==1 && selected<0,"full-pool request finishes after safe reclamation");
    Reset();managerAvailable=false;
    Field<void*>(spare,8)=resourceData;Field<int>(spare,0x10)=3;Field<uint32_t>(spare,0x14)=8;
    Field<short>(resourceData,8)=1;activeSlots[0]=spare;Field<int>(manager,0x20)=1;
    SpawnSelected(&player);Check(!evictions && !loads && !deletes,"world car using resource prevents reclamation even when unoccupied");
    carCount=0;Field<short>(resourceData,8)=2;SpawnSelected(&player);
    Check(!evictions,"external resource references prevent reclamation");
    Field<short>(resourceData,8)=1;Field<uint32_t>(spare,0xc)=1u<<16;SpawnSelected(&player);
    Check(!evictions,"another explicit garage reservation is protected");
    Reset();managerAvailable=false;OpenMenu();loadingPopupAt=GetTickCount64()-2001;
    Frame(&player);Check(popup<0 && closedMenus==1 && selected==0,"loading menu automatically unpauses native streaming without cancelling request");
    requestedAt=GetTickCount64()-20001;Frame(&player);
    Check(selected<0 && !message.empty(),"loading-menu visit cannot suppress request timeout forever");
    Reset();OpenMenu();loadingPopupAt=GetTickCount64()-2001;
    MenuInput(inputMenu,nullptr);
    Check(popup<0 && selected==0 && closedMenus==1,"loading dialog unpauses even when player Frame is suspended");
    std::printf("Garage adapter: %d checks passed.\n",assertions);
}
