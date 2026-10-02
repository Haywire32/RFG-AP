#ifdef APSHOP_NATIVE_TEST
#include "ApShopNativeTestSupport.h"
#elif defined(AP_STANDALONE_RUNTIME)
#include "NativeSupport.h"
#else
#include "Globals.h"
#include "Functions.h"
#include "IHookManager.h"
#endif
#include "ApShop.h"
#ifndef APSHOP_NATIVE_TEST
#include "Garage.h"
#include "Gunship.h"
#endif
#include "ApShopState.h"
#include "ApStoryCatalog.h"
#include "ApProgressionCatalog.h"
#include "ApActivityNames.h"
#include "ApActivityMap.h"
#include "ApDestructionCatalog.h"
#include "ApCollectibleCatalog.h"
#include "ApVehicleCatalog.h"
#include "ApShopTiers.h"
#include "EquipmentTuning.h"
#include <set>
#include <fstream>
#include <map>
#include <cstddef>
#include <intrin.h>
#include <deque>

namespace ApShop {
namespace {
using Json = nlohmann::json;
struct Notice { std::string id, text; };
std::deque<Notice> notices;
std::set<std::string> seenNotices;
uint64_t nextNoticeTime=0;
struct Snapshot {
    std::string session;
    Masks enabled{}, checked{};
    std::array<int, Rows> owned{};
    std::array<bool, 96> weapons{};
    std::map<int, int> salvage;
    std::map<int, std::wstring> shopRewards;
    int startDefinition = -1, startRow = -1;
    bool progression = false;
    bool shopRewardVisibility = true;
    int progressionVersion = 0;
    bool features060 = false, shopTiers = false;
    bool vehicleCosts=false,gunshipPurchase=false;
    int gunshipCost=1000;
    std::set<int> vehicles, collectibleChecks;
    std::array<int,96> ammo{};
    int backpackRecharge=0,backpackPower=0;
    unsigned sectors = 1;
    int storyRequired = 20;
    std::set<int> serverStories;
    std::set<int> destructionChecks, serverTargets;
};
// Native list layout independently verified against 0087C950 and its consumers.
struct Entry {
    int row;
    uint32_t title, description;
    const char* icon;
    const char* blueprint;
    uint8_t locked, available, isNew, padding;
    int price;
};
static_assert(sizeof(Entry) == 0x1c, "RF:G x86 shop record layout");
static_assert(sizeof(UpgradeItem) == 8, "RF:G upgrade state stride");
static_assert(offsetof(PlayerMetadata, upgrades) == 0x18, "RF:G metadata layout");
std::mutex mutex;
Snapshot pending, state;
bool hasPending = false, installed = false, journalFailed = false;
std::atomic<bool> active{false};
std::atomic<bool> configured{false};
std::string sessionKey;
Masks purchased{};
std::map<int, int> appliedSalvage;
std::set<int> completedStories, pendingStories;
std::set<int> completedActivities;
std::set<int> destroyedTargets, completedTargets, targetEvents;
bool targetsDirty=false;
std::set<int> collectedObjects, completedCollectibles, collectibleEvents;
std::map<uint32_t,std::pair<void*,bool>> tagObservations;
bool collectiblesDirty=false;
std::array<Entry, 128> entries{};
std::array<int, 128> offerLevels{}; // zero is a vanilla/local equip action
std::string journalPath;
int equippedBackpack=-1;
bool gunshipPurchased=false;
struct GaragePayment {Player* player=nullptr;uint32_t handle=0;int price=0;bool unlock=false;};
GaragePayment garagePayment;
std::string lastPayload;
Player* lastPlayer = nullptr;
int lastPlayTime = -1;
uint64_t lastReport = 0;
bool rebuilding = false;
using BuildFn = void(__cdecl*)();
using ApplyFn = int(__thiscall*)(PlayerMetadata*, int, char, char);
using PriceFn = unsigned short(__thiscall*)(void*, int, char);
using MetadataFn = PlayerMetadata*(__cdecl*)();
using RegisterFn = void(__cdecl*)(weapon_info*,bool);
using RegistryQueryFn = bool(__cdecl*)(weapon_info*,bool);
using TargetDestroyedFn=void(__cdecl*)(void*,int);
TargetDestroyedFn originalTargetDestroyed=nullptr;
using FogQueryFn=bool(__cdecl*)(const float*);
FogQueryFn originalFogQuery=nullptr;
// 00862D30 takes one stack argument and returns with RET 4 on every path.
// Both the replacement and trampoline type must preserve callee cleanup.
using TargetIconFn=int(__stdcall*)(void*);
TargetIconFn originalTargetIcon=nullptr;
using AmmoLimitFn=unsigned(__fastcall*)(void*);
AmmoLimitFn originalAmmoLimit=nullptr;
using AmmoAddFn=int(__thiscall*)(void*,int);
AmmoAddFn originalAmmoAdd=nullptr;
using RestockFn=void(__fastcall*)(void*);
RestockFn originalRestock=nullptr;
thread_local unsigned restockDepth=0;
using ExplosionFn=void(__cdecl*)(void*,void*,void*,void*,void*,void*,void*,bool);
ExplosionFn originalExplosion=nullptr;
using BackpackFn=void(__thiscall*)(void*);
BackpackFn originalBackpackUpdate=nullptr,originalBackpackActivate=nullptr;
using RechargeFn=float(__cdecl*)();
RechargeFn originalCampaignRecharge=nullptr;
thread_local void* updatingBackpack=nullptr;
void* originalJetpackDrainInstruction=nullptr;
void* jetpackDrainContinue=nullptr;
BuildFn originalBuild = nullptr;
BuildFn originalCabinet = nullptr;
RegisterFn originalRegister = nullptr;
RegistryQueryFn originalRegistryQuery = nullptr;
ApplyFn originalApply = nullptr;
using CompleteFn = void(__thiscall*)(void*);
CompleteFn originalComplete = nullptr;
using MissionAvailableFn = int(__thiscall*)(void*);
MissionAvailableFn originalMissionAvailable = nullptr;
using NodeReadyFn=bool(__thiscall*)(void*);
NodeReadyFn originalHouseReady=nullptr,originalRaidReady=nullptr,originalCollateralReady=nullptr,originalDeliveryLocked=nullptr;
using ActivityStartFn=bool(__thiscall*)(void*,void*,void*);
ActivityStartFn originalActivityStart=nullptr;
using CampGateFn=bool(__fastcall*)(void*);
CampGateFn originalCampGate=nullptr;
using PickupReadyFn=bool(__cdecl*)(void*,void*,char);
using PickupObjectFn=void*(__cdecl*)(void*,void*,char,char,char);
using SwapObjectFn=void(__cdecl*)(void*,uint32_t,int);
PickupReadyFn originalPickupReady=nullptr;
PickupObjectFn originalPickupObject=nullptr;
SwapObjectFn originalSwapObject=nullptr;
using DistrictAtFn=void*(__cdecl*)(const float*);
DistrictAtFn originalDistrictAt=nullptr;
BuildFn originalLiberationMarkerCleanup=nullptr;
using PlotQueryFn=bool(__cdecl*)();
PlotQueryFn originalGunsComplete=nullptr;
using MapPointFn=void(__thiscall*)(void*,const float*,int,char,int,void*,int,uint32_t);
using MapAreaFn=void(__thiscall*)(void*,const float*,int,char,void*,void*,int,void*,int);
MapPointFn originalMapPoint=nullptr;
MapAreaFn originalMapArea=nullptr;
using MapRenderFn=void(__thiscall*)(void*);
MapRenderFn originalMapRender=nullptr;
MapRenderFn originalMiniMapRender=nullptr;
BuildFn originalFullMapMenuRender=nullptr;
using EncodeTextFn=void(__cdecl*)(char*,unsigned,const wchar_t*);
EncodeTextFn originalEncodeText=nullptr;
using SetUiValueFn=void(__thiscall*)(void*,int,const void*);
SetUiValueFn originalSetUiValue=nullptr;
using LookupTextFn=bool(__thiscall*)(void*,uint32_t,const wchar_t**);
template<class T> T Address(uintptr_t va);
LookupTextFn originalLookupText=nullptr;
std::map<uint32_t,std::wstring> shopTextCatalog;
std::map<std::wstring,uint32_t> shopTextKeys;
bool __fastcall LookupShopText(void* catalog,void*,uint32_t key,const wchar_t** result) {
#ifndef APSHOP_NATIVE_TEST
    if(catalog==Address<void*>(0x02c7fc90) && Garage::LookupText(key,result)) return true;
#endif
    const auto found=shopTextCatalog.find(key);
    if(catalog==Address<void*>(0x02c7fc90) && result && found!=shopTextCatalog.end()) {
        *result=found->second.c_str();
        return true;
    }
    const bool foundNative=originalLookupText(catalog,key,result);
#ifndef APSHOP_NATIVE_TEST
    if(foundNative && result && catalog==Address<void*>(0x02c7fc90)) *result=Gunship::ControlHint(*result);
#endif
    return foundNative;
}
uint32_t ShopTextKey(const std::wstring& text) {
    const auto existing=shopTextKeys.find(text);
    if(existing!=shopTextKeys.end()) return existing->second;
    // Retain returned strings for the process lifetime: GUI layouts can cache
    // pointers. A new key also invalidates cached text when scouting arrives.
    for(uint32_t key=0xa9500000u;key<0xa9501000u;++key) {
        const wchar_t* native=nullptr;
        if(shopTextCatalog.count(key) || originalLookupText(Address<void*>(0x02c7fc90),key,&native)) continue;
        shopTextCatalog.emplace(key,text);
        shopTextKeys.emplace(text,key);
        return key;
    }
    return 0;
}
using MapObjectFn=void(__thiscall*)(void*,uint32_t,int,char,char,char);
using MapRemoveFn=void(__thiscall*)(void*,uint32_t);
MapObjectFn originalMapObject=nullptr;
MapRemoveFn originalMapRemove=nullptr;
struct HiddenMarker { void* map; int icon; char selected,flag; };
std::map<uint32_t,HiddenMarker> hiddenMarkers;
void ReconcileMap(void* map);
void RevealActivityMap(void* map);
template<class T> T Address(uintptr_t va) {
#ifdef APSHOP_NATIVE_TEST
    return reinterpret_cast<T>(ApShopTestAddress(va));
#else
    return reinterpret_cast<T>(Globals::ModuleBase + va - 0x400000);
#endif
}
PlayerMetadata* Metadata() { return Address<MetadataFn>(0x00a108e0)(); }
unsigned char* Definition(int row) { return static_cast<unsigned char*>(rfg::upgrade_info_get(row)); }
unsigned char* Level(int row, int level) {
    auto* def = Definition(row);
    return def && level >= 0 && level < def[5] ? def + 0x18 + 0x20 * level : nullptr;
}
bool OwnedCosmetic(int row) { return Cosmetic(row) && state.owned[row] > 0; }
bool BackpackRow(int row) { return row==0 || (row>=53 && row<62); }
bool OwnedEquip(int row) { return OwnedCosmetic(row) || (row==0 && state.enabled[0] && state.owned[0]>0); }
int BaseDefinition(int row) {
    switch(row) {
    case 2:return 12; case 3:return 17; case 6:return 16; case 9:return 11;
    case 11:return 13; case 14:return 14; case 15:return 15; case 25:return 4;
    default:return -1;
    }
}
bool WriteJournal(const Masks& checks, const std::map<int,int>& salvage) {
    try {
        Json j;
        j["version"] = 2; j["session"] = state.session;
        j["purchased"] = checks;
        j["stories"] = completedStories;
        j["activities"] = completedActivities;
        j["destroyed_targets"] = destroyedTargets;
        j["destruction_checks"] = completedTargets;
        j["equipped_backpack"] = equippedBackpack;
        j["gunship_purchased"] = gunshipPurchased;
        j["collected_objects"] = collectedObjects;
        j["collectible_checks"] = completedCollectibles;
        j["salvage"] = Json::object();
        for(const auto& item:salvage) j["salvage"][std::to_string(item.first)] = item.second;
        const auto bytes = j.dump();
        const auto temp = journalPath + ".tmp";
        HANDLE file = CreateFileA(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
        ok = FlushFileBuffers(file) && ok;
        CloseHandle(file);
        return ok && MoveFileExA(temp.c_str(), journalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } catch(...) { return false; }
}
bool LoadJournal() {
    gunshipPurchased=false;
    destroyedTargets.clear(); completedTargets.clear(); targetsDirty=false;
    collectedObjects.clear(); completedCollectibles.clear(); collectibleEvents.clear();
    tagObservations.clear(); collectiblesDirty=false;
    purchased.fill(0); appliedSalvage.clear(); completedStories.clear(); completedActivities.clear(); equippedBackpack=-1;
    try {
#ifdef AP_STANDALONE_RUNTIME
        const auto directory = Globals::GetEXEPath(false) + "RFGArchipelago/Shopsanity/";
#else
        const auto directory = Globals::GetEXEPath(false) + "RSL/AP/Shopsanity/";
#endif
        fs::create_directories(directory);
        journalPath = directory + state.session + ".json";
        std::string readPath=journalPath;
#ifdef AP_STANDALONE_RUNTIME
        // Import an existing run only after its complete journal validates.
        // Keeping the old file intact also leaves the development build's
        // recovery path available; a valid new journal always takes priority.
        if(!fs::exists(journalPath)) {
            const auto legacy=Globals::GetEXEPath(false)+"RSL/AP/Shopsanity/"+state.session+".json";
            if(fs::exists(legacy)) readPath=legacy;
        }
#endif
        if(!fs::exists(readPath)) return WriteJournal(purchased, appliedSalvage);
        std::ifstream f(readPath);
        Json j; f >> j;
        if(j.at("version") != 2 || j.at("session") != state.session) return false;
        gunshipPurchased=j.value("gunship_purchased",false);
        equippedBackpack=j.value("equipped_backpack",-1);
        if(equippedBackpack!=-1 && !BackpackRow(equippedBackpack)) return false;
        purchased = j.at("purchased").get<Masks>();
        for(int row=0;row<Rows;++row) if(purchased[row] & ~CatalogMask(row)) return false;
        if(j.find("stories")!=j.end()) {
            completedStories=j.at("stories").get<std::set<int>>();
            for(int id:completedStories) if(id<867530200 || id>867530221) return false;
        }
        if(j.find("activities")!=j.end()) {
            completedActivities=j.at("activities").get<std::set<int>>();
            for(int id:completedActivities) if(!ApProgression::ValidActivity(id)) return false;
        }
        collectedObjects=j.value("collected_objects",std::set<int>{});
        completedCollectibles=j.value("collectible_checks",std::set<int>{});
        for(int id:collectedObjects) if(!ApCollectibles::Find(id)) return false;
        for(int id:completedCollectibles) if(!collectedObjects.count(id)) return false;
        destroyedTargets=j.value("destroyed_targets",std::set<int>{});
        completedTargets=j.value("destruction_checks",std::set<int>{});
        for(int id:destroyedTargets) if(!ApDestruction::Find(id)) return false;
        for(int id:completedTargets) if(!destroyedTargets.count(id)) return false;
        for(auto it=j.at("salvage").begin();it!=j.at("salvage").end();++it) {
            int index=std::stoi(it.key()), amount=it.value().get<int>();
            if(index<0 || amount<=0 || amount>30000) return false;
            appliedSalvage[index]=amount;
        }
        return readPath==journalPath || WriteJournal(purchased,appliedSalvage);
    } catch(...) { return false; }
}
Masks AllChecked() {
    auto result=purchased;
    for(int row=0;row<Rows;++row) result[row] |= state.checked[row];
    return result;
}
int StoryCount() {
    auto stories=state.serverStories;
    stories.insert(completedStories.begin(),completedStories.end());
    return static_cast<int>(std::count_if(stories.begin(),stories.end(),ApProgression::CountedStory));
}
bool MissionAllowed(const ApProgression::Mission& mission) {
    return ApProgression::MissionOpen(mission,state.sectors,StoryCount(),state.storyRequired,state.progressionVersion<2);
}
void AnnounceFinale() {
    if(!active || !state.progression || completedStories.count(ApProgression::FinalStory) ||
       state.serverStories.count(ApProgression::FinalStory)) return;
    const auto* final=ApProgression::FindMission("Final Mission");
    if(!final || !MissionAllowed(*final)) return;
    std::lock_guard<std::mutex> lock(mutex);
    const auto id="finale-unlocked:"+state.session;
    if(seenNotices.insert(id).second)
        notices.push_back({id,"Final mission unlocked: Mars Attacks! Look for its mission marker on the map."});
}
std::wstring StoryTrackerText() {
    return L"You have completed "+std::to_wstring(StoryCount())+L" out of "+
        std::to_wstring(state.storyRequired)+L" story missions to unlock the final mission";
}
void __fastcall MapRender(void* map,void*) {
    // Native map renderers apply exploration visibility after points are
    // registered. Sector ownership replaces that filter for fixed AP actions
    // and EDF targets, without changing the explored terrain or map filters.
    std::map<int,unsigned char> iconFlags;
    if(active && state.progression && (*Address<unsigned*>(0x02bd9328)&0x60)==0x60) {
        for(const auto& point:ApProgression::ActivityMapPoints) iconFlags.emplace(point.icon,0);
        iconFlags.emplace(0x36,0);iconFlags.emplace(0x37,0);
        for(auto& entry:iconFlags) {
            auto* flags=Address<unsigned char*>(0x02bd03dc+200*entry.first);
            entry.second=*flags;*flags|=2;
        }
    }
    ReconcileMap(map);
    RevealActivityMap(map);
    originalMapRender(map);
    for(const auto& entry:iconFlags) *Address<unsigned char*>(0x02bd03dc+200*entry.first)=entry.second;
}
void __cdecl FullMapMenuRender() {
    originalFullMapMenuRender();
    // Registered exclusively as the fullscreen-map menu's render callback.
    // Draw after its GUI, with the same visibility flags as the native menu.
    if(!active || !state.progression || (*Address<unsigned*>(0x02bd9328)&0x60)!=0x60) return;
    const auto text=StoryTrackerText();
    const auto* screen=Address<int*(__cdecl*)()>(0x0089bdb0)();
    const int font=*Address<int*>(0x0163f160); // font_body; 0163f164 is font_numbers
    // Font handles can be tagged negative values (the live map uses
    // 0x80000004). Resolve them through the engine instead of testing the sign.
    if(screen[0]<=0 || screen[1]<=0 || !Address<void*(__cdecl*)(int)>(0x00594140)(font)) return;
    const int width=Address<int(__cdecl*)(const wchar_t*,int)>(0x008b4a80)(text.c_str(),font);
    if(width<=0) return;
    // Size the sentence to the display instead of the tiny map-label scale.
    // Limit ultrawide growth and leave clear space above the control hints.
    const float scale=std::min(screen[0]*0.54f,screen[1]*0.92f)/width;
    const int x=static_cast<int>((screen[0]-width*scale)*0.5f),y=static_cast<int>(screen[1]*0.79f);
    using ColorFn=void(__cdecl*)(int,int,int,int);
    using TextFn=void(__cdecl*)(int,int,const wchar_t*,float,int,void*);
    auto color=Address<ColorFn>(0x00509170);
    auto draw=Address<TextFn>(0x00555550);
    color(0,0,0,255);
    draw(x+2,y+2,text.c_str(),scale,font,Address<void*>(0x0150a648));
    color(255,255,255,255);
    draw(x,y,text.c_str(),scale,font,Address<void*>(0x0150a648));
}
void __fastcall MiniMapRender(void* map,void*) {
    ReconcileMap(map);
    originalMiniMapRender(map);
#ifndef APSHOP_NATIVE_TEST
    Gunship::RenderHud();
#endif
}
void __cdecl EncodeMapTitle(char* destination,unsigned capacity,const wchar_t* text) {
#ifdef APSHOP_NATIVE_TEST
    const auto caller=ApShopTestCaller;
#else
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-Globals::ModuleBase+0x400000;
#endif
    std::wstring storyLabel;
    // The story-title branch formats a sector briefing before this conversion.
    if(active && state.progression && caller==0x0087ad49 && *Address<unsigned char*>(0x02bcc080)) {
        auto* table=*Address<unsigned char**>(0x02246c1c);
        const int count=*Address<int*>(0x02246c24);
        const auto primary=*Address<uint32_t*>(0x02bcc084),secondary=*Address<uint32_t*>(0x02bcc088);
        if(table && count>0 && count<=128) for(int i=0;i<count;++i) {
            auto* record=table+i*0x60;
            const auto handle=*reinterpret_cast<uint32_t*>(record+0x10);
            if(handle!=primary && handle!=secondary) continue;
            const auto* name=*reinterpret_cast<const char**>(record);
            const auto* mission=ApProgression::FindMission(name);
            const auto* title=ApStory::Find(name);
            if(!mission || !title || !MissionAllowed(*mission)) continue;
            constexpr const wchar_t* sectors[]={L"Parker",L"Dust",L"Badlands",L"Oasis",L"Free Fire Zone",L"Eos",L"Mount Vogel"};
            storyLabel=sectors[mission->sector];storyLabel+=L" - ";
            for(const char* c=title->title;*c;++c) storyLabel+=static_cast<wchar_t>(*c);
            text=storyLabel.c_str();break;
        }
    }
    // Only the hover title conversion, never descriptions, HUD or localization.
    if(active && state.progression && caller==0x0087ad7e && *Address<unsigned char*>(0x02bcc080)) {
        const auto primary=*Address<uint32_t*>(0x02bcc084);
        const auto secondary=*Address<uint32_t*>(0x02bcc088);
        const auto* marker=ApProgression::FindMarker(primary);
        if(!marker) marker=ApProgression::FindMarker(secondary);
        const auto* target=ApDestruction::FindHandle(primary);
        if(!target) target=ApDestruction::FindHandle(secondary);
        if(target && state.destructionChecks.count(target->id) && ApProgression::SectorOpen(state.sectors,target->sector))
            text=target->label;
        if(marker && ApProgression::SectorOpen(state.sectors,marker->sector)) {
            if(const auto* label=ApProgression::ActivityLabel(ApProgression::ActivityLocation(marker->activity)))
                text=label;
        }
    }
    originalEncodeText(destination,capacity,text);
}
void __fastcall SetShopDescription(void* widget,void*,int index,const void* value) {
#ifdef APSHOP_NATIVE_TEST
    const auto caller=ApShopTestCaller;
#else
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-Globals::ModuleBase+0x400000;
#endif
    // Slot 6 in the upgrade details panel. Leave titles, prices and other UI alone.
    if(active && state.shopRewardVisibility && caller==0x00859c95 && index==6 && widget && value) {
        const int count=*Address<int*>(0x01661804);
        const int selected=*Address<int*>(0x02b9dd18)+*Address<int*>(0x02b9dd1c);
        if(*Address<Entry**>(0x016617fc)==entries.data() && selected>=0 && selected<count && selected<128) {
            const int row=entries[selected].row,level=offerLevels[selected];
            if(row>=0 && row<Rows && level>0) {
                const auto found=state.shopRewards.find(row*16+level);
                const wchar_t* text=found==state.shopRewards.end() ? L"Reward information unavailable" : found->second.c_str();
                const auto key=ShopTextKey(text);
                if(key) {
                    // The shop layout consumes a localized hash (type 2), not
                    // a literal string (type 4). Preserve its native contract.
                    std::array<uint32_t,8> replacement{};
                    replacement[0]=2;
                    replacement[2]=key;
                    originalSetUiValue(widget,index,replacement.data());
                    return;
                }
            }
        }
    }
    originalSetUiValue(widget,index,value);
}
int __fastcall MissionAvailable(void* record,void*) {
    if(active && state.progression && record) {
        const auto* mission=ApProgression::FindMission(*static_cast<const char**>(record));
        if(mission) return MissionAllowed(*mission) ? 1 : 0;
    }
    return originalMissionAvailable(record);
}
void* ProgressionObject(uint32_t handle) {
    using FindFn=void*(__thiscall*)(void*,uint32_t);
    return Address<FindFn>(0x0093c050)(Address<void*>(0x02f98490),handle);
}
void __cdecl TargetDestroyed(void* object,int destroyed) {
    auto* bytes=static_cast<unsigned char*>(object);
    const auto* target=bytes ? ApDestruction::FindHandle(*reinterpret_cast<uint32_t*>(bytes+0x6c)) : nullptr;
    const auto* collectible=bytes ? ApCollectibles::FindHandle(*reinterpret_cast<uint32_t*>(bytes+0x6c)) : nullptr;
    const bool transition=(target || (collectible && !collectible->radio)) && destroyed && !(bytes[0x10c]&0x10);
    originalTargetDestroyed(object,destroyed);
    if(active && transition && (bytes[0x10c]&0x10)) {
        std::lock_guard<std::mutex> lock(mutex);
        if(target) targetEvents.insert(target->id);
        if(collectible) collectibleEvents.insert(collectible->id);
    }
}
void ReconcileCollectibles(bool reload) {
    if(!state.features060) return;
    if(reload) tagObservations.clear();
    // Observe live transitions, never import a completed save's counters.
    for(const auto& c:ApCollectibles::Entries) if(c.radio && state.collectibleChecks.count(c.id)) {
        auto* object=static_cast<unsigned char*>(ProgressionObject(c.handle));
        if(!object || (object[0x54]&0x10)) {tagObservations.erase(c.handle);continue;}
        const bool found=(object[0xac]&1)!=0;
        auto previous=tagObservations.find(c.handle);
        if(previous!=tagObservations.end() && previous->second.first==object && !previous->second.second && found) {
            std::lock_guard<std::mutex> lock(mutex);
            collectibleEvents.insert(c.id);
        }
        tagObservations[c.handle]={object,found};
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        for(int id:collectibleEvents) if(state.collectibleChecks.count(id))
            collectiblesDirty=collectedObjects.insert(id).second || collectiblesDirty;
        collectibleEvents.clear();
    }
    for(int id:collectedObjects) {
        const auto* c=ApCollectibles::Find(id);
        if(state.collectibleChecks.count(id) && c && ApProgression::SectorOpen(state.sectors,c->sector))
            collectiblesDirty=completedCollectibles.insert(id).second || collectiblesDirty;
    }
    if(collectiblesDirty && WriteJournal(purchased,appliedSalvage)) collectiblesDirty=false;
}
bool PaidOfferAvailable(int row,int level,const Masks& checked) {
    return OfferAvailable(row,level,checked) && (!state.shopTiers ||
           ShopTier(row,level)<=UnlockedShopTier(state.sectors));
}
void ReconcileTargets() {
    if(!state.progression) return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for(int id:targetEvents) if(state.destructionChecks.count(id))
            targetsDirty=destroyedTargets.insert(id).second || targetsDirty;
        targetEvents.clear();
    }
    for(int id:destroyedTargets) {
        const auto* target=ApDestruction::Find(id);
        if(state.destructionChecks.count(id) && target && ApProgression::SectorOpen(state.sectors,target->sector))
            targetsDirty=completedTargets.insert(id).second || targetsDirty;
    }
    if(targetsDirty && WriteJournal(purchased,appliedSalvage)) targetsDirty=false;
}
int SectorAt(const float* position) {
    int x=-1,z=-1;
    using GridFn=bool(__cdecl*)(const float*,int*,int*);
    if(!position || !Address<GridFn>(0x0091bb90)(position,&x,&z)) return -1;
    return ApProgression::SectorForGrid(x,z);
}
bool __cdecl FogQuery(const float* position) {
    if(active && state.progression) {
        const int sector=SectorAt(position);
        if(ApProgression::SectorOpen(state.sectors,sector)) return false;
    }
    return originalFogQuery(position);
}
int __stdcall TargetIcon(void* object) {
    if(active && state.progression && object) {
        const auto* bytes=static_cast<const unsigned char*>(object);
        const auto* target=ApDestruction::FindHandle(*reinterpret_cast<const uint32_t*>(bytes+0x6c));
        if(target && state.destructionChecks.count(target->id) && (destroyedTargets.count(target->id) || state.serverTargets.count(target->id))) return -1;
        const int sector=SectorAt(reinterpret_cast<const float*>(bytes+4));
        if(sector>=0 && !ApProgression::SectorOpen(state.sectors,sector)) return -1;
    }
    return originalTargetIcon(object);
}
bool MapMarkerAllowed(const float* position,int icon,uint32_t handle) {
    if(!active || !state.progression || icon==0x26) return true; // safehouses
    if(const auto* marker=ApProgression::FindMarker(handle))
        return ApProgression::SectorOpen(state.sectors,marker->sector);
    // Mission objectives, player, enemies, vehicles and waypoints remain native.
    // These icon families represent gated actions or destruction targets.
    const bool content=(icon>=0xe && icon<=0x1b) || (icon>=0x20 && icon<=0x25) || icon==0x36 || icon==0x37;
    if(!content) return true;
    if(icon==0x20 && handle!=0xffffffffu) {
        auto* table=*Address<unsigned char**>(0x02246c1c);
        int count=*Address<int*>(0x02246c24);
        if(table && count>0 && count<=128) for(int i=0;i<count;++i) {
            auto* record=table+i*0x60;
            if(*reinterpret_cast<uint32_t*>(record+0x10)!=handle) continue;
            if(const auto* m=ApProgression::FindMission(*reinterpret_cast<const char**>(record)))
                return MissionAllowed(*m);
        }
    }
    const int sector=SectorAt(position);
    return sector<0 || ApProgression::SectorOpen(state.sectors,sector);
}
void __fastcall MapPoint(void* map,void*,const float* position,int icon,char selected,int label,void* color,int flags,uint32_t handle) {
    if(MapMarkerAllowed(position,icon,handle)) originalMapPoint(map,position,icon,selected,label,color,flags,handle);
}
void __fastcall MapArea(void* map,void*,const float* position,int icon,char selected,void* size,void* scale,int label,void* color,int flags) {
    if(MapMarkerAllowed(position,icon,0xffffffffu)) originalMapArea(map,position,icon,selected,size,scale,label,color,flags);
}
bool MapObjectAllowed(uint32_t handle,int icon) {
    auto* object=static_cast<unsigned char*>(ProgressionObject(handle));
    return !object || MapMarkerAllowed(reinterpret_cast<float*>(object+4),icon,handle);
}
void __fastcall MapObject(void* map,void*,uint32_t handle,int icon,char selected,char flag,char force) {
    if(active && state.progression && !MapObjectAllowed(handle,icon)) {
        hiddenMarkers[handle]={map,icon,selected,flag};
        originalMapRemove(map,handle);
        return;
    }
    hiddenMarkers.erase(handle);
    originalMapObject(map,handle,icon,selected,flag,force);
}
void __fastcall MapRemove(void* map,void*,uint32_t handle) {
    // A native removal (completion, destruction, unloading) invalidates our
    // stored marker too. It must not reappear when a sector item arrives.
    hiddenMarkers.erase(handle);
    originalMapRemove(map,handle);
}
void ReconcileMap(void* map) {
    if(!active || !state.progression || !map) return;
    // Catch markers created before AP connected. These two native arrays hold
    // persistent object markers; ephemeral points/areas have separate hooks.
    auto* context=static_cast<unsigned char*>(map);
    std::map<uint32_t,HiddenMarker> blocked;
    for(int offset: {0x13388,0x13818}) {
        auto* records=*reinterpret_cast<unsigned char**>(context+offset);
        const int count=*reinterpret_cast<int*>(context+offset+8);
        if(!records || count<0 || count>4096) continue;
        for(int i=0;i<count;++i) {
            auto* record=records+0x18*i;
            auto handle=*reinterpret_cast<uint32_t*>(record);
            int icon=*reinterpret_cast<int*>(record+4);
            if(!MapObjectAllowed(handle,icon)) blocked[handle]={map,icon,static_cast<char>(record[0xd]),static_cast<char>(record[0xc])};
        }
    }
    for(const auto& entry:blocked) {
        hiddenMarkers[entry.first]=entry.second;
        originalMapRemove(map,entry.first);
    }
    for(auto it=hiddenMarkers.begin();it!=hiddenMarkers.end();) {
        const auto handle=it->first;
        const auto marker=it->second;
        if(marker.map!=map || !ProgressionObject(handle) || !MapObjectAllowed(handle,marker.icon)) {++it;continue;}
        it=hiddenMarkers.erase(it);
        originalMapObject(map,handle,marker.icon,marker.selected,marker.flag,0);
    }
}
// The campaign emits some action icons only after discovery or a radio event.
// Fill missing fixed checks on the full map, preserving native points/objectives.
void RevealActivityMap(void* map) {
    if(!active || !state.progression || !map || (*Address<unsigned*>(0x02bd9328)&0x60)!=0x60 ||
       *Address<int*>(0x0165150c)!=-1) return;
    auto* context=static_cast<unsigned char*>(map);
    std::set<int> shown;
    std::set<uint32_t> shownHandles;
    auto addShown=[&](uint32_t handle) {
        shownHandles.insert(handle);
        const auto* marker=ApProgression::FindMarker(handle);
        if(!marker) {
            auto* object=static_cast<unsigned char*>(ProgressionObject(handle));
            if(object && object[0x7e]==0x25) marker=ApProgression::FindMarker(*reinterpret_cast<uint32_t*>(object+0xa0));
        }
        if(marker) shown.insert(ApProgression::ActivityLocation(marker->activity));
    };
    for(int offset:{0x11978,0x13388,0x13818}) {
        auto* records=*reinterpret_cast<unsigned char**>(context+offset);
        const int count=*reinterpret_cast<int*>(context+offset+8);
        if(!records || count<0 || count>4096) continue;
        const int stride=offset==0x11978 ? 0x34 : 0x18,handleOffset=offset==0x11978 ? 0x30 : 0;
        for(int i=0;i<count;++i) addShown(*reinterpret_cast<uint32_t*>(records+i*stride+handleOffset));
    }
    // Faraway chunks may not have instantiated mover objects yet. A known
    // location still has a map pin; absence alone never counts as destruction.
    for(const auto& target:ApDestruction::Targets) {
        if(!state.destructionChecks.count(target.id) || shownHandles.count(target.handle) || destroyedTargets.count(target.id) || state.serverTargets.count(target.id) ||
           !ApProgression::SectorOpen(state.sectors,target.sector) || ProgressionObject(target.handle)) continue;
        originalMapPoint(map,target.position,target.importance==3?0x37:0x36,0,-1,Address<void*>(0x016621a0),0,target.handle);
    }
    for(const auto& point:ApProgression::ActivityMapPoints) {
        const auto* marker=ApProgression::FindMarker(point.handle);
        if(!marker || !ApProgression::SectorOpen(state.sectors,marker->sector)) continue;
        const int id=ApProgression::ActivityLocation(marker->activity);
        if(!id || shown.count(id)) continue;
        shown.insert(id);
        originalMapPoint(map,point.position,point.icon,0,-1,
            Address<void*>(completedActivities.count(id)?0x016621a4:0x016621a0),0,point.handle);
    }
}
void* __cdecl ActivityDistrictAt(const float* position) {
#ifdef APSHOP_NATIVE_TEST
    const auto caller=ApShopTestCaller;
#else
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-Globals::ModuleBase+0x400000;
#endif
    // These exact native call sites only inspect the returned district's C8
    // liberation byte. A private read-only view avoids changing save state.
    const bool availability=caller==0x00a3a43b || caller==0x00a5c34b ||
        caller==0x00a15ae1 || caller==0x00a83fba || caller==0x00651eae;
    if(active && state.progression && availability) {
        const int sector=SectorAt(position);
        if(sector>=0) {
            static const auto blocked=[] {std::array<unsigned char,0xc9> v{};v[0xc8]=1;return v;}();
            if(ApProgression::SectorOpen(state.sectors,sector)) return nullptr;
            return const_cast<unsigned char*>(blocked.data());
        }
    }
    return originalDistrictAt(position);
}
void ReconcileMissions() {
    if(!state.progression) return;
    auto* table=*Address<unsigned char**>(0x02246c1c);
    int count=*Address<int*>(0x02246c24);
    if(!table || count<1 || count>128) return;
    using EnableFn=void(__thiscall*)(void*,bool);
    for(int i=0;i<count;++i) {
        auto* record=table+i*0x60;
        const auto* mission=ApProgression::FindMission(*reinterpret_cast<const char**>(record));
        if(!mission) continue;
        bool allowed=MissionAllowed(*mission);
        // Availability and visibility are separate from completion bit 0.
        record[0x58]=allowed ? record[0x58]|0x40 : record[0x58]&~0x40;
        record[0x21]=0; // late Eos records have a separate hidden flag
        auto handle=*reinterpret_cast<uint32_t*>(record+0x10);
        if(handle==0xffffffffu) continue;
        auto* node=static_cast<unsigned char*>(ProgressionObject(handle));
        if(!node || node[0x7e]!=0x2d) continue;
        if((node[0xa0]!=0)!=allowed)
            Address<EnableFn>(0x00aa9fb0)(node,allowed);
    }
}
const ApProgression::Marker* ProgressionMarker(void* node) {
    if(!active || !state.progression || !node) return nullptr;
    return ApProgression::FindMarker(*reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(node)+0x6c));
}
bool __fastcall HouseReady(void* node,void*) {
    if(const auto* marker=ProgressionMarker(node))
        return ApProgression::SectorOpen(state.sectors,marker->sector) && !(static_cast<unsigned char*>(node)[0xa0]&1);
    return originalHouseReady(node);
}
bool __fastcall RaidReady(void* node,void*) {
    if(const auto* marker=ProgressionMarker(node))
        return ApProgression::SectorOpen(state.sectors,marker->sector) && !(static_cast<unsigned char*>(node)[0xa0]&1)
            && Address<int(__cdecl*)()>(0x0075d610)()<2;
    return originalRaidReady(node);
}
bool __fastcall CollateralReady(void* node,void*) {
    if(const auto* marker=ProgressionMarker(node))
        return ApProgression::SectorOpen(state.sectors,marker->sector) && !(static_cast<unsigned char*>(node)[0xa4]&1);
    return originalCollateralReady(node);
}
bool __fastcall DeliveryLocked(void* node,void*) {
    if(const auto* marker=ProgressionMarker(node)) return !ApProgression::SectorOpen(state.sectors,marker->sector);
    return originalDeliveryLocked(node);
}
bool __fastcall CampGate(void* context) {
    // All four native callers suppress activities after Hammer of the Gods.
    // Do not fake the mission's completion or the global postgame flag.
    return active && state.progression ? false : originalCampGate(context);
}
void __cdecl LiberationMarkerCleanup() {
    // Native 0079A530 removes the action markers in liberated districts.
    // Sector access retains these activities after their capstone mission.
    if(!active || !state.progression) originalLiberationMarkerCleanup();
}
bool __fastcall ActivityStart(void* activity,void*,void* node,void* district) {
    const auto* marker=ProgressionMarker(node);
    if(!marker) return originalActivityStart(activity,node,district);
    if(!ApProgression::SectorOpen(state.sectors,marker->sector)) return false;
    // Liberation normally disallows several fixed action families. Scope the
    // exception to this native start call; preserve real district/save state.
    auto* liberated=district ? static_cast<unsigned char*>(district)+0xc8 : nullptr;
    const unsigned char saved=liberated ? *liberated : 0;
    if(liberated) *liberated=0;
    const bool result=originalActivityStart(activity,node,district);
    if(liberated) *liberated=saved;
    return result;
}
void ReconcileActivities() {
    if(!state.progression) return;
    auto* table=*Address<unsigned char**>(0x0224d02c);
    const int count=*Address<int*>(0x0224d034);
    for(int i=0;table && count>0 && count<=512 && i<count;++i) {
        auto* record=table+i*0x50;
        auto* name=*reinterpret_cast<const char**>(record);
        if(!name) continue;
        for(const auto& marker:ApProgression::Markers) {
            if(std::strcmp(name,marker.activity)!=0) continue;
            bool open=ApProgression::SectorOpen(state.sectors,marker.sector);
            record[0x4c]=open ? record[0x4c]&~2 : record[0x4c]|2;
            // The native value is an EDF-control fraction, not a completion flag.
            *reinterpret_cast<float*>(record+4)=1.0f;
            break;
        }
    }
    for(const auto& marker:ApProgression::Markers) {
        auto* node=static_cast<unsigned char*>(ProgressionObject(marker.handle));
        if(!node) continue;
        bool open=ApProgression::SectorOpen(state.sectors,marker.sector);
        // Read the game's saved completion fields, independently of the old
        // zone-name diagnostics. This covers every fixed action and recovers
        // a success after reconnect/reload without needing a UI log message.
        const int location=ApProgression::ActivityLocation(marker.activity);
        bool complete=false;
        switch(node[0x7e]) {
        case 0x26: case 0x28: complete=(node[0xa0]&1)!=0; break;
        case 0x2a: case 0x2f: complete=(node[0xa4]&1)!=0; break;
        case 0x2b: complete=node[0xa4] && (node[0xac]&2); break;
        case 0x29: {
            auto* info=*reinterpret_cast<unsigned char**>(node+0x9c);
            complete=info && info[0x238]; break;
        }
        }
        if(open && complete && location && !completedActivities.count(location)) {
            completedActivities.insert(location);
            if(!WriteJournal(purchased,appliedSalvage)) completedActivities.erase(location);
            else Logger::Log("AP_ACTIVITY_CHECK_V3|{0}|{1}\n",state.session,location);
        }
        if(node[0x7e]==0x28) node[0xa1]=open ? 0 : 1;
        // Heavy Metal uses a courier endpoint; bit 1 is 'end_game_only'.
        // Keep its completed bit 0 intact.
        if(node[0x7e]==0x2f) node[0xa4]=open ? node[0xa4]&~2 : node[0xa4]|2;
        if(node[0x7e]==0x29 && open) {
            auto* info=*reinterpret_cast<unsigned char**>(node+0x9c);
            if(info) info[0x239]=1; // Demolition Master discovered, not completed
        }
    }
}
void ReconcileSafehouses() {
    if(!state.progression) return;
    struct Safehouse { uint32_t handle; int sector; };
    // Persistent-zone identities, including the later Badlands camp.
    static constexpr Safehouse houses[]={{1191313536u,5},{1241645101u,0},
        {1442971656u,5},{2030305290u,1},{2080440328u,1},{2785280009u,3},
        {3355574278u,2},{3389063173u,2}};
    using FlagFn=void(__thiscall*)(void*,unsigned char);
    for(const auto& house:houses) {
        auto* node=static_cast<unsigned char*>(ProgressionObject(house.handle));
        if(!node || node[0x7e]!=0x30) continue;
        auto& flags=*reinterpret_cast<unsigned*>(node+0xa0);
        node[0x9c]=1; // visible; campaign defaults to discovery on arrival
        // Clear only campaign-disable bits and our own sector bit. Streaming
        // and temporary mission restrictions retain their other flag bits.
        for(unsigned char bit: {0,1,30})
            if(flags&(1u<<bit)) Address<FlagFn>(0x00aabad0)(node,bit);
    }
}
bool __cdecl GunsCompleteForTravel() {
    // Script expression player_completed_GoT queries this function. Override
    // northern road access during free roam AND unrelated missions. Only the
    // actual artillery mission keeps its native combat state. No completion
    // flag, AP check or upgrade row 22 is written.
    if(active && state.progression) {
        const int index=*Address<int*>(0x0165150c);
        auto* table=*Address<unsigned char**>(0x02246c1c);
        const int count=*Address<int*>(0x02246c24);
        if(table && count>0 && count<=128 && index>=0 && index<count) {
            const auto* name=*reinterpret_cast<const char**>(table+index*0x60);
            if(name && std::strcmp(name,"Guns of Tharsis")==0) return originalGunsComplete();
        }
        return true;
    }
    return originalGunsComplete();
}
void ReconcileRoadBarriers() {
    if(!state.progression) return;
    // Vanilla delete_chunk (00b285a0) accepts every class-2 object, without
    // restricting subtype +7f. Match it for these three force-field handles;
    // filtering subtype leaves collision behind after the effect disappears.
    // Reapply after streaming/loading; do not alter mission completion.
    using DeleteChunkFn=bool(__thiscall*)(void*,void*,unsigned char);
    using RemoveEffectFn=void(__thiscall*)(void*,int);
    for(uint32_t handle: {0x53030aaeu,0x5406000bu,0x6306000bu}) {
        auto* body=static_cast<unsigned char*>(ProgressionObject(handle));
        if(!body || body[0x7e]!=2 || (body[0x54]&0x10)) continue;
        if(Address<DeleteChunkFn>(0x0091d070)(Address<void*>(0x02f98490),body,0))
            Logger::Log("AP road force field removed: {0}.\n",handle);
    }
    // aa0000e2 is gp_disappearing_rock_a at the Ultor/Marauder approach.
    // Stop the native hologram effect after every stream/load, independently of missions.
    for(uint32_t handle: {0x53030b84u,0x5406001fu,0x54060004u,0x6306001fu,0xaa0000e2u}) {
        auto* effect=static_cast<unsigned char*>(ProgressionObject(handle));
        if(!effect || effect[0x7e]!=4 || (effect[0x54]&0x10) ||
           *reinterpret_cast<int*>(effect+0xe0)>=1) continue;
        Address<RemoveEffectFn>(0x007465a0)(effect,1);
        if(handle==0xaa0000e2u) Logger::Log("AP Marauder Temple approach hologram disabled.\n");
    }
}
int Price(unsigned char* info,int row,bool paid) {
    if(!paid && OwnedEquip(row)) return 0;
    // The native price function uses its this pointer for price/difficulty, and
    // row only for already-owned cosmetics. Row 1 has no ownership discount.
    return Address<PriceFn>(0x00775840)(info,paid && Cosmetic(row) ? 1 : row,0);
}
Entry MakeEntry(int row,unsigned char* info,bool available,bool paid) {
    auto* def=Definition(row);
    Entry entry{};
    entry.row=row;
    entry.title=*reinterpret_cast<uint32_t*>(info);
    entry.description=*reinterpret_cast<uint32_t*>(info+4);
    entry.icon=*reinterpret_cast<const char**>(def+0x21c);
    entry.blueprint=*reinterpret_cast<const char**>(def+0x220);
    entry.price=paid ? Price(info,row,true) : 0;
    entry.locked=!available;
    auto* metadata=Metadata();
    entry.available=available && metadata && metadata->Salvage>=entry.price;
    entry.isNew=paid;
    return entry;
}
void __cdecl Build() {
    if(!active || rebuilding) { originalBuild(); return; }
    auto* live=Metadata();
    if(!live) { originalBuild(); return; }
    rebuilding=true;
    // Preserve the game's resource loading, then replace only the resulting
    // display records. No native queries are detoured or given a wrong ABI.
    originalBuild();
    auto** nativeEntries=Address<Entry**>(0x016617fc);
    auto* nativeCount=Address<int*>(0x01661804);
    auto* nativeCapacity=Address<int*>(0x01661800);
    std::array<Entry,128> retained{};
    int retainedCount=std::min(std::max(*nativeCount,0),128);
    for(int i=0;i<retainedCount;++i) retained[i]=(*nativeEntries)[i];
    const auto checked=AllChecked();
    entries.fill({}); offerLevels.fill(0);
    int count=0;
    for(int row=0;row<Rows;++row) {
        if(CatalogMask(row)) {
            int next=NextOffer(row,state.enabled,checked);
            if(next) {
                auto* info=Level(row,next);
                bool available=PaidOfferAvailable(row,next,checked);
                if(info && available) {
                    entries[count]=MakeEntry(row,info,available,true);
                    offerLevels[count++]=next;
                }
            }
            // Older seeds have no AP jetpack checks; retain their native offer.
            if(row==0 && !state.enabled[0])
                for(int i=0;i<retainedCount;++i) if(retained[i].row==0) entries[count++]=retained[i];
            // Selecting an AP-owned cosmetic is independent of buying its check.
            // Both entries may coexist; the selection entry always costs zero.
            if(OwnedEquip(row) && live->upgrades[row].current_level==0) {
                auto* info=Level(row,1);
                if(info) entries[count++]=MakeEntry(row,info,true,false);
            }
        } else if(row!=2 && row!=25) {
            for(int i=0;i<retainedCount;++i) if(retained[i].row==row) entries[count++]=retained[i];
        }
    }
    if(!count) {
        // Reuse native no-offer text, never the artwork's placeholder strings.
        Entry empty{}; empty.row=-1; empty.price=-1;
        using CrcFn=uint32_t(__cdecl*)(const char*);
        empty.title=Address<CrcFn>(0x00bef520)("UPG_NO_UPGRADE");
        entries[count++]=empty;
    }
    *nativeEntries=entries.data(); *nativeCapacity=static_cast<int>(entries.size()); *nativeCount=count;
    rebuilding=false;
}
int __fastcall Apply(PlayerMetadata* metadata,void*,int row,char automatic,char freeGrant) {
#ifdef APSHOP_NATIVE_TEST
    const auto caller=ApShopTestCaller;
#else
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
#endif
    if(configured && !active && caller==Globals::ModuleBase+(0x0087f685-0x400000)) return 2;
    if(!active || caller!=Globals::ModuleBase+(0x0087f685-0x400000))
        return originalApply(metadata,row,automatic,freeGrant);
    const int index=*Address<int*>(0x02b9dd18)+*Address<int*>(0x02b9dd1c);
    const int count=*Address<int*>(0x01661804);
    if(!metadata || metadata!=Metadata() || index<0 || index>=count || index>=128 || entries[index].row!=row)
        return 1;
    const int level=offerLevels[index];
    if(!level) {
        if(CatalogMask(row) && !(row==0 && !state.enabled[0]) && !OwnedEquip(row)) return 2;
        if(BackpackRow(row) && OwnedEquip(row)) {
            const int previous=equippedBackpack;
            equippedBackpack=row;
            if(!WriteJournal(purchased,appliedSalvage)) {equippedBackpack=previous;return 2;}
            // Selection is free and independent of the paid AP check. Metadata
            // is applied on the next frame, without native purchase side effects.
            return 5;
        }
        return originalApply(metadata,row,automatic,freeGrant);
    }
    auto* info=Level(row,level);
    if(!info) return 1;
    auto checked=AllChecked();
    const int price=Price(info,row,true);
    auto result=Purchase(row,level,price,
        PaidOfferAvailable(row,level,checked),
        metadata->Salvage,state.enabled,checked,
        [](const Masks& next){return WriteJournal(next,appliedSalvage);});
    if(result==PurchaseResult::Purchased) {
        purchased=checked;
        Logger::Log("AP_SHOP_CHECK_V2|{0}|{1}|{2}|{3}\n",state.session,row,level,price);
        return 5; // native success; caller rebuilds UI and performs its autosave
    }
    if(result==PurchaseResult::StorageFailure)
        Logger::LogError("AP shop purchase refused: purchase journal could not be saved. No salvage charged.\n");
    return result==PurchaseResult::InsufficientFunds ? 3 : 2;
}
bool AuthorizedRegistryFlag(weapon_info* weapon,bool enabled) {
    if(active && weapon) {
        for(int def : {3,4,5,6,7,8,9,10,18,19})
            if(weapon==&Globals::WeaponInfos[def]) enabled=state.weapons[def];
        for(int row=0;row<Rows;++row) {
            int def=BaseDefinition(row);
            if(def>=0 && weapon==&Globals::WeaponInfos[def]) {
                enabled=state.owned[row]>0 || state.weapons[def];
                break;
            }
        }
        auto* repair=Definition(39);
        if(repair && weapon==*reinterpret_cast<weapon_info**>(repair+0x218)) enabled=state.owned[39]>0;
    }
    return enabled;
}
bool PickupAllowed(void* human,void* object) {
    if(!active || human!=lastPlayer || !object) return true;
    auto* bytes=static_cast<unsigned char*>(object);
    // A dropped weapon is a native item object (type 1, subtype 7).
    // Scripted inventory grants and NPC loadouts do not use this policy.
    if(bytes[0x7e]!=1 || bytes[0x7f]!=7) return true;
    return AuthorizedRegistryFlag(*reinterpret_cast<weapon_info**>(bytes+0x148),true);
}
bool __cdecl PickupReady(void* human,void* object,char flag) {
    return PickupAllowed(human,object) && originalPickupReady(human,object,flag);
}
void* __cdecl PickupObject(void* human,void* object,char a,char b,char c) {
    if(!PickupAllowed(human,object)) return nullptr;
    return originalPickupObject(human,object,a,b,c);
}
void __cdecl SwapObject(void* human,uint32_t handle,int slot) {
    // Reject before the original function removes the player's current weapon.
    if(handle!=0xffffffffu && !PickupAllowed(human,ProgressionObject(handle))) return;
    originalSwapObject(human,handle,slot);
}
void ShowNotice() {
    const auto now=GetTickCount64();
    if(now<nextNoticeTime) return;
    std::lock_guard<std::mutex> lock(mutex);
    if(notices.empty()) return;
    const auto& text=notices.front().text;
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(length<=0) {notices.pop_front();return;}
    std::wstring wide(length,L' ');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),&wide[0],length);
    // Native secondary HUD text: the game's own font, layout and animation.
    using MessageFn=int(__cdecl*)(const wchar_t*,float,bool,bool);
    if(Address<MessageFn>(0x008d8270)(wide.c_str(),4.0f,false,true)!=-1) {
        notices.pop_front();nextNoticeTime=now+5500;
    }
}
void FlushStories() {
    for(auto it=pendingStories.begin();it!=pendingStories.end();) {
        const int id=*it;
        if(completedStories.count(id)) {it=pendingStories.erase(it);continue;}
        completedStories.insert(id);
        if(!WriteJournal(purchased,appliedSalvage)) {
            completedStories.erase(id);
            break;
        }
        Logger::Log("AP_STORY_CHECK_V2|{0}|{1}\n",state.session,id);
        it=pendingStories.erase(it);
    }
}
void RecordStory(const char* internal,uintptr_t returnVa) {
    if(!active || !ApStory::GameplayCompletion(returnVa)) return;
    if(state.progression) {
        const auto* mission=ApProgression::FindMission(internal);
        if(!mission || !MissionAllowed(*mission)) return;
    }
    const auto* mission=ApStory::Find(internal);
    if(mission && !completedStories.count(mission->location)) pendingStories.insert(mission->location);
    // Persist at completion, before a final cutscene/menu can stop player frames.
    // If storage fails, the next player frame retries the pending record.
    FlushStories();
}
void __fastcall CompleteMission(void* record,void*) {
#ifdef APSHOP_NATIVE_TEST
    const auto caller=ApShopTestCaller;
#else
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
#endif
    // The native function receives the actual mission record in ECX. It sets
    // completion bit 0, including cutscene missions, independently of UI names.
    originalComplete(record);
    if(!active || !record) return;
    auto* table=*Address<unsigned char**>(0x02246c1c);
    int count=*Address<int*>(0x02246c24);
    auto start=reinterpret_cast<uintptr_t>(table), ptr=reinterpret_cast<uintptr_t>(record);
    if(!table || count<1 || count>128 || ptr<start || ptr>=start+count*0x60u || (ptr-start)%0x60u) return;
    if((static_cast<unsigned char*>(record)[0x58]&1)==0) return;
    RecordStory(*static_cast<const char**>(record),caller-Globals::ModuleBase+0x400000);
}
void __cdecl RegisterWeapon(weapon_info* weapon,bool enabled) {
    enabled=AuthorizedRegistryFlag(weapon,enabled);
    originalRegister(weapon,enabled);
}
bool __cdecl RegistryQuery(weapon_info* weapon,bool enabled) {
    return originalRegistryQuery(weapon,AuthorizedRegistryFlag(weapon,enabled));
}
void __cdecl Cabinet() {
    originalCabinet();
    if(active) for(int def=0;def<96;++def) if(state.weapons[def])
        originalRegister(&Globals::WeaponInfos[def],true);
}
unsigned __fastcall AmmoLimit(void* weapon) {
    const unsigned limit=originalAmmoLimit(weapon);
    if(!active || !state.features060 || !weapon || !lastPlayer ||
       *Address<unsigned char*>(0x02feb588) || Address<unsigned char(__cdecl*)()>(0x00760440)()) return limit;
    auto* bytes=static_cast<unsigned char*>(weapon);
    const auto owner=*reinterpret_cast<uint32_t*>(bytes+0x13c);
    if(ProgressionObject(owner)!=lastPlayer) return limit;
    const auto* info=*reinterpret_cast<weapon_info**>(bytes+0x148);
    for(int def=3;def<=18;++def) if(info==&Globals::WeaponInfos[def]) {
        const unsigned magazine=Address<unsigned(__fastcall*)(void*)>(0x00bdcee0)(weapon);
        const unsigned expanded=EquipmentTuning::TotalAmmo(limit,magazine,state.ammo[def]);
        static std::array<int,96> loggedLevels{};
        if(loggedLevels[def]!=state.ammo[def]) {
            loggedLevels[def]=state.ammo[def];
            Logger::Log("AP ammo: weapon {}, tier {}/5, magazine {}, reserve {} -> {}.\n",
                def,state.ammo[def],magazine,limit>magazine?limit-magazine:0,expanded>magazine?expanded-magazine:0);
        }
        return expanded;
    }
    return limit;
}
void __fastcall Restock(void* human) {
    const bool local=active && state.features060 && human==lastPlayer;
    if(local) ++restockDepth;
    originalRestock(human);
    if(local) --restockDepth;
}
int __fastcall AddAmmo(void* weapon,void*,int amount) {
    // Expand successful crate interactions and vanilla-sized full refills.
    // Small pickups, reloads and NPCs keep native amounts.
    if(amount>0 && weapon) {
        const unsigned nativeLimit=originalAmmoLimit(weapon),limit=AmmoLimit(weapon);
        const unsigned magazine=Address<unsigned(__fastcall*)(void*)>(0x00bdcee0)(weapon);
        // Safehouse/locker refills can pass an inlined vanilla amount without
        // going through the crate interaction. Expand full-refill requests too.
        const bool fullRefill=nativeLimit>magazine &&
            static_cast<unsigned>(amount)>=nativeLimit-magazine;
        if((restockDepth || fullRefill) && limit>nativeLimit && limit<=65535) {
            auto* bytes=static_cast<unsigned char*>(weapon);
            const int loaded=*reinterpret_cast<int*>(bytes+0x20c);
            const int reserve=*reinterpret_cast<int*>(bytes+0x210);
            if(loaded>=0 && reserve>=0) amount=std::max(amount,static_cast<int>(limit)-loaded-reserve);
        }
    }
    return originalAmmoAdd(weapon,amount);
}
void* TunedTremor(void* info,int power) {
    static std::array<EquipmentTuning::ExplosionInfo,6> tuned;
    if(!info || power<=0) return info;
    for(auto& value:tuned) if(info==value.data()) return info;
    EquipmentTuning::ExplosionInfo base;
    std::memcpy(base.data(),info,base.size());
    auto& copy=tuned[EquipmentTuning::Level(power)];
    EquipmentTuning::Tremor(base,copy,power);
    return copy.data();
}
void __cdecl BackpackExplosion(void* info,void* source,void* owner,void* position,void* orientation,void* direction,void* weapon,bool fromServer) {
#ifndef APSHOP_NATIVE_TEST
    if(!fromServer) info=Gunship::TuneExplosion(info,owner,weapon);
#endif
    // 00958350 supplies the local owner for Tremor's mp_quake explosions.
    // Each power tier retains its own definition for queued native explosions.
    if(active && state.features060 && state.backpackPower>0 && lastPlayer && owner==lastPlayer && info &&
       !fromServer && !*Address<unsigned char*>(0x02feb588) && !Address<unsigned char(__cdecl*)()>(0x00760440)() &&
       std::memcmp(info,"mp_quake",9)==0) {
        info=TunedTremor(info,state.backpackPower);
    }
    originalExplosion(info,source,owner,position,orientation,direction,weapon,fromServer);
}
void ReconcileBackpackTuning(Player* player) {
    if(!state.features060) return;
    // Only the local player's equipped instance uses the copy. Native global
    // definitions, NPC backpacks, animation and resource pointers stay intact.
    static std::array<EquipmentTuning::PackInfo,10> tuned;
    auto* pack=*reinterpret_cast<unsigned char**>(reinterpret_cast<unsigned char*>(player)+0x1170);
    if(!pack || *reinterpret_cast<Player**>(pack+0x274)!=player) return;
    auto*& info=*reinterpret_cast<unsigned char**>(pack+0x270);
    auto** table=Address<unsigned char**>(0x02fe8d18);
    for(int type=0;type<10;++type) {
        if(info!=table[type] && info!=tuned[type].data()) continue;
        auto* native=table[type];
        if(!native || *reinterpret_cast<int*>(native+0x50)!=type) return;
        if(!state.features060 || *Address<unsigned char*>(0x02feb588) ||
           Address<unsigned char(__cdecl*)()>(0x00760440)()) { info=native;return; }
        EquipmentTuning::PackInfo base;
        std::memcpy(base.data(),native,base.size());
        EquipmentTuning::Backpack(base,tuned[type],type,state.backpackRecharge,state.backpackPower);
        if(type==9 && state.backpackPower>0) {
            auto* explosion=EquipmentTuning::Read<void*>(base,0x24c);
            if(explosion) EquipmentTuning::Write(tuned[type],0x24c,TunedTremor(explosion,state.backpackPower));
        }
        static unsigned char* loggedPack=nullptr;
        static int loggedType=-1,loggedRecharge=-1,loggedPower=-1;
        if(pack!=loggedPack || type!=loggedType || state.backpackRecharge!=loggedRecharge || state.backpackPower!=loggedPower) {
            Logger::Log("AP backpack: type {}, recharge {}/5, power {}/5; native instance tuned.\n",type,state.backpackRecharge,state.backpackPower);
            loggedPack=pack;loggedType=type;loggedRecharge=state.backpackRecharge;loggedPower=state.backpackPower;
        }
        info=tuned[type].data();return;
    }
}
bool LocalBackpack(void* object) {
    return active && state.features060 && lastPlayer && object &&
        *reinterpret_cast<Player**>(static_cast<unsigned char*>(object)+0x274)==lastPlayer &&
        *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(lastPlayer)+0x1170)==object &&
        !*Address<unsigned char*>(0x02feb588) && !Address<unsigned char(__cdecl*)()>(0x00760440)();
}
void RefillInactiveBackpack(void* object) {
    if(state.backpackRecharge<5 || !LocalBackpack(object)) return;
    auto* bytes=static_cast<unsigned char*>(object);
    // Never refill an active ability or an airborne jetpack. Do not toggle
    // activity/animation/timer flags; the native update remains authoritative.
    if(bytes[0x254] || (*reinterpret_cast<unsigned*>(reinterpret_cast<unsigned char*>(lastPlayer)+0xdcc)&0x100)) return;
    *reinterpret_cast<float*>(bytes+0x25c)=1.0f;
    auto* info=*reinterpret_cast<unsigned char**>(bytes+0x270);
    if(info && *reinterpret_cast<int*>(info+0x50)==0)
        *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(lastPlayer)+0xdbc)=1.0f;
}
float __cdecl CampaignBackpackRecharge() {
    const float base=originalCampaignRecharge();
    return LocalBackpack(updatingBackpack)?base*EquipmentTuning::RechargeMultiplier(state.backpackRecharge):base;
}
void __fastcall BackpackUpdate(void* object,void*) {
    if(LocalBackpack(object)) {ReconcileBackpackTuning(lastPlayer);RefillInactiveBackpack(object);}
    void* previous=updatingBackpack;updatingBackpack=object;
    originalBackpackUpdate(object);
    updatingBackpack=previous;
    RefillInactiveBackpack(object);
}
void __fastcall BackpackActivate(void* object,void*) {
    if(LocalBackpack(object)) {ReconcileBackpackTuning(lastPlayer);RefillInactiveBackpack(object);}
    originalBackpackActivate(object);
}
float __cdecl CampaignJetpackDrain(Player* human) {
    const float base=*Address<float*>(0x01694480);
    if(human!=lastPlayer || !human) return base;
    auto* pack=*reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(human)+0x1170);
    return LocalBackpack(pack)?base/EquipmentTuning::Strength(state.backpackPower):base;
}
// Replace ONLY the campaign drain load at 00a9e83b. ESI is the human.
// Preserve flags, GPRs and all other XMM registers. MOVSS's original memory
// form clears XMM0's upper lanes; the replacement does the same. No globals
// are changed and the remaining native physics/fuel calculation runs as-is.
__declspec(naked) void JetpackDrainInstruction() {
    __asm {
        pushfd
        pushad
        sub esp,128
        movups [esp+16],xmm1
        movups [esp+32],xmm2
        movups [esp+48],xmm3
        movups [esp+64],xmm4
        movups [esp+80],xmm5
        movups [esp+96],xmm6
        movups [esp+112],xmm7
        push esi
        call CampaignJetpackDrain
        add esp,4
        fstp dword ptr [esp]
        movss xmm0,dword ptr [esp]
        movups xmm1,[esp+16]
        movups xmm2,[esp+32]
        movups xmm3,[esp+48]
        movups xmm4,[esp+64]
        movups xmm5,[esp+80]
        movups xmm6,[esp+96]
        movups xmm7,[esp+112]
        add esp,128
        popad
        popfd
        jmp dword ptr [jetpackDrainContinue]
    }
}
void Reconcile(Player* player,bool force) {
    auto* metadata=Metadata();
    if(!metadata || metadata!=&player->Metadata) return;
    bool refresh=force;
    const int selected=equippedBackpack>=0 && OwnedEquip(equippedBackpack) ? equippedBackpack : -1;
    for(int row=0;row<Rows;++row) {
        if(row==0 && !state.enabled[0]) continue; // old seed compatibility
        if(!CatalogMask(row) && row!=25) continue;
        if(BackpackRow(row)) {
            metadata->upgrades[row].current_level=static_cast<char>(row==selected ? state.owned[row] : 0);
            if(state.owned[row]) metadata->upgrades[row].availability_bitfield|=2;
            Globals::ApGrantedUpgradeLevels[row]=state.owned[row];
            Globals::ApGrantedUpgradeRows[row]=state.owned[row]>0;
            continue;
        }
        if(Cosmetic(row)) {
            if(state.owned[row]) metadata->upgrades[row].availability_bitfield |= 2;
            if(!state.owned[row] && metadata->upgrades[row].current_level!=0)
                metadata->upgrades[row].current_level=0;
            continue;
        }
        int target=state.owned[row];
#ifndef APSHOP_NATIVE_TEST
        if(Garage::PlacementTravelUnlocked && row==29) target=1;
#endif
        int before=static_cast<unsigned char>(metadata->upgrades[row].current_level);
        if(before!=target) {
            if(target>before) {
                metadata->upgrades[row].current_level=static_cast<char>(target-1);
                metadata->upgrades[row].availability_bitfield|=static_cast<unsigned short>(1u<<target);
                // Capacity is applied directly below, including before weapon
                // ownership. Receiving it is not a native shop purchase and
                // must not dispatch purchase script events during level load.
                if(row!=1) originalApply(metadata,row,0,1);
            }
            metadata->upgrades[row].current_level=static_cast<char>(target);
            if(row==26 && target<before) {
                using ArmorFn=void(__cdecl*)(int,Player*);
                Address<ArmorFn>(0x00ab3350)(target,player);
            }
            refresh=true;
        }
        Globals::ApGrantedUpgradeLevels[row]=target;
        Globals::ApGrantedUpgradeRows[row]=target>0;
    }
    if(Globals::RfgMaxCharges) *Globals::RfgMaxCharges=2+state.owned[1];
    unsigned short backpackMask=0;
    unsigned int hammerMask=1; // default hammer remains a local selection
    for(int row=0;row<Rows;++row) {
        if(!OwnedCosmetic(row)) continue;
        if(row>=53) backpackMask|=static_cast<unsigned short>(1u<<(row-53));
        else {
            int bit=row==20 ? 1 : row==21 ? 2 : row-37;
            hammerMask|=1u<<bit;
        }
    }
    *Address<unsigned short*>(0x03017c50)=backpackMask;
    if(state.enabled[0]) *Address<unsigned char*>(0x03017c52)=static_cast<unsigned char>(state.owned[0]);
    *Address<unsigned int*>(0x030266b0)=hammerMask;
    *Address<unsigned int*>(0x030266b4)=0;
    if(state.enabled[0]) {
        // Native backpack removal clears the attachment, effects and player
        // pointer. Clearing upgrade levels alone leaves the model on Mason.
        auto* pack=*reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(player)+0x1170);
        if(selected<0 && pack) {
            using RemovePackFn=void(__thiscall*)(void*,char);
            Address<RemovePackFn>(0x009df480)(pack,1);
        }
        // The original player frame equips the selected metadata row. Reset
        // its cached type after reload/removal so it can recreate the model.
        if(force || selected<0) *Address<int*>(0x01694490)=-1;
    }
    if(refresh) {
        originalCabinet();
        for(int def=0;def<96;++def) if(state.weapons[def])
            originalRegister(&Globals::WeaponInfos[def],true);
    }
}
}
bool Active() { return active.load(); }
bool VehicleUnlocked(int item) {
    return active && state.features060 && (state.vehicles.count(item) ||
        (item==867531428 && state.gunshipPurchase && gunshipPurchased));
}
bool VehiclePurchasable(int item) {
    return active && state.features060 && item==867531428 && state.gunshipPurchase && !VehicleUnlocked(item);
}
int GarageSalvage() {return active && lastPlayer ? lastPlayer->Metadata.Salvage : 0;}
int GaragePrice(int item,int spawnPrice) {
    if(VehiclePurchasable(item)) return state.gunshipCost;
    if(!VehicleUnlocked(item)) return -1;
    return state.vehicleCosts ? spawnPrice : 0;
}
void GarageCancel() {
    const auto payment=garagePayment;garagePayment={};
    if(payment.player && ProgressionObject(payment.handle)==payment.player)
        payment.player->Metadata.Salvage+=payment.price;
}
bool GarageReserve(int item,int spawnPrice) {
    if(garagePayment.player || !active || journalFailed || !lastPlayer) return false;
    const int price=GaragePrice(item,spawnPrice);
    if(price<0 || GarageSalvage()<price) return false;
    garagePayment={lastPlayer,*reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(lastPlayer)+0x6c),
                   price,VehiclePurchasable(item)};
    lastPlayer->Metadata.Salvage-=price;
    return true;
}
bool GarageCommit() {
    if(!garagePayment.player) return false;
    if(ProgressionObject(garagePayment.handle)!=garagePayment.player) {GarageCancel();return false;}
    if(garagePayment.unlock) {
        gunshipPurchased=true;
        if(!WriteJournal(purchased,appliedSalvage)) {
            gunshipPurchased=false;GarageCancel();return false;
        }
    }
    Logger::Log("Garage: confirmed spawn, charged {} salvage, permanent gunship unlock {}.\n",garagePayment.price,garagePayment.unlock);
    garagePayment={};return true;
}
bool AcceptSnapshot(const std::string& payload,std::string& error) {
    try {
        auto j=Json::parse(payload);
        if(j.at("protocol")!=2 && j.at("protocol")!=3) throw std::runtime_error("unsupported RF:G protocol");
        Snapshot next;
        std::vector<Notice> incomingNotices;
        if(j.find("notices")!=j.end()) {
            if(!j["notices"].is_array() || j["notices"].size()>32) throw std::runtime_error("invalid HUD message batch");
            for(const auto& n:j["notices"]) {
                Notice notice{n.at("id").get<std::string>(),n.at("text").get<std::string>()};
                if(notice.id.empty() || notice.id.size()>96 || notice.text.empty() || notice.text.size()>768)
                    throw std::runtime_error("invalid HUD message");
                // Native text interprets markup. AP names must stay plain text.
                for(auto& ch:notice.text) if(ch=='[' || ch==']' || static_cast<unsigned char>(ch)<32) ch=' ';
                incomingNotices.push_back(std::move(notice));
            }
        }
        if(j.at("protocol")==3) {
            const auto& p=j.at("progression");
            next.progressionVersion=p.at("version").get<int>();
            if(next.progressionVersion!=1 && next.progressionVersion!=2) throw std::runtime_error("unsupported progression protocol");
            next.progression=true;
            next.sectors=p.at("sectors").get<unsigned>();
            next.storyRequired=p.at("required").get<int>();
            next.serverStories=p.at("stories").get<std::set<int>>();
            if(!(next.sectors&1) || (next.sectors&~127u) || next.storyRequired<0 || next.storyRequired>20)
                throw std::runtime_error("invalid sector progression state");
            for(int id:next.serverStories) if(id<867530200 || id>867530221) throw std::runtime_error("invalid story history");
        }
        const int features=j.value("features_version",0);
        if(features!=0 && features!=6) throw std::runtime_error("unsupported feature protocol");
        next.features060=features==6;
        next.vehicleCosts=j.value("vehicle_spawn_costs",false);
        next.gunshipPurchase=j.value("gunship_purchase",false);
        if(j.find("gunship_purchase_cost")!=j.end() && !j["gunship_purchase_cost"].is_number_integer())
            throw std::runtime_error("invalid gunship price");
        next.gunshipCost=j.value("gunship_purchase_cost",1000);
        if(next.gunshipCost<0 || next.gunshipCost>30000 ||
           (!next.features060 && (next.vehicleCosts || next.gunshipPurchase)))
            throw std::runtime_error("invalid garage settings");
        if(next.features060) {
            if(!next.progression) throw std::runtime_error("0.6.0 requires sector progression");
            next.shopTiers=j.at("shop_tiers").get<bool>();
            next.vehicles=j.at("vehicles").get<std::set<int>>();
            for(int id:next.vehicles) if(!ApVehicles::Valid(id)) throw std::runtime_error("invalid vehicle");
            next.ammo=j.at("ammo").get<std::array<int,96>>();
            for(int def=0;def<96;++def) if(next.ammo[def]<0 || next.ammo[def]>5 ||
                (next.ammo[def] && (def<3 || def>18))) throw std::runtime_error("invalid ammo extension");
            next.backpackRecharge=j.at("backpack_recharge").get<int>();
            next.backpackPower=j.at("backpack_power").get<int>();
            if(next.backpackRecharge<0 || next.backpackRecharge>5 || next.backpackPower<0 || next.backpackPower>5)
                throw std::runtime_error("invalid backpack extension");
            next.collectibleChecks=j.at("collectible_checks").get<std::set<int>>();
            for(int id:next.collectibleChecks) if(!ApCollectibles::Find(id)) throw std::runtime_error("invalid collectible");
        }
        next.destructionChecks=j.value("destruction_checks",std::set<int>{});
        if(!next.destructionChecks.empty() && !next.progression) throw std::runtime_error("targets require sector progression");
        for(int id:next.destructionChecks) if(!ApDestruction::Find(id)) throw std::runtime_error("invalid destruction check");
        next.serverTargets=j.value("checked_targets",std::set<int>{});
        for(int id:next.serverTargets) if(!next.destructionChecks.count(id)) throw std::runtime_error("invalid target history");
        next.session=j.at("session").get<std::string>();
        if(next.session.size()!=64 || next.session.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("invalid seed/slot identity");
        next.enabled=j.at("enabled").get<Masks>(); next.checked=j.at("checked").get<Masks>();
        next.owned=j.at("owned").get<std::array<int,Rows>>();
        next.weapons=j.at("weapons").get<std::array<bool,96>>();
        next.startDefinition=j.at("start_definition").get<int>(); next.startRow=j.at("start_row").get<int>();
        const bool upgradeStart=next.startRow>=0 && next.startRow<Rows && next.startDefinition>=0 &&
            BaseDefinition(next.startRow)==next.startDefinition;
        const bool registryStart=next.startRow==-1 &&
            ((next.startDefinition>=3 && next.startDefinition<=10) || next.startDefinition==18 || next.startDefinition==19);
        const bool repairStart=next.startRow==39 && next.startDefinition==-1;
        if(!upgradeStart && !registryStart && !repairStart) throw std::runtime_error("invalid starting weapon");
        for(int row=0;row<Rows;++row) {
            if((next.enabled[row] & ~CatalogMask(row)) || (next.checked[row] & ~next.enabled[row]) ||
               next.owned[row]<0 || next.owned[row]>MaxLevel(row)) throw std::runtime_error("invalid row state");
        }
        next.shopRewardVisibility=j.value("shop_reward_visibility",true);
        if(next.shopRewardVisibility && j.find("shop_rewards")!=j.end()) {
            const auto& rewards=j.at("shop_rewards");
            if(!rewards.is_array() || rewards.size()>66) throw std::runtime_error("invalid shop rewards");
            for(const auto& reward:rewards) {
                const int row=reward.at("row").get<int>(),level=reward.at("level").get<int>();
                auto text=reward.at("text").get<std::string>();
                if(row<0 || row>=Rows || level<1 || level>MaxLevel(row) || !(next.enabled[row]&(1u<<level)) ||
                   text.empty() || text.size()>768 || next.shopRewards.count(row*16+level))
                    throw std::runtime_error("invalid shop reward");
                for(auto& ch:text) if(ch=='[' || ch==']' || static_cast<unsigned char>(ch)<32) ch=' ';
                const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
                if(length<=0) throw std::runtime_error("invalid shop reward text");
                std::wstring wide(length,L' ');
                MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),&wide[0],length);
                next.shopRewards.emplace(row*16+level,std::move(wide));
            }
        }
        for(auto it=j.at("salvage").begin();it!=j.at("salvage").end();++it) {
            int index=std::stoi(it.key()),amount=it.value().get<int>();
            if(index<0 || amount<=0 || amount>30000) throw std::runtime_error("invalid salvage receipt");
            next.salvage[index]=amount;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if(!sessionKey.empty() && sessionKey!=next.session) throw std::runtime_error("restart RF:G before changing seed or slot");
        if(!state.session.empty() && (state.progression!=next.progression || (state.progression &&
           (state.storyRequired!=next.storyRequired || state.progressionVersion!=next.progressionVersion))))
            throw std::runtime_error("progression rules changed within a seed");
        if(!state.session.empty() && (state.vehicleCosts!=next.vehicleCosts ||
            state.gunshipPurchase!=next.gunshipPurchase || state.gunshipCost!=next.gunshipCost))
            throw std::runtime_error("garage rules changed within a seed");
        if(payload==lastPayload) return installed;
        if(!installed) throw std::runtime_error("native shopsanity hooks unavailable");
        for(auto& n:incomingNotices) if(seenNotices.insert(n.id).second) {
            // A large AP catch-up must never delay gameplay ownership updates.
            if(notices.size()>=256) notices.pop_front();
            notices.push_back(std::move(n));
        }
        // The seed's intro loadout must be known before a Player frame exists.
        Globals::ApStartingWeaponDefinition=next.startDefinition;
        Globals::ApStartingWeaponUpgrade=next.startRow;
        if(next.startDefinition>=0) Globals::ApGrantedWeaponDefinitions[next.startDefinition]=true;
        if(next.startRow>=0) Globals::ApGrantedUpgradeRows[next.startRow]=true;
        Globals::ApStartingWeaponConfigured=true;
        sessionKey=next.session;
        configured=true;
        lastPayload=payload;
        pending=std::move(next); hasPending=true;
        return installed;
    } catch(const std::exception& ex) { error=ex.what(); return false; }
}
void Frame(Player* player) {
    if(!player || !installed) return;
    bool changed=false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(hasPending) {
            changed=true;
            bool first=state.session.empty();
            state=std::move(pending); hasPending=false;
            if(first && !LoadJournal()) {
                journalFailed=true;
                Logger::LogError("AP shopsanity journal failed validation or could not be written. Shop purchases are blocked; original journal preserved.\n");
            }
        }
    }
    if(state.session.empty() || journalFailed) return;
    bool reload=lastPlayer!=player || (lastPlayTime>=0 && player->Metadata.PlayTime<lastPlayTime);
    lastPlayer=player; lastPlayTime=player->Metadata.PlayTime;
    if(changed) {
        Globals::ApStartingWeaponDefinition=state.startDefinition;
        Globals::ApStartingWeaponUpgrade=state.startRow;
        Globals::ApStartingWeaponConfigured=true;
        for(int def=0;def<96;++def) Globals::ApGrantedWeaponDefinitions[def]=state.weapons[def];
    }
    active=true;
    Reconcile(player,changed || reload);
    ReconcileBackpackTuning(player);
    FlushStories();
    AnnounceFinale();
    ReconcileMissions();
    ReconcileActivities();
    ReconcileTargets();
    ReconcileCollectibles(reload);
    ReconcileSafehouses();
    ReconcileRoadBarriers();
    ShowNotice();
    if(changed || reload) Logger::Log("AP SHOP V2 READY: session={0}, capacity={1}, salvage={2}, story_missions={3}/{4}.\n",
        state.session,2+state.owned[1],player->Metadata.Salvage,StoryCount(),state.storyRequired);
    for(const auto& item:state.salvage) {
        if(appliedSalvage.count(item.first)) continue;
        auto next=appliedSalvage; next.insert(item);
        if(!WriteJournal(purchased,next)) break;
        appliedSalvage=std::move(next);
        player->Metadata.Salvage=std::min(30000,player->Metadata.Salvage+item.second);
        Logger::Log("AP salvage receipt applied: index={0}, amount={1}.\n",item.first,item.second);
    }
    const auto now=GetTickCount64();
    if(now-lastReport>=2000) {
        lastReport=now;
        for(int row=0;row<Rows;++row) for(int level=1;level<=MaxLevel(row);++level)
            if((purchased[row] & state.enabled[row] & ~state.checked[row]) & (1u<<level))
                Logger::Log("AP_SHOP_CHECK_V2|{0}|{1}|{2}|0\n",state.session,row,level);
    }
}
bool Install(IHookManager& hooks) {
    const unsigned char drainLoad[]={0xf3,0x0f,0x10,0x05};
    uintptr_t drainAddress=0;
    std::memcpy(&drainAddress,Address<void*>(0x00a9e83f),sizeof(drainAddress));
    if(std::memcmp(Address<void*>(0x00a9e83b),drainLoad,sizeof(drainLoad))!=0 ||
       drainAddress!=reinterpret_cast<uintptr_t>(Address<void*>(0x01694480))) {
        Logger::LogError("AP backpack hooks unavailable: unexpected campaign Jetpack drain instruction.\n");return false;
    }
    // Validate the actual purchase call site, not an RVA guessed from a UI label.
    const unsigned char expected[]={0xe8,0x1b,0x45,0x24,0x00};
    if(std::memcmp(Address<void*>(0x0087f680),expected,sizeof(expected))!=0) {
        Logger::LogError("AP shopsanity disabled: unsupported shop purchase call site.\n"); return false;
    }
    const unsigned char completePrologue[]={0x51,0x56,0x57,0x8b,0xf1};
    if(std::memcmp(Address<void*>(0x007abc10),completePrologue,sizeof(completePrologue))!=0) {
        Logger::LogError("AP story completion hook unavailable: unexpected native function.\n"); return false;
    }
    const unsigned char availablePrologue[]={0xf6,0x41,0x58,0x40};
    if(std::memcmp(Address<void*>(0x0075eb70),availablePrologue,sizeof(availablePrologue))!=0) {
        Logger::LogError("AP progression disabled: unexpected mission availability function.\n"); return false;
    }
    installed=hooks.CreateHook("APAmmoCapacityV6",static_cast<DWORD>(Globals::ModuleBase+0x7dcf60),AmmoLimit,originalAmmoLimit);
    installed=installed && hooks.CreateHook("APAmmoBoxRefillV6",static_cast<DWORD>(Globals::ModuleBase+0x6cd590),Restock,originalRestock)
        && hooks.CreateHook("APAmmoRestockAmountV6",static_cast<DWORD>(Globals::ModuleBase+0x7e56b0),AddAmmo,originalAmmoAdd);
    installed=installed && hooks.CreateHook("APBackpackExplosionV6",static_cast<DWORD>(Globals::ModuleBase+0x2ec720),BackpackExplosion,originalExplosion);
    jetpackDrainContinue=Address<void*>(0x00a9e843);
    installed=installed && hooks.CreateHook("APBackpackUpdate",static_cast<DWORD>(Globals::ModuleBase+0x5f2a70),BackpackUpdate,originalBackpackUpdate)
        && hooks.CreateHook("APBackpackActivate",static_cast<DWORD>(Globals::ModuleBase+0x5f2300),BackpackActivate,originalBackpackActivate)
        && hooks.CreateHook("APCampaignRecharge",static_cast<DWORD>(Globals::ModuleBase+0x375dd0),CampaignBackpackRecharge,originalCampaignRecharge)
        && hooks.CreateHook("APCampaignJetpackDrain",static_cast<DWORD>(Globals::ModuleBase+0x69e83b),JetpackDrainInstruction,originalJetpackDrainInstruction);
    installed=installed && hooks.CreateHook("APShopBuildV2",static_cast<DWORD>(Globals::ModuleBase+0x47c950),Build,originalBuild)
        && hooks.CreateHook("APShopPurchaseV2",static_cast<DWORD>(Globals::ModuleBase+0x6c3ba0),Apply,originalApply)
        && hooks.CreateHook("APShopCabinetV2",static_cast<DWORD>(Globals::ModuleBase+0x45b420),Cabinet,originalCabinet)
        && hooks.CreateHook("APShopRegisterV2",static_cast<DWORD>(Globals::ModuleBase+0x477200),RegisterWeapon,originalRegister)
        && hooks.CreateHook("APShopRegistryQueryV2",static_cast<DWORD>(Globals::ModuleBase+0x45b3b0),RegistryQuery,originalRegistryQuery)
        && hooks.CreateHook("APStoryCompleteV2",static_cast<DWORD>(Globals::ModuleBase+0x3abc10),CompleteMission,originalComplete)
        && hooks.CreateHook("APMissionAvailabilityV3",static_cast<DWORD>(Globals::ModuleBase+0x35eb70),MissionAvailable,originalMissionAvailable);
    installed=installed
        && hooks.CreateHook("APHouseAvailabilityV3",static_cast<DWORD>(Globals::ModuleBase+0x618230),HouseReady,originalHouseReady)
        && hooks.CreateHook("APRaidAvailabilityV3",static_cast<DWORD>(Globals::ModuleBase+0x61b070),RaidReady,originalRaidReady)
        && hooks.CreateHook("APCollateralAvailabilityV3",static_cast<DWORD>(Globals::ModuleBase+0x61bd40),CollateralReady,originalCollateralReady)
        && hooks.CreateHook("APDeliveryAvailabilityV3",static_cast<DWORD>(Globals::ModuleBase+0x6179e0),DeliveryLocked,originalDeliveryLocked)
        && hooks.CreateHook("APActivityStartV3",static_cast<DWORD>(Globals::ModuleBase+0x3a0410),ActivityStart,originalActivityStart)
        && hooks.CreateHook("APCampActivityGateV3",static_cast<DWORD>(Globals::ModuleBase+0x38eba0),CampGate,originalCampGate);
    installed=installed
        && hooks.CreateHook("APPickupReadyV3",static_cast<DWORD>(Globals::ModuleBase+0x690e80),PickupReady,originalPickupReady)
        && hooks.CreateHook("APPickupObjectV3",static_cast<DWORD>(Globals::ModuleBase+0x6b56b0),PickupObject,originalPickupObject)
        && hooks.CreateHook("APSwapObjectV3",static_cast<DWORD>(Globals::ModuleBase+0x6cdab0),SwapObject,originalSwapObject);
    installed=installed && hooks.CreateHook("APActivityDistrictV3",static_cast<DWORD>(Globals::ModuleBase+0x38f3c0),ActivityDistrictAt,originalDistrictAt);
    installed=installed && hooks.CreateHook("APKeepActivityMarkersV3",static_cast<DWORD>(Globals::ModuleBase+0x39a530),LiberationMarkerCleanup,originalLiberationMarkerCleanup);
    installed=installed && hooks.CreateHook("APArtilleryTravelV3",static_cast<DWORD>(Globals::ModuleBase+0x376180),GunsCompleteForTravel,originalGunsComplete);
    installed=installed && hooks.CreateHook("APMapPointV3",static_cast<DWORD>(Globals::ModuleBase+0x473fd0),MapPoint,originalMapPoint);
    installed=installed && hooks.CreateHook("APMapAreaV3",static_cast<DWORD>(Globals::ModuleBase+0x4741b0),MapArea,originalMapArea);
    installed=installed && hooks.CreateHook("APMapTrackerV3",static_cast<DWORD>(Globals::ModuleBase+0x481a10),MapRender,originalMapRender);
    installed=installed && hooks.CreateHook("APMiniMapV3",static_cast<DWORD>(Globals::ModuleBase+0x482130),MiniMapRender,originalMiniMapRender);
    installed=installed && hooks.CreateHook("APFullMapMenuV4",static_cast<DWORD>(Globals::ModuleBase+0x476c10),FullMapMenuRender,originalFullMapMenuRender);
    installed=installed && hooks.CreateHook("APMapActivityNamesV4",static_cast<DWORD>(Globals::ModuleBase+0x065280),EncodeMapTitle,originalEncodeText);
    installed=installed && hooks.CreateHook("APShopRewardDescriptionV4",static_cast<DWORD>(Globals::ModuleBase+0x075b10),SetShopDescription,originalSetUiValue);
    installed=installed && hooks.CreateHook("APShopRewardLocalizationV6",static_cast<DWORD>(Globals::ModuleBase+0x4bd690),LookupShopText,originalLookupText);
    installed=installed && hooks.CreateHook("APMapObjectV3",static_cast<DWORD>(Globals::ModuleBase+0x479ad0),MapObject,originalMapObject);
    installed=installed && hooks.CreateHook("APMapRemoveV3",static_cast<DWORD>(Globals::ModuleBase+0x474380),MapRemove,originalMapRemove);
    installed=installed && hooks.CreateHook("APTargetDestroyedV5",static_cast<DWORD>(Globals::ModuleBase+0x316e70),TargetDestroyed,originalTargetDestroyed);
    installed=installed && hooks.CreateHook("APSectorDiscoveryV5",static_cast<DWORD>(Globals::ModuleBase+0x43faf0),FogQuery,originalFogQuery);
    installed=installed && hooks.CreateHook("APTargetVisibilityV5",static_cast<DWORD>(Globals::ModuleBase+0x462d30),TargetIcon,originalTargetIcon);
    Logger::Log("AP shopsanity v2 native transaction hooks installed: {0}.\n",installed);
    return installed;
}
}

