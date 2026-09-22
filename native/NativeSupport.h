#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <unknwn.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>
#define FMT_HEADER_ONLY 1
#include <fmt/format.h>
#include <MinHook.h>

namespace fs=std::filesystem;

// Minimal layouts used by the AP adapter. Offsets are for the supported x86
// Steam executable. No game resources, RSL runtime or scripting engine are linked.
struct UpgradeItem {
    char current_level;
    unsigned short availability_bitfield,unlocked_notified_bitfield,new_notified_bitfield;
};
struct PlayerMetadata {
    void* vtable;
    int Salvage,MiningCount,SupplyCrateCount;
    unsigned int DistrictHash;
    int DistrictTime;
    UpgradeItem upgrades[128];
    int PlayTime,LastDeathTime;
};
struct Player { unsigned char beforeMetadata[10616]; PlayerMetadata Metadata; };
struct inv_item_info { const char* name; };
struct weapon_info {
    unsigned char beforeInventoryInfo[24];
    inv_item_info* weapon_inv_item_info;
    unsigned char remaining[532];
};
static_assert(sizeof(void*)==4,"RF:G requires an x86 DLL");
static_assert(sizeof(UpgradeItem)==8);
static_assert(sizeof(PlayerMetadata)==1056);
static_assert(offsetof(Player,Metadata)==10616);
static_assert(offsetof(PlayerMetadata,PlayTime)==1048);
static_assert(sizeof(weapon_info)==560);
static_assert(offsetof(weapon_info,weapon_inv_item_info)==24);

namespace Globals {
    extern uintptr_t ModuleBase;
    extern int* RfgMaxCharges;
    extern int ApStartingWeaponDefinition,ApStartingWeaponUpgrade;
    extern std::atomic<bool> ApStartingWeaponConfigured;
    extern std::atomic<bool> ApGrantedWeaponDefinitions[96];
    extern std::atomic<bool> ApGrantedUpgradeRows[62];
    extern std::atomic<int> ApGrantedUpgradeLevels[62];
    struct WeaponTable { weapon_info& operator[](size_t index) const; };
    extern WeaponTable WeaponInfos;
    std::string GetEXEPath(bool ignored=false);
}
namespace Logger {
    void Write(const std::string& text);
    template<class... Args> void Log(const char* format,const Args&... args) {
        Write(fmt::format(format,args...));
    }
    template<class... Args> void LogError(const char* format,const Args&... args) {
        Write("ERROR: "+fmt::format(format,args...));
    }
}
// Create every hook before enabling any. Failure rolls the complete set back;
// the vanilla game and system DirectInput continue to work.
class IHookManager {
public:
    template<class T,class U> bool CreateHook(const char* name,DWORD address,T& detour,U& original) {
        const auto status=MH_CreateHook(reinterpret_cast<void*>(address),reinterpret_cast<void*>(detour),reinterpret_cast<void**>(&original));
        if(status!=MH_OK) Logger::LogError("Cannot create {}: {}\n",name,MH_StatusToString(status));
        return status==MH_OK;
    }
};
namespace rfg { void* upgrade_info_get(unsigned int row); }

