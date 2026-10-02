#pragma once
#include <string>
class IHookManager;
struct Player;
namespace ApShop {
bool Install(IHookManager& hooks);
bool AcceptSnapshot(const std::string& payload, std::string& error);
void Frame(Player* player);
bool Active();
bool VehicleUnlocked(int item);
bool VehiclePurchasable(int item);
int GarageSalvage();
int GaragePrice(int item,int spawnPrice);
bool GarageReserve(int item,int spawnPrice);
bool GarageCommit();
void GarageCancel();
}
