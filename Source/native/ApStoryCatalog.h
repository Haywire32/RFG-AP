#pragma once
#include <cstring>
#include <cstdint>
namespace ApStory {
struct Mission { int location; const char* internal; const char* title; };
inline constexpr Mission Catalog[] = {
    {867530200, "Tutorial", "Welcome To Mars"},
    {867530201, "Intro 1", "Better Red Than Dead"},
    {867530202, "Intro 2", "Ambush"},
    {867530203, "We know where you are", "Start Your Engines"},
    {867530204, "Friends, Martians, Countrymen", "Rallying Point"},
    {867530205, "Walker, Martian Ranger", "Industrial Revolution"},
    {867530206, "PartyTime", "Ultor Echo"},
    {867530207, "Death From Above", "Ashes To Ashes..."},
    {867530208, "Refugee Truck", "Emergency Response"},
    {867530209, "Highway To Hell", "Catch And Release"},
    {867530210, "Start Your Engines", "Air Traffic Control"},
    {867530211, "Traffic Jam", "Access Denied"},
    {867530212, "Tank Attack", "Blitzkrieg"},
    {867530213, "Guns of Tharsis", "The Guns Of Tharsis"},
    {867530214, "Death By Committee", "Death By Committee"},
    {867530215, "Sniper Hunter", "The Dogs Of War"},
    {867530216, "Save the Guerrilla Camp", "Hammer Of The Gods"},
    {867530217, "Marauder Temple", "Marauder Temple"},
    {867530218, "Emergency Broadcast System", "Emergency Broadcast System"},
    {867530219, "Ants Vs Magnifying Glass", "Manual Override"},
    {867530220, "Assault the EDF Central Command", "Guerrillas At The Gates"},
    {867530221, "Final Mission", "Mars Attacks"},
};
inline const Mission* Find(const char* name) {
    if(name) for(const auto& mission:Catalog) if(std::strcmp(mission.internal,name)==0) return &mission;
    return nullptr;
}
// Both verified gameplay-success callers; exclude restoration/debug callers.
inline bool GameplayCompletion(uintptr_t returnVa) { return returnVa==0x007b4b23 || returnVa==0x007b4d82; }
}
