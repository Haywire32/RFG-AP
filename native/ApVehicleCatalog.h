#pragma once
#include <cstring>
namespace ApVehicles {
struct Vehicle { const char* family; int id; };
inline constexpr Vehicle Vehicles[] = {
    {"Artillery tank",867531400},
    {"Bus",867531401},
    {"Combat walker",867531402},
    {"Dump truck",867531403},
    {"EDF APC",867531404},
    {"EDF Scout",867531405},
    {"EDF staff car",867531406},
    {"EDF supply",867531407},
    {"Emergency rover",867531408},
    {"Flatbed",867531409},
    {"Fuel tanker",867531410},
    {"Garbage truck",867531411},
    {"Heavy tank",867531412},
    {"Heavy walker",867531413},
    {"Jetter",867531414},
    {"Light pickup",867531415},
    {"Light walker",867531416},
    {"Luxury SUV",867531417},
    {"Luxury coupe",867531418},
    {"Marauder Jetter",867531419},
    {"Marauder Raider",867531420},
    {"Medium tank",867531421},
    {"Mini hauler",867531422},
    {"Mining ATV",867531423},
    {"Mining rover",867531424},
    {"Supercar",867531425},
    {"Supply truck",867531426},
    {"Taxi",867531427},
};
inline int Find(const char* name) { for(const auto& v:Vehicles) if(name && std::strcmp(v.family,name)==0) return v.id; return 0; }
inline bool Valid(int id) { for(const auto& v:Vehicles) if(v.id==id) return true; return false; }
}
