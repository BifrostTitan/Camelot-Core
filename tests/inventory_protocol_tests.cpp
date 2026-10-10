#include "Camelot/CamelotClient.h"
#include <iostream>
#include <stdexcept>
using namespace camelot;
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void word(Bytes &b, uint16_t n)
{
    b.push_back(uint8_t(n >> 8));
    b.push_back(uint8_t(n));
}
Bytes frame(uint8_t op, const Bytes &b)
{
    Bytes f;
    word(f, uint16_t(b.size()));
    f.push_back(op);
    f.insert(f.end(), b.begin(), b.end());
    return f;
}
void feed(Client &c, uint8_t op, const Bytes &b)
{
    const auto f = frame(op, b);
    c.Feed(f.data(), f.size(), 1);
}
void playing(Client &c, int version)
{
    c.Start(0);
    feed(c, 0x22, {0, 0, 0, 0, 0});
    feed(c, 0x2a, {});
    Bytes roster(24 + (version >= 73 ? 10 : 8) * 184 + 104, 0);
    roster[24] = 'T';
    roster[24 + 124] = 14;
    feed(c, 0xfd, roster);
    feed(c, 0x28, {42, 0});
    feed(c, 0x2d, {0});
    feed(c, 0x20, {0, 7, 0, 50, 0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 0});
    feed(c, 0x2b, {0});
    check(c.GetState() == State::Playing, "join failed");
    c.TakeOutgoing();
}
void item(Bytes &b, int version, uint8_t slot, uint16_t model, const std::string &name, uint8_t type = 1, uint8_t count = 1)
{
    b.push_back(slot);
    Bytes data(version >= 72 ? 19 : 18, 0);
    if (!name.empty() || model)
    {
        data[0] = 50;
        data[1] = count;
        data[2] = 35;
        data[3] = 128;
        data[4] = type;
        data[5] = 0;
        data[6] = 25;
        data[7] = 91;
        data[8] = 87;
        data[9] = 99;
        data[10] = 5;
        data[11] = uint8_t(model >> 8);
        data[12] = uint8_t(model);
        if (version >= 72)
            data[13] = 3;
        const size_t color = version >= 72 ? 14 : 13;
        data[color] = 0x12;
        data[color + 1] = 0x34;
        data[color + 3] = 9;
        data.back() = uint8_t(name.size());
    }
    b.insert(b.end(), data.begin(), data.end());
    b.insert(b.end(), name.begin(), name.end());
}
Bytes payload(const Bytes &b)
{
    return Bytes(b.begin() + 10, b.end() - 2);
}
int main()
{
    try
    {
        for (int version : {68, 72, 73, 74})
        {
            Settings s;
            s.version = uint8_t(version);
            s.username = "account";
            s.password = "secret";
            s.character = "T";
            Client c(s);
            check(!c.MoveInventoryItem(40, 41, 0, 0), "offline move rejected");
            playing(c, version);
            check(c.Inventory().equipmentKnown && c.Inventory().backpackKnown && c.Inventory().items.empty(), "world initialization confirms empty inventory when DOL omits empty collections");
            Bytes equip{2, 1, 0xf0, 1};
            item(equip, version, 25, 58, "Robe");
            item(equip, version, 10, 4, "Sword");
            feed(c, 2, equip);
            check(c.Inventory().equipmentKnown && c.Inventory().items.size() == 2, "equipment snapshot");
            const auto &robe = c.Inventory().items.at(25);
            check(robe.model == 58 && robe.condition == 91 && robe.durability == 87 && robe.quality == 99 && robe.bonus == 5 && robe.weight == 25, "item metadata");
            check(robe.color == 0x1234 && robe.effect == 9 && robe.extension == (version >= 72 ? 3 : 0), "version-specific item fields");
            Bytes bag{2, 1, 0xf0, 2};
            item(bag, version, 40, 100, "20 Arrows", 43, 20);
            item(bag, version, 41, 200, "Ring");
            const auto packet = frame(2, bag);
            c.Feed(packet.data(), packet.size() - 1, 1);
            check(!c.Inventory().items.count(40), "fragmented packet not committed");
            c.Feed(packet.data() + packet.size() - 1, 1, 1);
            check(c.Inventory().items.size() == 4 && c.Inventory().backpackKnown, "backpack preserves equipment");
            check(c.Inventory().items.at(40).Count() == 20 && c.Inventory().items.at(41).Count() == 1, "stack decoding");
            auto rev = c.Inventory().revision;
            check(c.MoveInventoryItem(40, 42, 5, rev), "split request");
            auto out = c.TakeOutgoing();
            check(out.size() == 1 && out[0][9] == 0xdd && payload(out[0]) == Bytes({0, 7, 0, 42, 0, 40, 0, 5}), "classic move wire layout");
            check(c.Inventory().items.at(40).Count() == 20 && !c.Inventory().items.count(42), "move does not change confirmed state");
            check(c.MoveInventoryItem(41, 100, 0, rev), "server autoequip request");
            c.TakeOutgoing();
            for (int bad : {1, 18, 30, 39, 80, 110, 150, 1007, 65535})
                check(!c.MoveInventoryItem(40, uint16_t(bad), 0, rev), "unsafe destinations rejected");
            check(!c.MoveInventoryItem(40, 40, 0, rev) && !c.MoveInventoryItem(40, 42, 21, rev) && !c.MoveInventoryItem(42, 43, 0, rev), "invalid moves rejected");
            ActionContext context;
            context.speed = 200;
            context.backward = true;
            context.targetVisible = true;
            check(c.UseInventoryItem(40, 2, context, rev), "item use request");
            out = c.TakeOutgoing();
            check(out.size() == 1 && out[0][9] == 0x71 && payload(out[0]) == Bytes({0xa2, 0xc8, 40, 2}), "classic use wire layout");
            check(!c.UseInventoryItem(40, 3, context, rev), "invalid use rejected");
            check(c.InspectInventoryItem(40, rev, 1), "inspect request");
            out = c.TakeOutgoing();
            check(!c.InspectInventoryItem(41, rev, 1), "detail requests are serialized because responses carry no slot ID");
            check(out.size() == 1 && out[0][9] == 0xd8 && payload(out[0]) == Bytes({0, 1, 0, 0, 0, 0, 0, 40}), "classic item detail request");
            feed(c, 0xc4, {6, 'A', 'r', 'r', 'o', 'w', 's', 1, 5, 'B', 'o', 'n', 'u', 's', 0});
            check(c.Inventory().detailSlot == 40 && c.Inventory().detailText == "Bonus\n", "matching server delve text");
            check(c.InspectInventoryItem(41, rev, 1), "second inspect request");
            c.TakeOutgoing();
            feed(c, 0xc4, {6, 'A', 'r', 'r', 'o', 'w', 's', 1, 5, 'B', 'o', 'n', 'u', 's', 0});
            check(c.Inventory().detailText.empty(), "unrelated detail window not attributed to selected item");
            Bytes update{1, 1, 0xf0, 0};
            item(update, version, 40, 0, "");
            feed(c, 2, update);
            check(!c.Inventory().items.count(40) && c.Inventory().items.count(41), "partial empty slot removal");
            feed(c, 0xc4, {4, 'R', 'i', 'n', 'g', 1, 5, 'B', 'o', 'n', 'u', 's', 0});
            check(c.Inventory().detailText.empty(), "item changes invalidate pending delve details");
            check(!c.MoveInventoryItem(41, 42, 0, rev) && !c.UseInventoryItem(41, 0, context, rev), "stale requests rejected");
            const auto before = c.Inventory().revision;
            Bytes vault{1, 0, 0, 3};
            item(vault, version, 25, 999, "Storage item");
            feed(c, 2, vault);
            check(c.Inventory().revision == before && c.Inventory().items.at(25).model == 58, "storage cannot alter owned inventory or appearance");
            Bytes replacement{1, 0, 0xff, 1};
            item(replacement, version, 21, 10, "Helm");
            feed(c, 2, replacement);
            check(!c.Inventory().items.count(25) && c.Inventory().items.count(41) && c.Players().at(7).appearance.equipment.size() == 1, "equipment refresh clears only equipment");
            feed(c, 2, {0, 0, 0xff, 2});
            check(c.Inventory().items.size() == 1, "empty backpack snapshot clears backpack");
            rev = c.Inventory().revision;
            c.Disconnect();
            check(c.Inventory().items.empty() && !c.Inventory().equipmentKnown && c.Inventory().revision != rev, "disconnect invalidates inventory");
            Client other(s);
            check(other.Inventory().revision != c.Inventory().revision, "new session cannot reuse drag revision");
            playing(c, version);
            feed(c, 2, {1, 0, 0, 0, 40, 1});
            check(c.GetState() == State::Failed && c.Inventory().items.empty(), "truncated item fails safely");
            playing(c, version);
            Bytes malformed{1, 0, 0, 0};
            item(malformed, version, 40, 4, "Sword");
            malformed.pop_back();
            feed(c, 2, malformed);
            check(c.GetState() == State::Failed, "truncated name rejected");
        }
        std::cout << "Inventory protocol tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
