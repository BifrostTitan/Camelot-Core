#include "Camelot/CamelotClient.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
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
void name(Bytes &b, const std::string &s)
{
    b.push_back(uint8_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
Bytes frame(uint8_t op, const Bytes &b)
{
    Bytes f;
    word(f, uint16_t(b.size()));
    f.push_back(op);
    f.insert(f.end(), b.begin(), b.end());
    return f;
}
void feed(Client &c, uint8_t op, const Bytes &b, double now = 1)
{
    auto f = frame(op, b);
    c.Feed(f.data(), f.size(), now);
}
void playing(Client &c, int version)
{
    c.Start(0);
    feed(c, 0x22, {0, 0, 0, 0, 0});
    feed(c, 0x2a, {});
    Bytes overview(24 + (version >= 73 ? 10 : 8) * 184 + 104, 0);
    overview[24] = 'T';
    overview[24 + 124] = 14;
    feed(c, 0xfd, overview);
    feed(c, 0x28, {42, 0});
    feed(c, 0x2d, {0});
    feed(c, 0x20, {0, 7, 0, 50, 0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 0});
    feed(c, 0x2b, {0});
    check(c.GetState() == State::Playing, "join failed");
    c.TakeOutgoing();
}
Bytes skill(uint8_t first, uint8_t subtype, std::initializer_list<std::pair<uint8_t, std::string>> entries)
{
    Bytes b{1, uint8_t(entries.size()), subtype, first};
    for (const auto &e : entries)
    {
        b.push_back(1);
        b.push_back(e.first);
        word(b, 0x1234);
        b.push_back(0);
        word(b, 80);
        name(b, e.second);
    }
    return b;
}
Bytes line()
{
    Bytes b{2, 3, 2, 0, 0, 0, 0};
    name(b, "Fire");
    b.push_back(5);
    word(b, 200);
    name(b, "Flame");
    b.push_back(10);
    word(b, 201);
    name(b, "Blaze");
    return b;
}
Bytes payload(const Bytes &b)
{
    return Bytes(b.begin() + 10, b.end() - 2);
}
int main()
{
    try
    {
        Settings s;
        s.username = "account";
        s.password = "secret";
        s.character = "T";
        s.version = 74;
        Client c(s);
        check(!c.SetAttack(true), "offline request rejected");
        playing(c, 74);
        // Fragmented multi-packet skill lists commit only at final page, preserving absolute indexes.
        auto f = frame(0x16, skill(0, 99, {{0, "Sword"}, {1, "Sprint"}}));
        c.Feed(f.data(), 4, 1);
        check(c.Combat().skills.empty(), "partial frame visible");
        c.Feed(f.data() + 4, f.size() - 4, 1);
        check(!c.Combat().skillsReady, "partial list committed");
        feed(c, 0x16, skill(2, 3, {{2, "Slash"}, {3, "Heal"}}));
        check(c.Combat().skills.size() == 4 && c.Combat().skills[2].index == 2, "skill indexes");
        feed(c, 0x16, line());
        check(c.Combat().spellLines.at(0).spells[1].level == 10, "spell level selector");
        ActionContext a;
        a.x = 10;
        a.y = 20;
        a.z = 50;
        a.heading = 1024;
        a.speed = 191;
        a.targetVisible = true;
        a.groundVisible = true;
        check(!c.UseSkill(0, a, 1), "spec cannot activate");
        check(c.UseSkill(2, a, 1), "style request");
        auto out = c.TakeOutgoing();
        check(out.size() == 2 && out[0][9] == 0xa9 && out[1][9] == 0xbb, "movement-before-skill");
        check(payload(out[0]).back() == 0x18, "movement preserves target and ground visibility for server melee checks");
        check(c.Move(a.x, a.y, a.z, a.heading, a.speed, false, false), "blocked target movement");
        const auto blocked = c.TakeOutgoing();
        check(payload(blocked.back()).back() == 0, "blocked line of sight is never optimistic");
        check(payload(out[1]) == Bytes({0xb0, 191, 1, 1}), "non-spec index and view flags");
        check(Client::Checksum(out[1].data(), out[1].size() - 2) == uint16_t((out[1][out[1].size() - 2] << 8) | out[1].back()), "checksum");
        check(c.UseSpell(0, 1, a, 1), "spell request");
        out = c.TakeOutgoing();
        check(out.back()[9] == 0x7d && payload(out.back()) == Bytes({0xb0, 191, 4, 0, 0, 10, 0, 20, 0, 0, 0, 50, 10, 0}), "1.74 spell pose/level/line");
        feed(c, 0xd6, {0, 0, 1, 1, 0, 1, 0, 3}, 2);
        check(!c.UseSkill(2, a, 3) && c.UseSkill(2, a, 5), "hybrid cooldown server deadline");
        c.TakeOutgoing();
        feed(c, 0xd6, {0, 4, 1, 2, 0, 1}, 2);
        check(!c.UseSpell(0, 1, a, 3) && c.UseSpell(0, 1, a, 6), "list cooldown position");
        feed(c, 0xd6, {0, 0, 1, 2, 0, 1}, 3);
        check(c.Combat().spellCooldowns.empty(), "zero cooldown clear");
        c.TakeOutgoing();
        check(c.SelectTarget(7, true) && !c.SelectTarget(999, true), "known target validation");
        check(c.SetAttack(true) && !c.Combat().attacking, "attack not predicted");
        feed(c, 0x74, {1, 0, 0, 0});
        check(c.Combat().attacking, "attack confirmed");
        feed(c, 0xad, {90, 70, 0, 0, 0, 60, 80, 0});
        check(c.Combat().statusKnown && c.Combat().health == 90 && c.Combat().mana == 70 && c.Combat().endurance == 60, "vitals");
        feed(c, 0x72, {0, 7, 0, 20, 0, 30, 0, 0}, 2);
        check(c.Combat().castUntil == 5 && c.Combat().castEffect == 20, "cast tenths seconds");
        feed(c, 0x73, {0, 7, 0, 1});
        check(c.Combat().castUntil == 0, "interrupt");
        feed(c, 0xbc, {0, 8, 0, 7, 0, 4, 0, 5, 44, 1, 0x82, 40});
        check(c.Combat().entityHealth.at(7) == 40, "target health");
        auto events = c.TakeCombatEvents();
        check(events.back().style == 300 && events.back().result == 2 && events.back().weapon == 4, "style high bit event");
        feed(c, 0x1b, {0, 7, 0, 20, 0, 8, 0, 12, 1, 1, 0xff, 0xbf});
        events = c.TakeCombatEvents();
        check(events.back().type == CombatEventType::SpellEffect && events.back().noSound && events.back().seconds == 1.2 && events.back().result == 1, "spell effect preserves sound suppression and travel duration");
        feed(c, 0x1b, {0, 7, 0, 20, 0, 8, 0, 12, 0, 1});
        events = c.TakeCombatEvents();
        check(c.GetState() == State::Playing && events.size() == 1 && events[0].target == 8 && !events[0].noSound && events[0].seconds == 1.2, "Catacombs ten-byte effect stays connected");
        feed(c, 0xc9, {0, 103, 0, 1, 1, 0, 2, 0, 0, 50, 4, 0});
        events = c.TakeCombatEvents();
        check(events.size() == 1 && events[0].type == CombatEventType::Sound && events[0].effect == 103 && events[0].zone == 1 && events[0].x == 256 && events[0].y == 512 && events[0].z == 50 && events[0].radius == 1024, "positional sound packet");
        Bytes buffs{1, 0, 0, 0, 0, 0, 0, 40, 0, 20, 0, 9};
        name(buffs, "Armor");
        feed(c, 0x7f, buffs);
        check(c.Combat().effects.size() == 1 && c.CancelEffect(9), "effect snapshot/cancel");
        out = c.TakeOutgoing();
        check(out.back()[9] == 0xf8 && payload(out.back()) == Bytes({0, 9}), "cancel internal id");
        feed(c, 0x7f, {0, 0, 0, 0});
        check(c.Combat().effects.empty(), "effects removal snapshot");
        feed(c, 0xd0, {0, 7, 0, 8, 0, 0, 0, 0});
        events = c.TakeCombatEvents();
        check(events.back().type == CombatEventType::LineOfSight, "LOS request surfaced");
        check(c.ReplyLineOfSight(7, 8, false), "LOS blocked reply");
        out = c.TakeOutgoing();
        check(payload(out.back()) == Bytes({0, 7, 0, 8, 0, 0, 0, 0}), "LOS never optimistic");
        feed(c, 0xae, {0, 7, 0, 8, 0, 0, 0, 0});
        check(c.Combat().dead && !c.Combat().attacking && !c.UseSpell(0, 0, a, 10), "death action gate");
        check(c.Release(), "dead character can request release");
        out = c.TakeOutgoing();
        check(out.back()[9] == 0xaf && payload(out.back()) == Bytes({0, 0, 0, 0, 0, 0, 0, 0, '&', 'r', 'e', 'l', 'e', 'a', 's', 'e', 0}), "release uses original command wire layout");
        feed(c, 0x91, {0, 1, 0, 2, 1, 244, 0, 12, 0, 2, 0, 3, 0, 4, 0, 0});
        check(c.Combat().progressKnown && c.Combat().levelPermill == 500 && c.Combat().realmPoints == 65538 && c.Combat().bountyPoints == 131075 && c.Combat().skillPoints == 12 && c.Combat().realmSkillPoints == 4, "server progress uses network byte order");
        feed(c, 0x89, {0, 7, 0, 0});
        check(!c.Combat().dead, "revive");
        check(!c.Release(), "living character cannot release");
        feed(c, 0xf6, {0, 8, 0, 0});
        check(c.Combat().target == 8, "forced target");
        feed(c, 0x74, {1, 0, 0, 0});
        feed(c, 0xa2, {0, 8});
        check(c.Combat().target == 0 && !c.Combat().attacking, "deleted target clears attack even before killing-blow packet");
        feed(c, 0x16, skill(0, 3, {{1, "New"}}));
        check(c.Combat().spellLines.empty() && c.Combat().skillCooldowns.empty(), "respec clears old indexes");
        for (int i = 0; i < 300; ++i)
            feed(c, 0xaf, {0, 0, 0, 0, 1, 0, 0, 0, 'x', 0});
        check(c.TakeCombatEvents().size() == 256, "events bounded");
        feed(c, 0xdf, {0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 50});
        check(c.Combat().groundKnown && c.Combat().groundY == 20, "server ground override");
        check(c.SetGroundTarget(11, 21, 51, true), "ground request");
        out = c.TakeOutgoing();
        check(payload(out.back()) == Bytes({0, 0, 0, 11, 0, 0, 0, 21, 0, 0, 0, 51, 1, 0}), "ground target 32bit coordinates");
        feed(c, 0x16, line());
        feed(c, 0xd6, {0, 10, 1, 2, 0, 0}, 2);
        feed(c, 0x16, skill(0, 3, {{1, "New"}}), 3);
        feed(c, 0x16, line(), 3);
        check(c.Combat().spellCooldowns.at(0) == 12, "unchanged list refresh preserves cooldown");
        c.Disconnect();
        check(c.Combat().skills.empty() && !c.Combat().statusKnown && c.LocalObjectId() == 0 && c.TakeCombatEvents().empty(), "disconnect reset");
        s.version = 68;
        Client legacy(s);
        playing(legacy, 68);
        feed(legacy, 0x16, skill(0, 3, {}));
        feed(legacy, 0x16, line());
        check(legacy.UseSpell(0, 0, a, 1), "legacy list spell");
        out = legacy.TakeOutgoing();
        check(payload(out.back()) == Bytes({0xb0, 191, 4, 0, 5, 0}), "1.68 spell layout");
        for (const auto &bad : std::vector<std::pair<uint8_t, Bytes>>{{0x16, {1, 1, 3, 0, 1, 1, 0, 0, 0, 0, 2, 100, 'x'}}, {0xd6, {0, 0, 1, 1, 0, 1, 0}}, {0x7f, {1, 0, 0, 0, 0}}, {0xbc, {0}}, {0xc9, {0}}, {0x1b, {0}}, {0xad, {0}}, {0x91, {0}}, {0x16, skill(4, 3, {{1, "Gap"}})}})
        {
            Client malformed(s);
            playing(malformed, 68);
            feed(malformed, 0x16, skill(0, 3, {{1, "Confirmed"}}));
            feed(malformed, 0xad, {90, 70, 0, 0, 0, 60, 80, 0});
            feed(malformed, bad.first, bad.second);
            check(malformed.GetState() == State::Playing && malformed.Combat().health == 90 && malformed.Combat().skills.size() == 1 && malformed.Combat().skills[0].name == "Confirmed", "malformed gameplay packet preserves confirmed state");
            auto diagnostics = malformed.TakePacketDiagnostics();
            check(diagnostics.size() == 1 && diagnostics[0].opcode == bad.first && diagnostics[0].payloadBytes == bad.second.size(), "rejection reports metadata only");
            for (int i = 0; i < 1000; ++i)
                feed(malformed, 0x1b, {0});
            diagnostics = malformed.TakePacketDiagnostics();
            check(diagnostics.size() == 1 && diagnostics[0].occurrences == 1000, "repeated malformed effects coalesce within bounded storage");
            feed(malformed, 0xad, {80, 70, 0, 0, 0, 60, 80, 0});
            check(malformed.GetState() == State::Playing && malformed.Combat().health == 80, "valid packets continue after rejections");
            auto combined = frame(0x1b, {0});
            auto valid = frame(0x1b, {0, 7, 0, 20, 0, 8, 0, 12, 0, 1});
            combined.insert(combined.end(), valid.begin(), valid.end());
            malformed.Feed(combined.data(), 2, 2);
            malformed.Feed(combined.data() + 2, combined.size() - 2, 2);
            check(malformed.GetState() == State::Playing && malformed.TakeCombatEvents().size() == 1, "fragmented adjacent bad and good frames retain alignment");
            malformed.TakePacketDiagnostics();
            for (int op = 0; op < 256; ++op)
                malformed.RejectPacket(uint8_t(op), 0, "Test rejection");
            check(malformed.TakePacketDiagnostics().size() == 32, "distinct diagnostics have bounded storage");
            const uint8_t invalidFrame[] = {0x20, 0, 0x1b};
            malformed.Feed(invalidFrame, sizeof(invalidFrame), 2);
            check(malformed.GetState() == State::Failed, "invalid framing remains terminal rather than interpreting untrusted stream offsets");
        }
        std::cout << "Combat packet, action, cooldown, lifecycle and malformed-input tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
