#pragma once
#include <array>
#include <cstdint>
#include <algorithm>

// Pure state rules. Paid locations never change an ownership level.
namespace ApShop {
constexpr int Rows = 62;
using Masks = std::array<uint16_t, Rows>;
inline bool Cosmetic(int row) { return row == 20 || row == 21 || (row >= 40 && row < Rows); }
inline int MaxLevel(int row) {
    if (row == 1) return 10;
    if (row == 0 || row == 2 || row == 3 || row == 6 || row == 9 || row == 11 || row == 14 || row == 15 || row == 26) return 2;
    return 1;
}
inline uint16_t CatalogMask(int row) {
    if (row == 0) return 6; // standard jetpack and recharge
    if (row == 1) return 0x7fe;
    if (row == 2) return 4; // free Remote Charges are never an AP location
    if ((row >= 3 && row <= 18) || row == 20 || row == 21 || row == 26 ||
        (row >= 28 && row <= 31) || (row >= 39 && row <= 61))
        return MaxLevel(row) == 2 ? 6 : 2;
    return 0;
}
inline bool OfferAvailable(int row,int level,const Masks& checked) {
    int parent=-1;
    switch(row) {
    case 4:case 5:parent=3;break;
    case 7:case 8:parent=6;break;
    case 10:parent=9;break;
    case 12:case 13:parent=11;break;
    case 16:parent=15;break;
    }
    if(parent>=0 && !(checked[parent]&2)) return false;
    // Sequential levels within one row are handled by NextOffer/Purchase.
    return row>=0 && row<Rows && level>0 && level<16 && (CatalogMask(row)&(1u<<level));
}
inline bool IsCheck(int row, int level) {
    return row >= 0 && row < Rows && level > 0 && level < 16 &&
        (CatalogMask(row) & (1u << level)) != 0;
}
inline int NextOffer(int row, const Masks& enabled, const Masks& checked) {
    if (row < 0 || row >= Rows) return 0;
    const auto remaining = enabled[row] & CatalogMask(row) & ~checked[row];
    for (int level = 1; level <= MaxLevel(row); ++level)
        if (remaining & (1u << level)) return level;
    return 0;
}
enum class PurchaseResult { Purchased, Invalid, Duplicate, Unavailable, InsufficientFunds, StorageFailure };
template<class Persist>
PurchaseResult Purchase(int row, int level, int price, bool available, int& salvage,
                        const Masks& enabled, Masks& checked, Persist persist) {
    if (!IsCheck(row, level) || price < 0 || !(enabled[row] & (1u << level))) return PurchaseResult::Invalid;
    if (checked[row] & (1u << level)) return PurchaseResult::Duplicate;
    if (NextOffer(row, enabled, checked) != level || !available) return PurchaseResult::Unavailable;
    if (salvage < price) return PurchaseResult::InsufficientFunds;
    Masks next = checked;
    next[row] |= static_cast<uint16_t>(1u << level);
    if (!persist(next)) return PurchaseResult::StorageFailure;
    checked = next;
    salvage -= price;
    return PurchaseResult::Purchased;
}
}
