#pragma once
#include <string>
class IHookManager;
struct Player;
namespace ApShop {
bool Install(IHookManager& hooks);
bool AcceptSnapshot(const std::string& payload, std::string& error);
void Frame(Player* player);
bool Active();
}
