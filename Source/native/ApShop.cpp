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
#include "ApShopState.h"
#include "ApStoryCatalog.h"
#include "ApProgressionCatalog.h"
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
    int startDefinition = -1, startRow = -1;
    bool progression = false;
    int progressionVersion = 0;
    unsigned sectors = 1;
    int storyRequired = 20;
    std::set<int> serverStories;
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
std::array<Entry, 128> entries{};
std::array<int, 128> offerLevels{}; // zero is a vanilla/local equip action
std::string journalPath;
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
using MapObjectFn=void(__thiscall*)(void*,uint32_t,int,char,char,char);
using MapRemoveFn=void(__thiscall*)(void*,uint32_t);
MapObjectFn originalMapObject=nullptr;
MapRemoveFn originalMapRemove=nullptr;
struct HiddenMarker { void* map; int icon; char selected,flag; };
std::map<uint32_t,HiddenMarker> hiddenMarkers;
void ReconcileMap(void* map);
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
    purchased.fill(0); appliedSalvage.clear(); completedStories.clear(); completedActivities.clear();
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
    ReconcileMap(map);
    originalMapRender(map);
    if(!active || !state.progression) return;
    // 00882130 is the full map draw (00881A10 is the minimap). Draw after
    // its viewport reset, using the same wide-string font API as map labels.
    const auto text=StoryTrackerText();
    const auto* screen=Address<int*(__cdecl*)()>(0x0089bdb0)();
    const int font=*Address<int*>(0x0163f164);
    if(screen[0]<=0 || screen[1]<=0 || font<0) return;
    const int width=Address<int(__cdecl*)(const wchar_t*,int)>(0x008b4a80)(text.c_str(),font);
    float scale=*Address<float*>(0x01661264);
    if(scale<=0 || width<=0) return;
    scale=std::min(scale,screen[0]*0.9f/width);
    const int x=static_cast<int>((screen[0]-width*scale)*0.5f),y=static_cast<int>(screen[1]*0.88f);
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
int SectorAt(const float* position) {
    int x=-1,z=-1;
    using GridFn=bool(__cdecl*)(const float*,int*,int*);
    if(!position || !Address<GridFn>(0x0091bb90)(position,&x,&z)) return -1;
    return ApProgression::SectorForGrid(x,z);
}
bool MapMarkerAllowed(const float* position,int icon,uint32_t handle) {
    if(!active || !state.progression || icon==0x26) return true; // safehouses
    if(const auto* marker=ApProgression::FindMarker(handle))
        return ApProgression::SectorOpen(state.sectors,marker->sector);
    // Mission objectives, player, enemies, vehicles and waypoints remain native.
    // These icon families represent gated actions or destruction targets.
    const bool content=(icon>=0xe && icon<=0x1b) || (icon>=0x20 && icon<=0x25);
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
    if(!table || count<1 || count>512) return;
    for(int i=0;i<count;++i) {
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
    // only free-roam access to the northern road; never award a mission check
    // or change upgrade row 22. Missions retain their real artillery state.
    if(active && state.progression &&
       *Address<int*>(0x0165150c)==-1) return true;
    return originalGunsComplete();
}
void ReconcileRoadBarriers() {
    if(!state.progression) return;
    // These exact chunk/effect pairs are lowered by the vanilla Assault power
    // core scripts and Final Mission's FORCE_FIELD_lower script. The old build
    // removed only collision and left the blue energy effects running. Use
    // both native script operations: delete_chunk (00b285a0) and remove_effect
    // (00b28170), including effect/audio teardown, without touching generators,
    // mission objectives or completion. Reapply after streaming or loading.
    using DeleteChunkFn=bool(__thiscall*)(void*,void*,unsigned char);
    using RemoveEffectFn=void(__thiscall*)(void*,int);
    for(uint32_t handle: {0x53030aaeu,0x5406000bu,0x6306000bu}) {
        auto* body=static_cast<unsigned char*>(ProgressionObject(handle));
        if(!body || body[0x7e]!=2 || body[0x7f]!=1 || (body[0x54]&0x10)) continue;
        if(Address<DeleteChunkFn>(0x0091d070)(Address<void*>(0x02f98490),body,0))
            Logger::Log("AP road force field removed: {0}.\n",handle);
    }
    for(uint32_t handle: {0x53030b84u,0x5406001fu,0x54060004u,0x6306001fu}) {
        auto* effect=static_cast<unsigned char*>(ProgressionObject(handle));
        if(!effect || effect[0x7e]!=4 || (effect[0x54]&0x10) ||
           *reinterpret_cast<int*>(effect+0xe0)>=1) continue;
        Address<RemoveEffectFn>(0x007465a0)(effect,1);
    }
}
int Price(unsigned char* info,int row,bool paid) {
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
                bool available=OfferAvailable(row,next,checked);
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
            if(OwnedCosmetic(row) && live->upgrades[row].current_level==0) {
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
        if(CatalogMask(row) && !(row==0 && !state.enabled[0]) && !OwnedCosmetic(row)) return 2;
        return originalApply(metadata,row,automatic,freeGrant);
    }
    auto* info=Level(row,level);
    if(!info) return 1;
    auto checked=AllChecked();
    const int price=Price(info,row,true);
    auto result=Purchase(row,level,price,
        OfferAvailable(row,level,checked),
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
void Reconcile(Player* player,bool force) {
    auto* metadata=Metadata();
    if(!metadata || metadata!=&player->Metadata) return;
    bool refresh=force;
    for(int row=0;row<Rows;++row) {
        if(row==0 && !state.enabled[0]) continue; // old seed compatibility
        if(!CatalogMask(row) && row!=25) continue;
        if(Cosmetic(row)) {
            if(state.owned[row]) metadata->upgrades[row].availability_bitfield |= 2;
            if(!state.owned[row] && metadata->upgrades[row].current_level!=0)
                metadata->upgrades[row].current_level=0;
            continue;
        }
        int target=state.owned[row];
        int before=static_cast<unsigned char>(metadata->upgrades[row].current_level);
        if(before!=target) {
            if(target>before) {
                metadata->upgrades[row].current_level=static_cast<char>(target-1);
                metadata->upgrades[row].availability_bitfield|=static_cast<unsigned short>(1u<<target);
                originalApply(metadata,row,0,1);
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
    *Address<unsigned int*>(0x030266b0)=hammerMask;
    *Address<unsigned int*>(0x030266b4)=0;
    if(refresh) {
        originalCabinet();
        for(int def=0;def<96;++def) if(state.weapons[def])
            originalRegister(&Globals::WeaponInfos[def],true);
    }
}
}
bool Active() { return active.load(); }
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
        next.session=j.at("session").get<std::string>();
        if(next.session.size()!=64 || next.session.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("invalid seed/slot identity");
        next.enabled=j.at("enabled").get<Masks>(); next.checked=j.at("checked").get<Masks>();
        next.owned=j.at("owned").get<std::array<int,Rows>>();
        next.weapons=j.at("weapons").get<std::array<bool,96>>();
        next.startDefinition=j.at("start_definition").get<int>(); next.startRow=j.at("start_row").get<int>();
        if(next.startDefinition<0 || next.startDefinition>=96 || next.startRow<0 || next.startRow>=Rows ||
           BaseDefinition(next.startRow)!=next.startDefinition) throw std::runtime_error("invalid starting weapon");
        for(int row=0;row<Rows;++row) {
            if((next.enabled[row] & ~CatalogMask(row)) || (next.checked[row] & ~next.enabled[row]) ||
               next.owned[row]<0 || next.owned[row]>MaxLevel(row)) throw std::runtime_error("invalid row state");
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
        Globals::ApGrantedWeaponDefinitions[next.startDefinition]=true;
        Globals::ApGrantedUpgradeRows[next.startRow]=true;
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
    FlushStories();
    AnnounceFinale();
    ReconcileMissions();
    ReconcileActivities();
    ReconcileSafehouses();
    ReconcileRoadBarriers();
    ShowNotice();
    if(changed || reload) Logger::Log("AP SHOP V2 READY: session={0}, capacity={1}, salvage={2}.\n",
        state.session,2+state.owned[1],player->Metadata.Salvage);
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
    installed=hooks.CreateHook("APShopBuildV2",static_cast<DWORD>(Globals::ModuleBase+0x47c950),Build,originalBuild)
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
    installed=installed && hooks.CreateHook("APMapTrackerV3",static_cast<DWORD>(Globals::ModuleBase+0x482130),MapRender,originalMapRender);
    installed=installed && hooks.CreateHook("APMiniMapV3",static_cast<DWORD>(Globals::ModuleBase+0x481a10),MiniMapRender,originalMiniMapRender);
    installed=installed && hooks.CreateHook("APMapObjectV3",static_cast<DWORD>(Globals::ModuleBase+0x479ad0),MapObject,originalMapObject);
    installed=installed && hooks.CreateHook("APMapRemoveV3",static_cast<DWORD>(Globals::ModuleBase+0x474380),MapRemove,originalMapRemove);
    Logger::Log("AP shopsanity v2 native transaction hooks installed: {0}.\n",installed);
    return installed;
}
}

