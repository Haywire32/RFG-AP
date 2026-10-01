#pragma once
#include <cstdint>
namespace GaragePolicy {
constexpr uint32_t None=0xffffffffu;
inline bool Replaceable(uint32_t owner,uint32_t node,uint32_t handle,uint32_t tracked,
                        uint32_t flags,float distanceSquared,bool deleting) {
    // Only this garage's unoccupied, non-mission vehicle still in its bay.
    return !deleting && distanceSquared<=144.0f &&
        (owner==node || (handle==tracked && tracked!=None)) &&
        !(flags & ((1u<<29)|(1u<<24)|(1u<<8)|0xfu));
}
inline bool TimedOut(uint64_t now,uint64_t start) {return now-start>=20000;}
}
