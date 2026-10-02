#include "NativeSupport.h"
#include "Garage.h"
#include "Gunship.h"
#include "GunshipPolicy.h"
#include "GaragePolicy.h"
#include "GarageCatalog.h"
#include "GarageOutdoor.h"
#include "ApVehicleCatalog.h"
#include "ApShop.h"
#include "CrashDiagnostics.h"
#include <map>
#include <cmath>
#include <intrin.h>

namespace Garage {
namespace {
template<class T> T At(uintptr_t va) {
#ifdef GARAGE_TEST
    return reinterpret_cast<T>(GarageTestAddress(va));
#else
    return reinterpret_cast<T>(Globals::ModuleBase+va-0x400000);
#endif
}
template<class T> T& Field(void* p,size_t offset) {return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
constexpr uint32_t None=GaragePolicy::None;
struct Bay {uint32_t node,stand;float position[3];uint32_t marker=None,car=None;};
// User-recorded on-foot positions, captured 2026-09-29.
Bay bays[]={
    {0x47020041,0x47020086,{-1717.807251f,40.049534f,447.019440f}},
    {0x4a0200dc,0x4a020024,{-1895.641357f,14.984375f,-1439.760986f}},
    {0x56020041,0x5603004d,{-1389.812256f,24.111620f,554.795593f}},
    {0x7904000c,0x79040187,{-407.229401f,32.715542f,-824.077881f}},
    {0x7c010041,0x7c010027,{-96.388374f,23.656670f,-2444.361572f}},
    {0xa604007e,0xa604020c,{1410.818237f,2.816776f,647.317993f}},
    {0xc80201c0,0xc8000166,{2405.909424f,43.387852f,-303.724670f}},
    {0xca010007,0xca010008,{2462.320068f,51.418667f,-1207.637329f}}};
struct PropBlock {uint16_t count,owner,position,size;void* data;};
struct Spawn {
    void* info;float pos[3];void* stream;uint16_t zone,padding;float orient[9];
    float speed;uint32_t parent,building;int priority;uint32_t flags[2],spawnFlags;void* vehicle;
};
static_assert(sizeof(PropBlock)==12);
static_assert(sizeof(Spawn)==0x5c && offsetof(Spawn,vehicle)==0x58);
struct Choice {void* info;std::string token;std::wstring label;int price=50,item=0;};
std::vector<Choice> choices;
struct Family {std::string name,token;int category=4,variant=0;std::vector<int> choices;std::vector<const char*> labels;};
std::vector<Family> families;
bool FamilyUnlocked(const Family& family) {return ApShop::VehicleUnlocked(ApVehicles::Find(family.name.c_str())) || ApShop::VehiclePurchasable(ApVehicles::Find(family.name.c_str()));}
struct MenuVariants {std::vector<std::string> tokens;std::vector<const char*> labels;};
std::map<std::string,MenuVariants> menuVariants;
const std::vector<const char*>& RetainedVariants(const Family& family) {
    std::string key;
    for(const auto* label:family.labels) {key+=label;key+='|';}
    auto [it,inserted]=menuVariants.try_emplace(key);
    if(inserted) {
        for(const auto* label:family.labels) it->second.tokens.emplace_back(label);
        for(const auto& token:it->second.tokens) it->second.labels.push_back(token.c_str());
    }
    // Native closing animations can outlive a campaign load or catalog rebuild.
    // Reuse immutable arrays by content; repeated visits do not allocate more.
    return it->second.labels;
}
int category=-1;
int rememberedRow[6]{};
const char* categoryTokens[]={"AP_GARAGE_CARS","AP_GARAGE_TRUCKS","AP_GARAGE_EDF","AP_GARAGE_WALKERS","AP_GARAGE_EXPERIMENTAL","AP_GARAGE_UTILITY"};
std::map<uint32_t,std::wstring> text;
using UseFn=int(__cdecl*)(void*,char);
UseFn originalUse=nullptr;
using StateFn=void(__cdecl*)(int,char);
StateFn originalSetState=nullptr;
using MenuInputFn=void(__thiscall*)(void*);
MenuInputFn originalMenuInput=nullptr;
using DeleteFn=int(__thiscall*)(void*,void*,char);
DeleteFn originalDelete=nullptr;
int popup=-1,bayIndex=-1,rowCount=0,rows[16]{},selected=-1;
bool nextPage=false;
uint64_t nextScan=0,requestedAt=0;
uint64_t loadingPopupAt=0;
void* unloadingSlotResource=nullptr;
void* heldResource=nullptr;
void* requestedSlot=nullptr;
bool ownsSlotReason=false;
struct Pin {void* resource;uint32_t handle;void* object;void* slot=nullptr;bool ownsReason=false;};
std::vector<Pin> pins;
// Deletion callbacks can run outside the frame update. Do not expose pins to them.
struct OwnedVehicle {void* object;void* resource;uint32_t handle;bool evictionLogged=false;};
std::mutex ownedMutex;
std::vector<OwnedVehicle> ownedVehicles;
void ForgetVehicle(void* object,uint32_t handle) {
    std::lock_guard<std::mutex> lock(ownedMutex);
    ownedVehicles.erase(std::remove_if(ownedVehicles.begin(),ownedVehicles.end(),
        [=](const OwnedVehicle& car){return car.object==object && car.handle==handle;}),ownedVehicles.end());
}
void ClearOwnedVehicles() {
    std::lock_guard<std::mutex> lock(ownedMutex);
    ownedVehicles.clear();
}
bool PreserveForEviction(void* object,bool logEviction=true) {
    if(!object || Field<unsigned char>(object,0x7e)!=3 || (Field<unsigned char>(object,0x54)&0x10)) return false;
    auto* info=Field<void*>(object,0x3514);
    if(!info) return false;
    const auto handle=Field<uint32_t>(object,0x6c);
    bool found=false,log=false;
    {
        std::lock_guard<std::mutex> lock(ownedMutex);
        for(auto& car:ownedVehicles) {
            if(car.object!=object || car.handle!=handle || !car.resource || car.resource!=Field<void*>(info,0x34)) continue;
            found=true;log=logEviction && !car.evictionLogged;
            if(logEviction) car.evictionLogged=true;
            break;
        }
    }
    if(log) Logger::Log("Garage: retained vehicle {:08x} during traffic resource eviction; garage asset reference is still held.\n",handle);
    return found;
}
struct Replacement {
    uint32_t handle=None;void* object=nullptr;void* resource=nullptr;
    Spawn backup{};float day=0,night=0;uint64_t since=0;
} replacement;
struct Arrival {uint32_t handle=None;void* object=nullptr;uint64_t since=0,activeSince=0;bool restoring=false;} arrival;
struct Retirement {uint32_t handle=None;void* object=nullptr;void* resource=nullptr;uint64_t since=0;} retirement;
bool retiredCar=false,recovering=false;
int restoreChoice=-1;
std::atomic<void*> creatingInfo{nullptr};
std::atomic<uint32_t> traceHandles[8]{};
std::string message;

void* Object(uint32_t handle) {
    if(handle==None) return nullptr;
    return At<void*(__thiscall*)(void*,uint32_t)>(0x0093c050)(At<void*>(0x02f98490),handle);
}
float Distance(void* a,void* b) {
    float result=0;
    for(int i=0;i<3;++i) {float d=Field<float>(a,4+i*4)-Field<float>(b,4+i*4);result+=d*d;}
    return result;
}
void Token(const std::string& key,const std::wstring& value) {
    const auto hash=At<uint32_t(__cdecl*)(const char*)>(0x00bef520)(key.c_str());
    text.emplace(hash,value); // Engine layouts may retain these pointers.
}
bool Campaign() {
    return !*At<unsigned char*>(0x02feb588) &&
        !At<unsigned char(__cdecl*)()>(0x00760440)() &&
        At<int(__cdecl*)()>(0x007bfcf0)()==1;
}
void UnlockPlacementTravel(Player* player) {
    if(!PlacementTravelUnlocked || !player || !Campaign()) return;
    // Local test grant; not a shop purchase or Archipelago receipt/check.
    player->Metadata.upgrades[29].current_level=1;
    player->Metadata.upgrades[29].availability_bitfield|=2;
    static constexpr uint32_t houses[]={1191313536u,1241645101u,1442971656u,2030305290u,
        2080440328u,2785280009u,3355574278u,3389063173u};
    for(auto handle:houses) {
        auto* node=Object(handle);
        if(!node || Field<unsigned char>(node,0x7e)!=0x30) continue;
        Field<unsigned char>(node,0x9c)=1;
        for(unsigned char bit:{0,1,30})
            if(Field<uint32_t>(node,0xa0)&(1u<<bit))
                At<void(__thiscall*)(void*,unsigned char)>(0x00aabad0)(node,bit);
    }
}
bool ResourceThread() {return GetCurrentThreadId()==*At<DWORD*>(0x01ac3b4c);}
void ReleaseSlot(void* slot,bool owned) {
    if(slot && owned) Field<uint32_t>(slot,0xc)&=~(1u<<16);
}
void ReleaseRequestSlot() {
    if(requestedSlot && ownsSlotReason) {
        if(ResourceThread()) ReleaseSlot(requestedSlot,true);
        else pins.push_back({nullptr,None,nullptr,requestedSlot,true});
    }
    requestedSlot=nullptr;ownsSlotReason=false;
}
void Release(void*& resource) {
    if(!resource) return;
    if(ResourceThread()) At<void(__cdecl*)(void*)>(0x004b02a0)(resource);
    else pins.push_back({resource,None,nullptr});
    resource=nullptr;
}
void SweepPins() {
    if(!ResourceThread()) return;
    for(auto it=pins.begin();it!=pins.end();) {
        if(it->object && Object(it->handle)==it->object) {++it;continue;}
        ForgetVehicle(it->object,it->handle);
        ReleaseSlot(it->slot,it->ownsReason);
        if(it->resource) At<void(__cdecl*)(void*)>(0x004b02a0)(it->resource);
        it=pins.erase(it);
    }
}
void* SlotFor(void* info) {
    if(!info) return nullptr;
    const int index=Field<int>(info,0x30);
    auto* slots=*At<unsigned char**>(0x016b8ad8);
    if(!slots || index<0 || index>=*At<int*>(0x016b8ae0)) return nullptr;
    return slots+index*0x18;
}
void* ResourceFor(void* info) {
    auto* slot=SlotFor(info);
    return slot?Field<void*>(slot,8):nullptr;
}
bool ReclaimUnusedSlot(void* target) {
    auto* manager=At<void*>(0x016b8ad0);
    const int count=Field<int>(manager,0x20);
    auto** active=Field<void**>(manager,0x18);
    if(count<1 || count>16 || !active) return false;
    const int carCount=*At<int*>(0x02f9853c),capacity=*At<int*>(0x02f984f8);
    auto* indices=*At<uint16_t**>(0x02f98534);
    auto** objects=*At<void***>(0x02f984f4);
    if(carCount<0 || carCount>1024 || (carCount && (!indices || !objects))) return false;
    for(int i=0;i<count;++i) {
        auto* slot=active[i];
        if(!slot || slot==target || Field<int>(slot,0x10)!=3) continue;
        auto* resource=Field<void*>(slot,8);
        // Only retire a completed, otherwise unreferenced traffic asset.
        // In particular never evict a live vehicle (even an empty world car).
        if(!resource || !(Field<uint32_t>(slot,0x14)&8) ||
           (Field<uint32_t>(slot,0xc)&(1u<<16)) || Field<short>(resource,8)!=1 ||
           At<int(__cdecl*)(void*)>(0x004ac250)(resource)!=3) continue;
        bool used=false;
        for(const auto& pin:pins) if(pin.resource==resource) used=true;
        for(int j=0;j<carCount;++j) {
            if(indices[j]>=capacity) return false;
            auto* car=objects[indices[j]];
            if(!car || Field<unsigned char>(car,0x7e)!=3) continue;
            auto* info=Field<void*>(car,0x3514);
            if(info && Field<void*>(info,0x34)==resource) used=true;
        }
        if(used) continue;
        Logger::Log("Garage: releasing unused traffic slot {} for requested slot {}; active {}/{}.\n",
            slot,target,count,Field<int>(manager,0x1c));
        At<void(__thiscall*)(void*,void*)>(0x00b523e0)(manager,slot);
        unloadingSlotResource=resource;
        return true; // Native asynchronous unload must finish before loading more.
    }
    return false;
}
void CancelRequest() {
    ApShop::GarageCancel();
    unloadingSlotResource=nullptr;
    Release(heldResource);
    ReleaseRequestSlot();
    if(replacement.handle!=None) {
        if(bayIndex>=0 && bayIndex<static_cast<int>(std::size(bays))) {
            if(auto* node=Object(bays[bayIndex].node)) {
                Field<float>(node,0xa0)=replacement.day;Field<float>(node,0xa4)=replacement.night;
            }
        }
        // Deletion is deferred by the engine. Retain the asset until cleanup.
        if(replacement.resource) pins.push_back({replacement.resource,replacement.handle,replacement.object});
    }
    replacement={};arrival={};retirement={};retiredCar=false;recovering=false;restoreChoice=-1;selected=-1;
}
void Fail(const char* reason) {
    Logger::Log("Garage: {}\n",reason);
    message=reason;CancelRequest();
}
void LoadFailed(const char* reason) {
    if(retiredCar && !recovering && restoreChoice>=0) {
        Logger::Log("Garage: {}; reloading previous vehicle.\n",reason);
        Release(heldResource);ReleaseRequestSlot();selected=restoreChoice;recovering=true;requestedAt=GetTickCount64();return;
    }
    Fail(retiredCar ? "The replacement could not load and the previous car could not be restored. You can choose another vehicle." : reason);
}
void BuildCatalog() {
    if(!choices.empty()) return;
    auto* table=*At<unsigned char**>(0x016ba5fc);
    const int count=*At<int*>(0x016ba604);
    if(!table || count<1 || count>163) return;
    for(int i=0;i<count;++i) {
        auto* info=table+i*0x4bc;
        const char* name=Field<const char*>(info,0);
        const char* mesh=Field<const char*>(info,8);
        int kind=Field<int>(info,0xc),slot=Field<int>(info,0x30);
        if(!name || !*name || !mesh || !*mesh || kind<0 || kind>2 || slot<0 ||
           slot>=*At<int*>(0x016b8ae0)) continue;
        const GarageCatalog::Entry* allowed=nullptr;
        for(const auto& e:GarageCatalog::Entries) if(std::strcmp(e.id,name)==0) {allowed=&e;break;}
        if(!allowed || !GarageCatalog::Enabled(name) || ((allowed->category==4 || kind==1) && !GunshipPolicy::Supported(name))) continue;
        std::string label=name;
        std::replace(label.begin(),label.end(),'_',' ');
        if(kind==1) label+=" (aircraft)";
        Choice c{info,"AP_GARAGE_VEHICLE_"+std::to_string(i),std::wstring(label.begin(),label.end())};
        c.price=allowed->price;c.item=ApVehicles::Find(allowed->family);
        Token(c.token,c.label);choices.push_back(std::move(c));
    }
    std::sort(choices.begin(),choices.end(),[](const Choice& a,const Choice& b){return a.label<b.label;});
    families.clear();
    for(int i=0;i<static_cast<int>(choices.size());++i) {
        auto& c=choices[i];const auto* id=Field<const char*>(c.info,0);
        const GarageCatalog::Entry* entry=nullptr;
        for(const auto& e:GarageCatalog::Entries) if(std::strcmp(e.id,id)==0) {entry=&e;break;}
        const std::string name=entry?entry->family:id;
        auto f=std::find_if(families.begin(),families.end(),[&](const Family& v){return v.name==name;});
        if(f==families.end()) {
            Family item;item.name=name;item.token="AP_GARAGE_FAMILY_"+std::to_string(families.size());item.category=entry?entry->category:4;
            Token(item.token,std::wstring(name.begin(),name.end()));families.push_back(std::move(item));f=std::prev(families.end());
        }
        f->choices.push_back(i);
        const std::string label=entry?entry->variant:"Standard";
        Token(c.token+"_VARIANT",std::wstring(label.begin(),label.end()));
    }
    // Pointer arrays must remain alive until the native screen closes.
    for(auto& f:families) for(int i:f.choices) {
        choices[i].token+="_VARIANT";f.labels.push_back(choices[i].token.c_str());
    }
    Token("AP_GARAGE_TITLE",L"VEHICLE GARAGE");
    Token(categoryTokens[0],L"Cars and rovers");Token(categoryTokens[1],L"Trucks and buses");
    Token(categoryTokens[2],L"EDF combat vehicles");Token(categoryTokens[3],L"Walkers");
    Token(categoryTokens[4],L"Gunships");Token(categoryTokens[5],L"Utility and off-road");
    Token("AP_GARAGE_BACK",L"Back to categories");Token("AP_GARAGE_SPAWN",L"Spawn vehicle");
    Token("AP_GARAGE_OK",L"OK");
    Token("AP_GARAGE_LOCKED",L"No vehicles unlocked");
    Token("AP_GARAGE_CANCEL_LOAD",L"Cancel vehicle request");
    Token("AP_GARAGE_LOADING",L"Loading vehicle. You can cancel and choose another vehicle.");
    Logger::Log("Garage: {} native vehicle definitions in {} families.\n",choices.size(),families.size());
}
void __cdecl VariantChanged(int row,int variant) {
    if(row<0 || row>=rowCount || rows[row]<0 || rows[row]>=static_cast<int>(families.size())) return;
    auto& f=families[rows[row]];
    if(variant>=0 && variant<static_cast<int>(f.choices.size())) f.variant=variant;
}
unsigned char __cdecl Select(int handle,int row,int reason) {
    if(handle!=popup) return 0;
    Logger::Log("Garage: selection popup {}, row {}, reason {}.\n",handle,row,reason);
    popup=-1;
    if(reason) {
        if(category>=0 && selected<0) {category=-1;nextPage=true;}
        return 1;
    }
    if(row>=0 && row<rowCount) {
        const int value=rows[row];
        if(value==-4) {CancelRequest();nextPage=true;}
        else if(value==-5) {category=-1;nextPage=true;}
        else if(value<=-10 && value>=-15) {category=-10-value;nextPage=true;}
        else if(value>=0 && value<static_cast<int>(families.size())) {
            auto& f=families[value];
            if(!FamilyUnlocked(f)) return 1;
            if(f.variant<0 || f.variant>=static_cast<int>(f.choices.size())) return 1;
            const auto& choice=choices[f.choices[f.variant]];
            if(!ApShop::GarageReserve(choice.item,choice.price)) {message="Not enough salvage.";return 1;}
            selected=f.choices[f.variant];requestedAt=GetTickCount64();
            if(category>=0) rememberedRow[category]=row;
            Logger::Log("Garage: requested {} at {:08x}.\n",Field<const char*>(choices[selected].info,0),bays[bayIndex].node);
        }
    }
    return 1;
}
bool DismissLoadingMenu() {
    if(popup<0 || selected<0 || rowCount!=1 || rows[0]!=-4 ||
       GetTickCount64()-loadingPopupAt<2000) return false;
    auto* record=At<void*(__cdecl*)(int)>(0x008cc350)(popup);
    if(!record || At<void*(__cdecl*)()>(0x008cc2e0)()!=record) return false;
    const int closing=popup;
    Select(closing,0,1); // Back closes the popup without cancelling the request.
    At<unsigned char(__cdecl*)(int,int)>(0x008dce30)(closing,1);
    return true;
}
void __fastcall MenuInput(void* menu,void*) {
    // Menu input continues even if the modal dialog pauses player updates.
    if(DismissLoadingMenu()) return;
    // Enum rows have no action callback. Route the game's Accept action to the
    // highlighted garage choice; keep native navigation and other menus intact.
    if(popup>=0 && category>=0 && selected<0 && rowCount>0 && rows[0]>=0 &&
       Field<int>(menu,0x12c)!=0 && !Field<unsigned char>(menu,6) && !Field<unsigned char>(menu,7)) {
        auto* record=At<void*(__cdecl*)(int)>(0x008cc350)(popup);
        if(record && record==At<void*(__cdecl*)()>(0x008cc2e0)() &&
           Field<int>(record,4)==Field<int>(menu,0x10) &&
           !At<unsigned char(__cdecl*)()>(0x0094a7d0)()) {
            const auto group=Field<int>(menu,0x18);
            // Native action 10 is Accept (legend 0x49), not a raw keyboard key.
            if(At<float(__cdecl*)(int,int)>(0x008c7c80)(10,group)>0) {
                const int row=Field<int>(menu,0x1c);
                if(row>=0 && row<rowCount) {
                    Logger::Log("Garage: native Accept on family row {}.\n",row);
                    const int handle=popup;
                    if(Select(handle,row,0)) At<unsigned char(__cdecl*)(int,int)>(0x008dce30)(handle,0);
                    At<void(__cdecl*)()>(0x0089bd60)();
                    return;
                }
            }
        }
    }
    originalMenuInput(menu);
}
void OpenMenu() {
    BuildCatalog();
    if(choices.empty()) {Logger::Log("Garage: native vehicle catalog is not ready.\n");return;}
    const std::string bodyKey=!message.empty()?"AP_GARAGE_MESSAGE_"+std::to_string(text.size()):
        "AP_GARAGE_BALANCE_"+std::to_string(ApShop::GarageSalvage())+"_"+std::to_string(category)+"_"+
        std::to_string(ApShop::VehiclePurchasable(867531428)?ApShop::GaragePrice(867531428,200):-1);
    const bool status=!message.empty();
    if(status) Token(bodyKey,std::wstring(message.begin(),message.end()));
    else {
        std::wstring balance=L"Salvage: "+std::to_wstring(ApShop::GarageSalvage());
        if(category==4 && ApShop::VehiclePurchasable(867531428))
            balance+=L"    Gunship unlock: "+std::to_wstring(ApShop::GaragePrice(867531428,200));
        Token(bodyKey,balance);
    }
    using PopupFn=int(__cdecl*)(int,const char*,const char*,void*,int,char,const char*,const char*,int);
    const bool browsing=!status && selected<0 && category>=0;
    popup=At<PopupFn>(0x008f6c70)(1,browsing?categoryTokens[category]:"AP_GARAGE_TITLE",
        status?bodyKey.c_str():selected>=0?"AP_GARAGE_LOADING":bodyKey.c_str(),
        reinterpret_cast<void*>(&Select),0,0,browsing?"AP_GARAGE_SPAWN":nullptr,nullptr,0);
    if(popup<0) return;
    auto add=At<int(__cdecl*)(int,const char*,int,int)>(0x008f06e0);
    rowCount=0;
    if(status) {rows[rowCount++]=-1;add(popup,"AP_GARAGE_OK",0,0);message.clear();}
    else if(selected>=0) {loadingPopupAt=GetTickCount64();rows[rowCount++]=-4;add(popup,"AP_GARAGE_CANCEL_LOAD",0,0);}
    else if(category<0) {
        for(int i=0;i<6;++i) {
            const bool any=std::any_of(families.begin(),families.end(),[i](const Family& f){return f.category==i && FamilyUnlocked(f);});
            if(any) {rows[rowCount++]=-10-i;add(popup,categoryTokens[i],0,0);}
        }
        if(!rowCount) {rows[rowCount++]=-1;add(popup,"AP_GARAGE_LOCKED",0,0);}
    } else {
        const int list=At<int(__cdecl*)(int)>(0x008cc820)(popup);
        auto* record=At<void*(__cdecl*)(int)>(0x008cc350)(popup);
        if(list!=-1 && record) {
            using EnumFn=int(__cdecl*)(int,const char*,const char* const*,int,void*,int,char);
            for(int i=0;i<static_cast<int>(families.size());++i) {
                auto& f=families[i];if(f.category!=category || !FamilyUnlocked(f)) continue;
                // Native enum menus support 15 rows, popup row storage is bounded too.
                if(rowCount>=14 || Field<int>(record,0x14)>=Field<int>(record,0x10)) break;
                Family priced=f;priced.labels.clear();
                std::vector<std::string> priceTokens;
                for(int choiceIndex:f.choices) {
                    const auto& choice=choices[choiceIndex];
                    const int price=ApShop::GaragePrice(choice.item,choice.price);
                    std::string key=choice.token+"_PRICE_"+std::to_string(price)+
                        (ApShop::VehiclePurchasable(choice.item)?"_BUY":"");
                    const auto* id=Field<const char*>(choice.info,0);
                    const char* variant="";
                    for(const auto& entry:GarageCatalog::Entries) if(std::strcmp(id,entry.id)==0) {variant=entry.variant;break;}
                    std::string label=variant;
                    if(label=="Machine guns") label="MG";
                    if(!label.empty() && label.back()>='0' && label.back()<='9' && label.size()>1 && label[label.size()-2]==' ')
                        label.erase(label.size()-2,1);
                    label+=" "+std::to_string(price);
                    Token(key,std::wstring(label.begin(),label.end()));priceTokens.push_back(std::move(key));
                }
                for(const auto& key:priceTokens) priced.labels.push_back(key.c_str());
                const auto& labels=RetainedVariants(priced);
                const int item=At<EnumFn>(0x008b9260)(list,f.token.c_str(),labels.data(),static_cast<int>(labels.size()),
                    reinterpret_cast<void*>(&VariantChanged),f.variant,1);
                if(item==-1) break;
                auto& count=Field<int>(record,0x14);
                Field<int*>(record,0xc)[count++]=item;
                Field<unsigned char>(record,0x42c)=1;
                rows[rowCount++]=i;
            }
        }
        // The engine suppresses the menu's Accept callback if ANY action row
        // exists, even when an enum row is focused. Escape provides Back here.
    }
    const int list=At<int(__cdecl*)(int)>(0x008cc820)(popup);
    if(list!=-1) {
        // AddRow clears the accept legend; restore an explicit native Spawn action.
        if(browsing) At<void(__cdecl*)(int,const char*,int)>(0x008cbfe0)(list,"AP_GARAGE_SPAWN",0x49);
        At<void(__cdecl*)(int,int)>(0x008b9490)(list,browsing?std::min(rememberedRow[category],std::max(0,rowCount-1)):0);
    }
}
int __cdecl Use(void* player,char activate) {
    const auto handle=player?Field<uint32_t>(player,0xce4):None;
    for(int i=0;i<static_cast<int>(std::size(bays));++i) {
        if(bays[i].marker==None || bays[i].marker!=handle) continue;
        if(!originalUse(player,0)) return 0;
        if(!activate) return 1;
        if(!Campaign() || At<unsigned char(__cdecl*)()>(0x007c1690)()==1 ||
           At<int(__cdecl*)()>(0x007cc8f0)()!=-1) return 0;
        if(popup<0 && replacement.handle==None && arrival.handle==None && retirement.handle==None) {
            if(selected<0) {bayIndex=i;category=-1;}
            OpenMenu();
        }
        return 1;
    }
    return originalUse(player,activate);
}
void CreateMarkers(Player* player) {
    for(auto& bay:bays) {
        auto* node=Object(bay.node);
        if(!node || (Field<unsigned char>(node,0x54)&0x10) || Distance(node,player)>10000.0f) continue;
        auto* marker=Object(bay.marker);
        if(marker && Field<unsigned char>(marker,0x7e)==0x33) continue;
        bay.marker=None;
        float position[3],orient[9];
        // Use the captured position even when the original action node is unloaded.
        std::memcpy(position,bay.position,sizeof(position));
        std::memcpy(orient,static_cast<char*>(node)+0x10,sizeof(orient));
        PropBlock properties{};
        At<void*(__thiscall*)(void*,const float*,const float*,const char*)>(0x00a38b00)
            (&properties,position,orient,"AP Vehicle Garage");
        marker=At<void*(__thiscall*)(void*,uint32_t,void*,void*,uint32_t)>(0x0093e6b0)
            (At<void*>(0x02f98490),0x9526dd04,&properties,nullptr,None);
        At<void(__thiscall*)(void*)>(0x00921950)(&properties);
        if(marker) {
            Field<unsigned char>(marker,0x54)|=0x20; // transient, never serialize into saves
            bay.marker=Field<uint32_t>(marker,0x6c);
            Logger::Log("Garage: interaction {:08x}, parking {:08x}, stand {:08x} at {:.2f}, {:.2f}, {:.2f}.\n",
                bay.marker,bay.node,bay.stand,position[0],position[1],position[2]);
        }
    }
}
bool CapturePosition(Player* player) {
    if(!PlacementTravelUnlocked) return false;
    if(!player || !Campaign() || selected>=0 || popup>=0 ||
       At<unsigned char(__cdecl*)()>(0x007c1690)()==1 || At<int(__cdecl*)()>(0x007cc8f0)()!=-1) return false;
    auto* vehicle=Object(Field<uint32_t>(player,0xac4));
    if(vehicle && Field<unsigned char>(vehicle,0x7e)==3) {
        message="Stand on foot where you want the garage circle, then press Ctrl+F8.";return false;
    }
    int nearest=-1;float distance=3600.0f;
    for(int i=0;i<static_cast<int>(std::size(bays));++i) {
        auto* node=Object(bays[i].node);
        if(!node || (Field<unsigned char>(node,0x54)&0x10)) continue;
        const float d=Distance(node,player);
        if(d<distance) {distance=d;nearest=i;}
    }
    if(nearest<0) {message="Move within 60 metres of a safehouse garage, then press Ctrl+F8.";return false;}
    float pos[3];std::memcpy(pos,reinterpret_cast<char*>(player)+4,sizeof(pos));
    if(!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) return false;
    const auto& bay=bays[nearest];
    Logger::Log("GARAGE_PLACEMENT|garage={}|parking={:08x}|stand={:08x}|x={:.6f}|y={:.6f}|z={:.6f}\n",
        nearest+1,bay.node,bay.stand,pos[0],pos[1],pos[2]);
    message=fmt::format("Garage {} position recorded: {:.2f}, {:.2f}, {:.2f}. Close this message before recording another position. The circle will move in the next build.",
        nearest+1,pos[0],pos[1],pos[2]);
    return true;
}
void PollPlacementKey(Player* player) {
    if(!PlacementTravelUnlocked) return;
#ifndef GARAGE_TEST
    // Development capture only; no keyboard hook or input recording.
    static bool wasDown=false;
    DWORD foregroundProcess=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foregroundProcess);
    const bool down=foregroundProcess==GetCurrentProcessId() &&
        (GetAsyncKeyState(VK_CONTROL)&0x8000) && (GetAsyncKeyState(VK_F8)&0x8000);
    if(down && !wasDown) CapturePosition(player);
    wasDown=down;
#else
    (void)player;
#endif
}
bool OutdoorVehicle(void* info) {
    const char* name=info?Field<const char*>(info,0):nullptr;
    if(name) for(const auto& entry:GarageCatalog::Entries)
        if(std::strcmp(name,entry.id)==0) return GarageOutdoor::Family(entry.family);
    return false;
}
Spawn ParkingSpawn(void* node,void* info) {
    Spawn p{};p.info=info;p.parent=Field<uint32_t>(node,0x70);p.building=None;
    // Native entry eligibility (00b8c160) rejects parented walkers, treating
    // them as mounted cargo. Cars may inherit the garage building; walkers
    // must be independent world objects for their normal entry/exit paths.
    if(info && Field<int>(info,0xc)==2) p.parent=None;
    std::memcpy(p.pos,static_cast<char*>(node)+4,sizeof(p.pos));
    std::memcpy(p.orient,static_cast<char*>(node)+0x10,sizeof(p.orient));
    // Match object_vehicle_spawn_node::spawn (00a6cf20): ordinary vehicle,
    // native fade/activation and static ground placement.
    // m_is_player_car is an engine ownership flag, not a 'player may use' flag.
    p.spawnFlags=0x143 | (p.parent!=None?4u:0u);
    if(OutdoorVehicle(info)) {
        // Large vehicles use the authored outdoor centre and facing.
        // The captured height is a walkable surface, which can be above terrain
        // on a safehouse pad. Native flag 4 keeps that height and adds the model
        // ground offset instead of projecting through the pad onto terrain.
        // These are authored parking points, like the native safehouse nodes
        // (00a6cf20), not ambient drop locations. Keep static-placement bit 100:
        // 00b562c0 otherwise calls the broad 00b56000 admission test and may
        // reject nearby world objects or delete its "removable" overlap list.
        // Static parking skips that admission/cleanup, NOT vehicle physics.
        // The explicit player exclusion below still runs before any deletion.
        // Parent stays None for normal walker entry.
        p.parent=None;p.spawnFlags=0x147;
        if(const auto* point=GarageOutdoor::Find(Field<uint32_t>(node,0x6c))) {
            std::memcpy(p.pos,point->position,sizeof(p.pos));
            std::memcpy(p.orient,point->orientation,sizeof(p.orient));
        }
    }
    return p;
}
void KeepSpawn(Bay& bay,void* node,void* car,void*& resource) {
    bay.car=Field<uint32_t>(car,0x6c);
    traceHandles[&bay-bays].store(bay.car);
    Field<uint32_t>(car,0x4014)=bay.node;
    Field<unsigned char>(car,0x54)|=0xa0; // Native parking lifetime: no-save, destroy on stream.
    Field<float>(node,0xa0)=0;Field<float>(node,0xa4)=0;
    Field<uint32_t>(node,0xa8)=bay.car;
    pins.push_back({resource,bay.car,car});
    if(requestedSlot && resource==Field<void*>(requestedSlot,8)) {
        pins.back().slot=requestedSlot;pins.back().ownsReason=ownsSlotReason;
        requestedSlot=nullptr;ownsSlotReason=false;
    }
    {
        std::lock_guard<std::mutex> lock(ownedMutex);
        ownedVehicles.push_back({car,resource,bay.car});
    }
    resource=nullptr;
    Gunship::Track(car);
}
bool CreateVehicle(Spawn& params) {
    creatingInfo=params.info;
    const bool result=At<unsigned char(__cdecl*)(Spawn*)>(0x00b56420)(&params)!=0;
    creatingInfo=nullptr;
    return result && params.vehicle;
}
void AwaitArrival(void* car,bool restoring) {
    arrival={Field<uint32_t>(car,0x6c),car,GetTickCount64(),0,restoring};
}
bool RestoreVehicle(Bay& bay,void* node) {
    if(!replacement.resource) return false;
    auto backup=replacement.backup;
    if(!CreateVehicle(backup)) return false;
    KeepSpawn(bay,node,backup.vehicle,replacement.resource);
    AwaitArrival(backup.vehicle,true);
    Logger::Log("Garage: original vehicle recreated as {:08x}; waiting for activation.\n",bay.car);
    return true;
}
void CheckArrival(Bay& bay,void* node) {
    auto* car=Object(arrival.handle);
    const auto elapsed=GetTickCount64()-arrival.since;
    if(car!=arrival.object) {
        Logger::Log("Garage: vehicle {:08x} disappeared after {} ms before confirmation.\n",arrival.handle,elapsed);
        const bool restoring=arrival.restoring;arrival={};
        if(!restoring && RestoreVehicle(bay,node)) return;
        Fail("The game removed the vehicle during activation. Please choose another vehicle.");return;
    }
    const bool active=(Field<unsigned char>(car,0x54)&0x10)==0 &&
        (Field<unsigned char>(car,0x55)&0x10)!=0 && Field<unsigned char>(car,0x350c)==1 &&
        Field<uint32_t>(car,0x4c)!=None;
    if(active) {
        if(!arrival.activeSince) arrival.activeSince=GetTickCount64();
        if(GetTickCount64()-arrival.activeSince>=1500) {
            const bool restoring=arrival.restoring;
            Logger::Log("Garage: vehicle {:08x} activated and stable ({} ms).\n",arrival.handle,elapsed);
            Release(replacement.resource);replacement={};arrival={};selected=-1;
            retiredCar=false;recovering=false;restoreChoice=-1;
            if(restoring) {ApShop::GarageCancel();message="The requested vehicle was unavailable. The previous vehicle was restored.";}
            else if(!ApShop::GarageCommit()) message="Payment could not be saved. Salvage refunded.";
            return;
        }
    } else arrival.activeSince=0;
    if(!active && elapsed>=8000) {
        Logger::Log("Garage: activation timeout {:08x}: flags {:08x}, active {}, physics {:08x}.\n",
            arrival.handle,Field<uint32_t>(car,0x54),Field<unsigned char>(car,0x350c),Field<uint32_t>(car,0x4c));
        Fail("Vehicle activation did not complete. You can open the garage again.");
    }
}
bool OutdoorPlayerClear(const Spawn& params,Player* player) {
    // Use the same vehicle radius as native spawn-placement queries. Keep a
    // pedestrian margin; never push the player away or move the authored point.
    float radius=Field<float>(params.info,0x10);
    if(!std::isfinite(radius) || radius<0.5f || radius>30.0f) radius=8.0f;
    radius+=0.75f;
    const float dx=params.pos[0]-Field<float>(player,4),dz=params.pos[2]-Field<float>(player,12);
    return dx*dx+dz*dz>=radius*radius;
}
void FinishSpawn(Bay& bay,void* node,void* info,Player* player) {
    CrashDiagnostics::Stage stage("Garage native vehicle creation");
    auto params=ParkingSpawn(node,info);
    // The resource is already loaded and pinned. Do not re-enter the ambient
    // seven-slot manager (which can refuse a player request indefinitely).
    if(OutdoorVehicle(info) && !OutdoorPlayerClear(params,player)) {
        Fail("Move away from the outdoor vehicle spawn point, then try the garage again.");return;
    }
    if(OutdoorVehicle(info)) Logger::Log("Garage: authored static parking {} at {:08x}: surface {:.3f},{:.3f},{:.3f}, flags {:x}; player clear.\n",
        Field<const char*>(info,0),bay.node,params.pos[0],params.pos[1],params.pos[2],params.spawnFlags);
    const bool result=CreateVehicle(params);
    if(result) {
        KeepSpawn(bay,node,params.vehicle,heldResource);
        AwaitArrival(params.vehicle,recovering);
        Logger::Log("Garage: created {} ({:08x}), replacing {:08x}; awaiting activation.\n",
            Field<const char*>(info,0),bay.car,replacement.handle);
        return;
    }
    Logger::Log("Garage: native spawn failed for {} at {:08x}: centre {:.3f},{:.3f},{:.3f}, flags {:x}, model ground offset {:.3f}, radius {:.3f}, resource status {}.\n",
        Field<const char*>(info,0),bay.node,params.pos[0],params.pos[1],params.pos[2],params.spawnFlags,
        Field<float>(info,0x18),Field<float>(info,0x10),At<int(__cdecl*)(void*)>(0x004ac250)(heldResource));
    if(replacement.resource) {
        if(RestoreVehicle(bay,node)) return;
        Field<float>(node,0xa0)=replacement.day;Field<float>(node,0xa4)=replacement.night;
        Field<uint32_t>(node,0xa8)=None;
    }
    LoadFailed("The game could not create that vehicle. Please choose another vehicle.");
}
void SpawnSelected(Player* player) {
    if(bayIndex<0 || bayIndex>=static_cast<int>(std::size(bays)) ||
       selected<0 || selected>=static_cast<int>(choices.size())) {CancelRequest();return;}
    auto& bay=bays[bayIndex];
    void* node=Object(bay.node);
    if(!node || Distance(node,player)>900.0f) {Fail("Vehicle request cancelled: you left the garage.");return;}
    if(!ResourceThread()) {
        Logger::Log("Garage: wrong resource thread: {} (expected {}).\n",GetCurrentThreadId(),*At<DWORD*>(0x01ac3b4c));
        Fail("The vehicle loader is not available on this update. Please try again.");return;
    }
    void* info=choices[selected].info;
    if(arrival.handle!=None) {CheckArrival(bay,node);return;}
    if(OutdoorVehicle(info)) {
        if(!GarageOutdoor::Find(bay.node)) {Fail("This garage has no outdoor vehicle spawn point.");return;}
        if(!OutdoorPlayerClear(ParkingSpawn(node,info),player)) {
            Fail("Move away from the outdoor vehicle spawn point, then try the garage again.");return;
        }
    }
    if(replacement.handle!=None) {
        if(Object(replacement.handle)==replacement.object) {
            if(GetTickCount64()-replacement.since>=5000) {
                Field<float>(node,0xa0)=replacement.day;Field<float>(node,0xa4)=replacement.night;
                Fail("The game has not finished removing the parked vehicle. Please try again.");
            }
            return;
        }
        FinishSpawn(bay,node,info,player);return;
    }
    if(retirement.handle!=None) {
        if(Object(retirement.handle)==retirement.object) {
            if(GetTickCount64()-retirement.since>=5000) Fail("The game has not finished removing the previous garage vehicle.");
            return;
        }
        SweepPins();
        // Zero references with queued unload is NOT free memory yet.
        const int status=At<int(__cdecl*)(void*)>(0x004ac250)(retirement.resource);
        if(status==1 || status==2 || (Field<unsigned short>(retirement.resource,0x22)&4)) {
            if(GetTickCount64()-retirement.since>=10000) Fail("The previous vehicle is still unloading. Please try again.");
            return;
        }
        Logger::Log("Garage: previous vehicle {:08x} removed; resource status {}, refs {}.\n",
            retirement.handle,status,Field<short>(retirement.resource,8));
        retirement={};requestedAt=GetTickCount64();
    }
    if(!heldResource && !retiredCar && bay.car!=None) {
        auto* previous=Object(bay.car);
        if(previous && Field<unsigned char>(previous,0x7e)==3 && PreserveForEviction(previous,false)) {
            if(Field<uint32_t>(previous,0x3f24)&((1u<<29)|(1u<<24)|(1u<<8)|0xfu)) {
                Fail("Exit the previous garage vehicle before requesting another. Mission vehicles cannot be replaced.");return;
            }
            auto* previousInfo=Field<void*>(previous,0x3514);
            auto* resource=ResourceFor(previousInfo);
            if(!resource) {Fail("The previous vehicle resource is unavailable.");return;}
            for(int i=0;i<static_cast<int>(choices.size());++i) if(choices[i].info==previousInfo) restoreChoice=i;
            retirement={bay.car,previous,resource,GetTickCount64()};retiredCar=true;
            ForgetVehicle(previous,bay.car);
            At<int(__thiscall*)(void*,void*,char)>(0x0091d070)(At<void*>(0x02f98490),previous,0);
            Logger::Log("Garage: retiring previous selection {:08x} before loading {}; releasing its asset after native deletion.\n",bay.car,Field<const char*>(info,0));
            bay.car=None;Field<uint32_t>(node,0xa8)=None;return;
        }
    }
    if(GaragePolicy::TimedOut(GetTickCount64(),requestedAt)) {
        Logger::Log("Garage: request timeout at {:08x} for {}; manager slot {}, pinned assets {}.\n",
            bay.node,Field<const char*>(info,0),requestedSlot,pins.size());
        LoadFailed("No compatible vehicle asset slot became available. Please choose another vehicle or leave the safehouse and return.");return;
    }
    if(!heldResource) {
        // The native manager enforces both the asset budget and mutually
        // exclusive vehicle families. Never load an eighth/conflicting asset
        // directly: failure can leave shared meshes and streaming state broken.
        auto* slot=SlotFor(info);
        if(!slot) {LoadFailed("The vehicle resource is unavailable.");return;}
        void* resource=Field<void*>(slot,8);
        if(!resource) {LoadFailed("The vehicle resource is unavailable.");return;}
        const auto flags=Field<unsigned short>(resource,0x22);
        if(flags&(4u|0x10u)) return; // Unload must finish before acquiring a reference.
        if(unloadingSlotResource) {
            if(Field<unsigned short>(unloadingSlotResource,0x22)&(4u|0x10u)) return;
            unloadingSlotResource=nullptr;
        }
        if(!requestedSlot) {
            const bool alreadyRequested=(Field<uint32_t>(slot,0xc)&(1u<<16))!=0;
            if(!At<unsigned char(__thiscall*)(void*,void*,int)>(0x00b524c0)(At<void*>(0x016b8ad0),slot,16)) {
                ReclaimUnusedSlot(slot);return;
            }
            requestedSlot=slot;ownsSlotReason=!alreadyRequested;
        }
        const int status=At<int(__cdecl*)(void*)>(0x004ac250)(resource);
        if(status==4) {
            Logger::Log("Garage: native loader failed at bay {:08x}, vehicle {}, resource flags {:04x}, refs {}.\n",
                bay.node,Field<const char*>(info,0),Field<unsigned short>(resource,0x22),Field<short>(resource,8));
            LoadFailed("Vehicle asset loading failed. Please try again after leaving the safehouse.");return;
        }
        if(!resource || !At<unsigned char(__cdecl*)(void*)>(0x004aef40)(resource)) {
            LoadFailed("The game could not load that vehicle. The parked car has been kept.");return;
        }
        heldResource=resource;
        Logger::Log("Garage: loading {} with a dedicated resource reference.\n",Field<const char*>(info,0));
    }
    const int resourceState=At<int(__cdecl*)(void*)>(0x004ac250)(heldResource);
    if(resourceState==4) {
        Logger::Log("Garage: asset failure {} resource {} flags {:04x}, refs {}, garage pins {}.\n",Field<const char*>(info,0),heldResource,
            Field<unsigned short>(heldResource,0x22),Field<short>(heldResource,8),pins.size());
        LoadFailed("Vehicle asset loading failed. The parked car has been kept.");return;
    }
    if(resourceState!=3) return;
    Logger::Log("Garage: {} assets ready.\n",Field<const char*>(info,0));
    void* old=nullptr;
    const int count=*At<int*>(0x02f9853c);
    auto* indices=*At<uint16_t**>(0x02f98534);
    auto* objects=*At<void***>(0x02f984f4);
    const int capacity=*At<int*>(0x02f984f8);
    if(count<0 || count>1024 || (count && (!indices || !objects))) {Fail("The garage is not ready yet.");return;}
    for(int i=0;i<count;++i) {
        if(indices[i]>=capacity) {Fail("The garage vehicle list is not ready yet.");return;}
        auto* car=objects[indices[i]];
        if(!car || Field<unsigned char>(car,0x7e)!=3) continue;
        const bool owned=Field<uint32_t>(car,0x4014)==bay.node ||
            (bay.car!=None && Field<uint32_t>(car,0x6c)==bay.car);
        if(owned && Distance(car,node)<=144.0f &&
           (Field<uint32_t>(car,0x3f24)&((1u<<29)|(1u<<24)|(1u<<8)|0xfu))) {
            Fail("The garage vehicle is occupied or in use by a mission.");return;
        }
        if(GaragePolicy::Replaceable(Field<uint32_t>(car,0x4014),bay.node,
            Field<uint32_t>(car,0x6c),bay.car,Field<uint32_t>(car,0x3f24),Distance(car,node),
            (Field<unsigned char>(car,0x54)&0x10)!=0)) {
            if(old) {Fail("More than one garage-owned vehicle is parked here. Move one away first.");return;}
            old=car;
        }
    }
    if(old) {
        auto* oldInfo=Field<void*>(old,0x3514);
        auto* oldResource=ResourceFor(oldInfo);
        if(!oldResource || At<int(__cdecl*)(void*)>(0x004ac250)(oldResource)!=3 ||
           !At<unsigned char(__cdecl*)(void*)>(0x004aef40)(oldResource)) {
            Fail("The parked vehicle cannot be replaced safely yet. Please try again.");return;
        }
        replacement.handle=Field<uint32_t>(old,0x6c);replacement.object=old;
        replacement.resource=oldResource;replacement.backup=ParkingSpawn(node,oldInfo);
        if(OutdoorVehicle(oldInfo)) {
            std::memcpy(replacement.backup.pos,static_cast<char*>(old)+4,sizeof(replacement.backup.pos));
            std::memcpy(replacement.backup.orient,static_cast<char*>(old)+0x10,sizeof(replacement.backup.orient));
            // Stored objects use model-centre Y, whereas static spawn takes
            // surface Y and subtracts the model's minimum Y once (00b562c0).
            replacement.backup.pos[1]+=Field<float>(oldInfo,0x18);
        }
        replacement.day=Field<float>(node,0xa0);replacement.night=Field<float>(node,0xa4);
        replacement.since=GetTickCount64();
        Field<float>(node,0xa0)=0;Field<float>(node,0xa4)=0;Field<uint32_t>(node,0xa8)=None;
        {CrashDiagnostics::Stage stage("Garage parked vehicle removal");
        At<void(__thiscall*)(void*,void*,char)>(0x0091d070)(At<void*>(0x02f98490),old,0);}
        Logger::Log("Garage: removing parked vehicle {:08x} before replacement.\n",replacement.handle);
        return; // Wait for native deletion; never create overlapping replacements.
    }
    FinishSpawn(bay,node,info,player);
}
void __cdecl SetState(int state,char uninterruptible) {
    if(state==0 || state==2) {
        Gunship::Reset();
            // Dismiss our screen on campaign load; immutable labels also survive its fade.
        if(popup>=0 && At<void*(__cdecl*)(int)>(0x008cc350)(popup))
            At<unsigned char(__cdecl*)(int,int)>(0x008dce30)(popup,0);
        ClearOwnedVehicles(); // Loading/teardown must be allowed to delete everything.
        CancelRequest();choices.clear();families.clear();popup=-1;bayIndex=-1;selected=-1;nextPage=false;message.clear();nextScan=0;
        for(auto& bay:bays) {
            auto* marker=Object(bay.marker);
            if(marker && Field<unsigned char>(marker,0x7e)==0x33)
                At<void(__thiscall*)(void*,void*,char)>(0x0091d070)(At<void*>(0x02f98490),marker,0);
            bay.marker=None;bay.car=None;
        }
    }
    originalSetState(state,uninterruptible);
}
int DeleteFrom(void* world,void* object,char flags,uintptr_t caller) {
    // spawn_resource_manager evicts its ambient slot by deleting every car
    // using that asset, even when another owner holds a resource reference.
    // Keep only our independently pinned car (and its occupants); let the
    // manager release its slot normally. All other deletion paths still run.
    if(caller==0x00b51b88 && PreserveForEviction(object)) return 0;
    if(caller==0x00b51c08 && object && PreserveForEviction(Object(Field<uint32_t>(object,0xac4)))) return 0;
    if(object && Field<unsigned char>(object,0x7e)==3) {
        const auto handle=Field<uint32_t>(object,0x6c);
        Gunship::Forget(object,handle);
        ForgetVehicle(object,handle);
        auto* creating=creatingInfo.load();
        bool tracked=creating && Field<void*>(object,0x3514)==creating;
        for(const auto& trackedHandle:traceHandles) if(trackedHandle.load()==handle) tracked=true;
        if(tracked) {
            Logger::Log("Garage: vehicle {:08x} removal caller {:08x}, object flags {:08x}, vehicle flags {:08x}:{:08x}, active {}, physics {:08x}.\n",
                handle,caller,Field<uint32_t>(object,0x54),Field<uint32_t>(object,0x3f28),
                Field<uint32_t>(object,0x3f24),Field<unsigned char>(object,0x350c),Field<uint32_t>(object,0x4c));
        }
    }
    return originalDelete(world,object,flags);
}
int __fastcall ObserveDelete(void* world,void*,void* object,char flags) {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-Globals::ModuleBase+0x400000;
    return DeleteFrom(world,object,flags,caller);
}
}
bool LookupText(uint32_t key,const wchar_t** result) {
    auto found=text.find(key);
    if(!result || found==text.end()) return false;
    *result=found->second.c_str();return true;
}
void Frame(Player* player) {
    if(!player || !Campaign()) return;
    CrashDiagnostics::Stage stage("Garage update");
    UnlockPlacementTravel(player);
    PollPlacementKey(player);
    SweepPins();
    if(At<unsigned char(__cdecl*)()>(0x007c1690)()==1 || At<int(__cdecl*)()>(0x007cc8f0)()!=-1) {
        if(selected>=0) {Logger::Log("Garage: request cancelled for mission/activity.\n");CancelRequest();}
        return;
    }
    if(popup>=0) {
        // A modal native menu pauses streaming. Briefly allow cancellation,
        // then dismiss a loading-only popup so loading and its deadline resume.
        DismissLoadingMenu();
        return;
    }
    if(nextPage || !message.empty()) {nextPage=false;OpenMenu();return;}
    if(selected>=0) {SpawnSelected(player);return;}
    const auto now=GetTickCount64();
    if(now>=nextScan) {nextScan=now+1000;CreateMarkers(player);}
}
bool Install(IHookManager& hooks) {
    return hooks.CreateHook("APGarageInteraction",static_cast<DWORD>(Globals::ModuleBase+0x634370),Use,originalUse)
        && hooks.CreateHook("APGarageLoadReset",static_cast<DWORD>(Globals::ModuleBase+0x3d8710),SetState,originalSetState)
        && hooks.CreateHook("APGarageAccept",static_cast<DWORD>(Globals::ModuleBase+0x4dbf80),MenuInput,originalMenuInput)
        && hooks.CreateHook("APGarageVehicleLifetime",static_cast<DWORD>(Globals::ModuleBase+0x51d070),ObserveDelete,originalDelete);
}
}
