#include "Camelot/CamelotClient.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>
using namespace camelot;
void check(bool v, const char *m)
{
    if (!v)
        throw std::runtime_error(m);
}
void feed(Client &c, uint8_t op, Bytes b)
{
    Bytes w{uint8_t(b.size() >> 8), uint8_t(b.size()), op};
    w.insert(w.end(), b.begin(), b.end());
    c.Feed(w.data(), w.size(), 1);
}
void put(Bytes &b, int p, uint32_t v, int n)
{
    for (int i = 0; i < n; ++i)
        b[p + i] = uint8_t(v >> (8 * (n - i - 1)));
}
Bytes position(int region, int x, int y)
{
    Bytes b(24);
    put(b, 0, 42, 2);
    put(b, 2, 100, 2);
    put(b, 4, x, 4);
    put(b, 8, y, 4);
    put(b, 20, region, 2);
    return b;
}
int main()
{
    try
    {
        Settings s;
        s.username = "account";
        s.password = "secret";
        s.character = "Troll";
        s.version = 74;
        {
            auto higher = s;
            higher.zone = 266;
            Client modern(higher);
            modern.Start(0);
            check(modern.GetState() == State::Crypt, "Catacombs accepts 16-bit zone IDs");
            higher.version = 68;
            Client legacy(higher);
            legacy.Start(0);
            check(legacy.GetState() == State::Failed, "legacy zone limit remains explicit");
        }
        s.zones = {{0, 1, 548864, 483328, 65536, 65536, true}, {100, 100, 100000, 200000, 65536, 65536, true}, {101, 100, 165536, 200000, 65536, 65536, true}, {200, 200, 300000, 400000, 65536, 65536, true}, {201, 200, 365536, 400000, 65536, 65536, false}};
        Client c(s);
        c.Start(0);
        feed(c, 0x22, Bytes(5));
        feed(c, 0x2a, {});
        Bytes roster(24 + 1840);
        std::copy(s.character.begin(), s.character.end(), roster.begin() + 24);
        roster[24 + 124] = 137;
        roster[24 + 126] = 100;
        feed(c, 0xfd, roster);
        feed(c, 0x28, {1, 0});
        feed(c, 0x2d, {0});
        c.TakeOutgoing();
        feed(c, 0x20, position(100, 100010, 200020));
        check(c.GetState() == State::Loading, "entry waits for renderer");
        check(c.ZoneId() == 100 && c.ZoneOriginX() == 100000, "Midgard coordinates replace Albion origin");
        check(c.Players().at(42).appearance.model == 137, "selected body preserved during entry");
        Bytes heading(12);
        put(heading, 0, c.Players().at(42).sessionId, 2);
        // Classic heading: flags at byte 5, health/attack state at byte 8.
        heading[5] = 128;
        heading[8] = 100;
        feed(c, 0xba, heading);
        check(c.Players().at(42).attackModeKnown && !c.Players().at(42).attacking, "torch flag cannot be mistaken for attack mode");
        heading[8] = 100 | 128;
        feed(c, 0xba, heading);
        check(c.Players().at(42).attacking, "classic heading carries authoritative attack mode");
        feed(c, 0xba, Bytes(heading.begin(), heading.begin() + 4));
        check(c.Players().at(42).attacking, "short heading fixture cannot overwrite known combat state");
        Bytes movement(18);
        put(movement, 0, c.Players().at(42).sessionId, 2);
        movement[10] = 100;
        movement[17] = 100;
        feed(c, 0xa9, movement);
        check(!c.Players().at(42).attacking, "classic position clears authoritative attack mode");
        movement[17] = 100 | 128;
        feed(c, 0xa9, movement);
        check(c.Players().at(42).attacking, "classic position carries authoritative attack mode");
        check(!c.Move(100020, 200030, 100, 0), "movement blocked until map ready");
        check(c.TakeOutgoing().empty(), "init request waits for terrain");
        check(c.ConfirmWorldReady(2), "renderer acknowledgment");
        check(c.TakeOutgoing().back()[9] == 0xe8, "player init after map ready");
        feed(c, 0x2b, {0});
        check(c.GetState() == State::Playing, "initialization finishes");
        Bytes npc(28);
        put(npc, 0, 43, 2);
        put(npc, 6, 100, 2);
        put(npc, 8, 100050, 4);
        put(npc, 12, 200060, 4);
        put(npc, 18, 47, 2);
        npc[20] = 60;
        npc[21] = 1;
        npc[24] = 8;
        npc[25] = 2;
        npc.insert(npc.end(), {4, 'W', 'o', 'l', 'f', 0, 0});
        feed(c, 0xda, npc);
        check(c.Players().at(43).name == "Wolf" && c.Players().at(43).appearance.model == 47, "Catacombs NPC names follow extended flags, not the legacy offset");
        Bytes remote(26);
        put(remote, 0, 44, 2);
        put(remote, 2, 45, 2);
        put(remote, 8, 100, 2);
        remote.insert(remote.end(), {1, 'R', 4, 'T', 'e', 's', 't', 0});
        feed(c, 0x4b, remote);
        check(c.Players().count(45) != 0, "modern guild packet creates player in the current zone");
        check(c.Players().at(45).name == "R" && c.Players().at(45).guildName == "Test", "Catacombs player guild uses its versioned name offset");
        remote.resize(remote.size() - 2);
        remote[27] = 'X';
        auto malformed = c;
        feed(malformed, 0x4b, remote);
        check(malformed.GetState() == State::Failed && malformed.Players().empty(), "short modern guild packet rejects unsafe world state");
        c.TakeCombatEvents();
        check(c.SelectTarget(43, true) && c.SetAttack(true), "NPC combat request");
        feed(c, 0x74, {1, 0, 0, 0});
        feed(c, 0xbc, {0, 42, 0, 43, 0, 14, 0, 0, 0, 0, 0, 0});
        const auto death = c.TakeCombatEvents();
        check(death.size() == 2 && death[0].type == CombatEventType::Melee && death[1].type == CombatEventType::Death && !c.Combat().attacking && c.Combat().entityHealth.at(43) == 0, "server NPC killing blow emits death and stops attack");
        feed(c, 0xbc, {0, 42, 0, 43, 0, 14, 0, 0, 0, 0, 0, 0});
        check(c.TakeCombatEvents().size() == 1, "repeated zero health does not replay NPC death");
        feed(c, 0xa2, {0, 43});
        feed(c, 0xbc, {0, 42, 0, 43, 0, 14, 0, 0, 0, 0, 0, 0});
        feed(c, 0xda, npc);
        check(c.Players().at(43).name == "Wolf" && c.Combat().entityHealth.count(43) == 0, "NPC ID reuse after deletion and delayed killing blow clears stale dead health");
        Bytes jump(16);
        put(jump, 0, 165550, 4);
        put(jump, 4, 200050, 4);
        put(jump, 8, 42, 2);
        put(jump, 10, 100, 2);
        feed(c, 0x04, jump);
        check(c.GetState() == State::Loading && c.ZoneId() == 101, "same-region teleport regenerates zone");
        c.ConfirmWorldReady(3);
        check(c.GetState() == State::Playing, "same-region teleport retains session");
        feed(c, 0xb7, {0, 200, 0, 200});
        check(c.GetState() == State::World, "region packet requests world initialization without reconnecting");
        feed(c, 0x20, position(200, 300050, 400050));
        check(c.ZoneId() == 200 && c.GetState() == State::Loading, "region destination resolved");
        check(c.Players().at(42).appearance.model == 137, "body survives object ID reuse on teleport");
        c.ConfirmWorldReady(4);
        feed(c, 0x2b, {0});
        feed(c, 0x20, position(200, 365550, 400050));
        check(c.GetState() == State::Failed, "unsupported dungeon refuses unsafe spawn");
        std::cout << "World transition protocol passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
