// SPDX-License-Identifier: GPL-2.0-or-later
#include "CamelotInventory.h"
#include "CamelotClient.h"
#include <algorithm>
#include <vector>
namespace camelot
{
namespace
{
uint16_t Word(const Bytes &b, size_t p)
{
    return uint16_t((b[p] << 8) | b[p + 1]);
}
void Put(Bytes &b, uint16_t n)
{
    b.push_back(uint8_t(n >> 8));
    b.push_back(uint8_t(n));
}
} // namespace
void Client::ResetInventory()
{
    const auto revision = inventory_.revision + 1;
    inventory_ = {};
    inventory_.revision = revision;
    pendingItemDetailName_.clear();
    pendingItemDetailRevision_ = 0;
    pendingItemDetailExpires_ = 0;
}
bool Client::HandleInventory(uint8_t op, const Bytes &b, double now)
{
    if (op == 0xc4 && now <= pendingItemDetailExpires_ && !pendingItemDetailName_.empty())
    {
        size_t p = 0;
        std::string caption, text;
        const auto pascal = [&](std::string &result) {
            if (p >= b.size())
                return false;
            const size_t length = b[p++];
            if (length > b.size() - p)
                return false;
            result.assign(b.begin() + p, b.begin() + p + length);
            p += length;
            return true;
        };
        if (!pascal(caption) || caption != pendingItemDetailName_)
            return true;
        if (inventory_.revision != pendingItemDetailRevision_)
        {
            pendingItemDetailName_.clear();
            return true;
        }
        bool terminated = false;
        while (p < b.size())
        {
            if (!b[p++])
            {
                terminated = true;
                break;
            }
            std::string line;
            if (!pascal(line) || text.size() + line.size() + 1 > 8192)
                return true;
            text += line + '\n';
        }
        if (terminated)
        {
            inventory_.detailText = std::move(text);
            pendingItemDetailName_.clear();
        }
        return true;
    }
    if (op != 0x02)
        return false;
    if (b.size() < 4)
    {
        Fail("Truncated inventory header");
        return true;
    }
    const size_t fixed = settings_.version >= 72 ? 18 : 17; // 1.72 adds armor extension, before dye/emblem.
    size_t p = 4;
    std::vector<InventoryItem> updates;
    for (int n = 0; n < b[0]; ++n)
    {
        if (p >= b.size() || b.size() - p - 1 < fixed + 1)
        {
            Fail("Truncated inventory item");
            return true;
        }
        InventoryItem i;
        i.slot = b[p++];
        i.level = b[p];
        i.value1 = b[p + 1];
        i.value2 = b[p + 2];
        i.hand = b[p + 3] >> 6;
        i.type = b[p + 4] & 63;
        i.weight = Word(b, p + 5);
        i.condition = b[p + 7];
        i.durability = b[p + 8];
        i.quality = b[p + 9];
        i.bonus = b[p + 10];
        i.model = Word(b, p + 11);
        size_t q = p + 13;
        if (settings_.version >= 72)
            i.extension = b[q++];
        i.color = Word(b, q);
        i.effect = Word(b, q + 2);
        p += fixed;
        const size_t length = b[p++];
        if (length > b.size() - p)
        {
            Fail("Truncated inventory name");
            return true;
        }
        i.name.assign(b.begin() + p, b.begin() + p + length);
        p += length;
        updates.push_back(std::move(i));
    }
    // Storage windows use overlapping client slot IDs: never treat them as owned
    // equipment/backpack.
    if (b[3] > 2)
        return true;
    auto player = players_.find(objectId_);
    auto &appearance = player != players_.end() ? player->second.appearance : selectedAppearance_;
    bool changed = b[3] == 1 || appearance.hood != (b[1] & 1) || appearance.rightWeapon != (b[2] & 15) || appearance.leftWeapon != (b[2] >> 4);
    if (b[3] == 1)
        appearance.equipment.clear();
    for (const auto &item : updates)
        if ((item.slot >= 10 && item.slot <= 13) || (item.slot >= 21 && item.slot <= 28 && item.slot != 24))
        {
            changed = true;
            appearance.equipment.erase(std::remove_if(appearance.equipment.begin(), appearance.equipment.end(), [&](const Equipment &e) { return e.slot == item.slot; }), appearance.equipment.end());
            if (item.model & 8191)
                appearance.equipment.push_back({item.slot, uint16_t(item.model & 8191), item.color, item.effect, item.extension});
        }
    appearance.hood = b[1] & 1;
    appearance.rightWeapon = b[2] & 15;
    appearance.leftWeapon = b[2] >> 4;
    if (changed)
    {
        appearance.equipmentKnown = true;
        ++appearance.revision;
    }
    if (b[3] == 1 || b[3] == 2)
    {
        for (auto it = inventory_.items.begin(); it != inventory_.items.end();)
            if (b[3] == 1 ? EquipmentSlot(it->first) : (it->first >= 40 && it->first <= 79))
                it = inventory_.items.erase(it);
            else
                ++it;
        if (b[3] == 1)
            inventory_.equipmentKnown = true;
        else
            inventory_.backpackKnown = true;
    }
    for (auto &i : updates)
        if (OwnedItemSlot(i.slot))
        {
            if (i.name.empty() && !i.model)
                inventory_.items.erase(i.slot);
            else
                inventory_.items[i.slot] = std::move(i);
        }
    ++inventory_.revision;
    inventory_.detailText.clear();
    inventory_.detailSlot = -1;
    return true;
}
bool Client::MoveInventoryItem(uint16_t from, uint16_t to, uint16_t count, uint64_t revision)
{
    auto it = inventory_.items.find(uint8_t(from));
    if (state_ != State::Playing || !OwnedItemSlot(from) || !(OwnedItemSlot(to) || to == 100) || from == to || revision != inventory_.revision || it == inventory_.items.end() || count > it->second.Count())
        return false;
    Bytes b;
    Put(b, objectId_);
    Put(b, to);
    Put(b, from);
    Put(b, count);
    Send(0xdd, b);
    return true; // No optimistic changes; server sends success or correction with
                 // InventoryUpdate.
}
bool Client::UseInventoryItem(uint16_t slot, uint8_t use, const ActionContext &c, uint64_t revision)
{
    if (state_ != State::Playing || !OwnedItemSlot(slot) || use > 2 || revision != inventory_.revision || !inventory_.items.count(uint8_t(slot)))
        return false;
    Bytes b;
    Put(b, uint16_t((c.speed & 511) | (c.backward ? 0x200 : 0) | (c.strafing ? 0x4000 : 0) | (c.targetVisible ? 0xa000 : 0) | (c.groundVisible ? 0x1000 : 0)));
    b.push_back(uint8_t(slot));
    b.push_back(use);
    Send(0x71, b);
    return true;
}
bool Client::InspectInventoryItem(uint16_t slot, uint64_t revision, double now)
{
    const auto it = inventory_.items.find(uint8_t(slot));
    if (state_ != State::Playing || !OwnedItemSlot(slot) || revision != inventory_.revision || it == inventory_.items.end())
        return false;
    // DetailWindow has no slot/request ID. Serialize requests, including
    // identical-name items.
    if (!pendingItemDetailName_.empty() && now <= pendingItemDetailExpires_)
        return false;
    pendingItemDetailName_ = it->second.name;
    const auto prefix = std::to_string(it->second.Count()) + " ";
    if (it->second.Count() > 1 && pendingItemDetailName_.compare(0, prefix.size(), prefix) == 0)
        pendingItemDetailName_.erase(0, prefix.size());
    pendingItemDetailRevision_ = revision;
    pendingItemDetailExpires_ = now + 5;
    inventory_.detailSlot = slot;
    inventory_.detailText.clear();
    Bytes b;
    Put(b, 1);
    Put(b, 0);
    Put(b, 0);
    Put(b, slot);
    Send(0xd8, b);
    return true;
}
} // namespace camelot
