// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <map>
#include <string>
namespace camelot
{
/// Native decoded item metadata indexed by classic inventory slot.
struct InventoryItem
{
    uint8_t slot = 0, level = 0, value1 = 0, value2 = 0, hand = 0, type = 0;
    uint8_t condition = 0, durability = 0, quality = 0, bonus = 0, extension = 0;
    uint16_t weight = 0, model = 0, color = 0, effect = 0;
    std::string name; // Server display name already includes stack count.
    /// Returns the reported stack count from the item record.
    uint16_t Count() const
    {
        return type == 0 || type == 43 || type == 44 || type == 46 ? value1 : type == 16 ? value2
                                                                                         : 1;
    }
};
/// Native revisioned owned inventory and serialized detail-request state.
struct InventoryState
{
    std::map<uint8_t, InventoryItem> items;
    uint64_t revision = 0;
    bool equipmentKnown = false, backpackKnown = false;
    int detailSlot = -1;
    std::string detailText;
};
/// Reports whether the classic slot belongs to owned equipment.
inline bool EquipmentSlot(int slot)
{
    return (slot >= 10 && slot <= 17) || (slot >= 21 && slot <= 29) || (slot >= 32 && slot <= 37);
}
/// Reports whether the classic slot belongs to owned equipment or the backpack.
inline bool OwnedItemSlot(int slot)
{
    return EquipmentSlot(slot) || (slot >= 40 && slot <= 79);
}
} // namespace camelot
