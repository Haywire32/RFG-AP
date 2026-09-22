#pragma once
#include "NativeSupport.h"

// The intro's default inventory and the initial Parker script grants are the
// only exceptions. The same script dispatcher is used by later missions, so its
// caller address alone is deliberately not enough to suppress a weapon.
struct LoadoutPolicy {
    void* introPlayer=nullptr;
    unsigned int openingGrantMask=0;
    inv_item_info* Choose(void* human,inv_item_info* info,int slot,uintptr_t caller,
                         bool configured,inv_item_info* starting,bool& suppress,int ammo=-1,
                         inv_item_info* defaultHammer=nullptr) {
        suppress=false;
        if(!info || !info->name) return info;
        const bool golden=std::strcmp(info->name,"golden_hammer")==0;
        if(caller==0x00adafd8 && slot==3 && (golden || std::strcmp(info->name,"sledgehammer")==0)) {
            if(introPlayer!=human) {introPlayer=human;openingGrantMask=0;}
            // A vanilla profile reward must not select an AP cosmetic at spawn.
            if(configured && golden && defaultHammer) return defaultHammer;
        }
        if(!configured || human!=introPlayer) return info;
        if(caller==0x00adafd8 && std::strcmp(info->name,"charge_placer")==0 && starting)
            return starting;
        if(caller==0x00b21f05 && ammo==-1) {
            const unsigned int bit=std::strcmp(info->name,"charge_placer")==0 ? 1 : std::strcmp(info->name,"m16")==0 ? 2 : 0;
            if(bit && !(openingGrantMask&bit)) {
                openingGrantMask|=bit;suppress=true;
            }
        }
        return info;
    }
};
