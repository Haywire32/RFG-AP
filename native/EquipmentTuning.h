#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace EquipmentTuning {
inline int Level(int value) { return std::clamp(value,0,5); }
inline float Strength(int value) {return 1.0f+0.25f*Level(value);}
// Tier five is handled as an immediate inactive refill, not division by zero.
inline float RechargeMultiplier(int value) {return 1.0f/(1.0f-0.2f*std::min(Level(value),4));}
inline unsigned Ammo(unsigned nativeLimit,int level) {
    if(nativeLimit>65535) return nativeLimit; // Native unlimited-ammo modes.
    const unsigned tiers=static_cast<unsigned>(Level(level));
    if(!nativeLimit) return 0;
    return std::min(65535u,nativeLimit+std::max(tiers,(nativeLimit*tiers+3u)/4u));
}
inline unsigned TotalAmmo(unsigned total,unsigned magazine,int level) {
    if(total>65535 || magazine>=total) return total;
    return std::min(65535u,magazine+Ammo(total-magazine,level));
}
// Native backpack definition stride, verified in 009a59c0 and 009affd0.
using PackInfo=std::array<unsigned char,0x70c>;
template<class T,size_t N> T Read(const std::array<unsigned char,N>& info,size_t offset) { T value;std::memcpy(&value,info.data()+offset,sizeof(T));return value; }
template<class T,size_t N> void Write(std::array<unsigned char,N>& info,size_t offset,T value) {std::memcpy(info.data()+offset,&value,sizeof(T));}
template<size_t N> void Scale(std::array<unsigned char,N>& target,const std::array<unsigned char,N>& source,size_t offset,float factor) {
    const float base=Read<float>(source,offset);
    if(std::isfinite(base) && base>=0) Write(target,offset,base*factor);
}
inline void Backpack(const PackInfo& source,PackInfo& target,int type,int recharge,int power) {
    target=source;
    const float strength=Strength(power);
    Scale(target,source,0x6f8,RechargeMultiplier(recharge));
    switch(type) {
    case 0: case 3: case 4: case 8: // Jetpack, Stealth, Vision, Rhino: duration.
        Scale(target,source,0x6f4,1.0f/strength);break;
    case 1: // Thrust: a modest boost, without changing fall protection.
        Scale(target,source,0x150+0x50,1.0f+0.10f*Level(power));break;
    case 2: { // Fleetfoot: scale the bonus, not the base walking speed.
        const float speed=Read<float>(source,0x154);
        if(std::isfinite(speed) && speed>=1.0f) Write(target,0x154,1.0f+(speed-1.0f)*strength);
        break;
    }
    case 5: // Firepower.
        Scale(target,source,0x15c,strength);Scale(target,source,0x160,strength);break;
    case 7: // Heal: healing per second.
        Scale(target,source,0x150+0x98,strength);break;
    case 6: // Concussion: the native ripple impulse is an integer, not a float.
        Write(target,0x1e0,static_cast<int>(Read<int>(source,0x1e0)*strength));
        Scale(target,source,0x150+0x8c,strength);
        Scale(target,source,0x150+0x80,std::sqrt(strength));break;
    }
}
// Native explosion_info: stride 0xf0, as used by 006e2150.
using ExplosionInfo=std::array<unsigned char,0xf0>;
inline void Tremor(const ExplosionInfo& source,ExplosionInfo& target,int power) {
    target=source;
    const float strength=1.0f+0.25f*Level(power);
    Scale(target,source,0x2c,std::sqrt(strength)); // damage radius
    Scale(target,source,0x30,std::sqrt(strength)); // structural crumble radius
    Scale(target,source,0x5c,strength); // impulse
    Write(target,0x60,static_cast<int>(Read<int>(source,0x60)*strength));
}
}
