#pragma once
#include <cstdint>
class IHookManager;
struct Player;
namespace Gunship {
bool Install(IHookManager&);
#ifdef GARAGE_TEST
inline void Track(void*) {}
inline void Forget(void*,uint32_t) {}
inline void Reset() {}
#else
void Track(void* vehicle);
void Forget(void* vehicle,uint32_t handle);
void Reset();
#endif
void Frame(Player*);
void RenderHud();
const wchar_t* ControlHint(const wchar_t*);
void* TuneExplosion(void* info,void* owner,void* weapon);
}
