#pragma once
#include <cstdint>
class IHookManager;
struct Player;
namespace Garage {
inline constexpr bool PlacementTravelUnlocked=false;
bool Install(IHookManager&);
void Frame(Player*);
bool LookupText(uint32_t, const wchar_t**);
}
