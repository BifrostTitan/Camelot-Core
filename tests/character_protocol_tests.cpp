#include "Camelot/CamelotClient.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
using namespace camelot;
void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void feed(Client &c, uint8_t op, Bytes payload)
{
    Bytes frame{uint8_t(payload.size() >> 8), uint8_t(payload.size()), op};
    frame.insert(frame.end(), payload.begin(), payload.end());
    c.Feed(frame.data(), frame.size(), 1);
}
void login(Client &c)
{
    c.Start(0);
    feed(c, 0x22, {0, 0, 0, 0, 0});
    feed(c, 0x2a, {});
}
Bytes overview()
{
    Bytes b(24 + 8 * 184 + 104, 0);
    const size_t p = 24 + 7 * 184;
    const std::string name = "Tester";
    std::copy(name.begin(), name.end(), b.begin() + p);
    b[p + 120] = 50;
    b[p + 121] = 2;
    b[p + 122] = 1;
    b[p + 123] = 0x51;
    b[p + 124] = 0x34;
    b[p + 125] = 0x12;
    b[p + 128] = 1;
    b[p + 131] = 2;
    b[p + 140] = 0x45;
    b[p + 141] = 3;
    b[p + 156] = 0x67;
    b[p + 157] = 2;
    b[p + 174] = 0x89;
    b[p + 175] = 1;
    b[p + 162] = 0xab;
    b[p + 163] = 4;
    b[p + 180] = 0;
    b[p + 181] = 1;
    return b;
}
int main()
{
    try
    {
        Settings mixed;
        mixed.username = "account";
        mixed.password = "secret";
        mixed.character = "tEsTeR";
        Client caseClient(mixed);
        login(caseClient);
        feed(caseClient, 0xfd, overview());
        check(caseClient.GetState() == State::Selecting, "case insensitive character selection");
        auto selectedPackets = caseClient.TakeOutgoing();
        const auto &selection = selectedPackets.back();
        check(std::string(selection.begin() + 13, selection.begin() + 19) == "Tester", "canonical server name on wire");
        mixed.character = "Missing";
        Client waiting(mixed);
        login(waiting);
        feed(waiting, 0xfd, overview());
        check(waiting.GetState() == State::Characters && waiting.Characters().size() == 1, "missing character preserves roster and authenticated session");
        waiting.Tick(100);
        check(waiting.GetState() == State::Characters, "selection wait has no login timeout");
        check(!waiting.SelectCharacter("Wrong", 100), "reject missing character without disconnect");
        check(waiting.SelectCharacter("tester", 100) && waiting.Error().empty(), "recover selection using canonical roster");
        mixed.character.clear();
        Client manual(mixed);
        login(manual);
        feed(manual, 0xfd, overview());
        check(manual.GetState() == State::Characters && manual.SelectCharacter("Tester", 2), "launcher can defer selection");
        Settings s;
        s.username = "account";
        s.password = "secret";
        s.character = "Tester";
        Client c(s);
        login(c);
        feed(c, 0xfd, overview());
        check(c.GetState() == State::Selecting, "selection");
        check(c.Characters().size() == 1, "empty slots");
        const auto &ch = c.Characters()[0];
        check(ch.slot == 7 && ch.databaseId == 0x01000002, "slot/database endianness");
        check(ch.appearance.model == 0x234 && ch.appearance.modelCode == 0x1234 && ch.appearance.race == 17 && ch.appearance.gender == 1, "packed race/model");
        check(ch.appearance.equipment[0].model == 0x345 && ch.appearance.equipment[0].color == 0x267, "LE armor");
        check(ch.appearance.equipment[1].slot == 11 && ch.appearance.equipment[1].color == 0x4ab, "shield emblem source");
        feed(c, 0x28, {42, 0});
        feed(c, 0x2d, {0});
        feed(c, 0x20, {0, 7, 1, 2, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 0});
        feed(c, 0x2b, {0});
        check(c.Players().at(7).appearance.model == 0x234, "local overview inheritance");
        feed(c, 0x15, {0, 7, 1, 0x10, 2, 21, 0xc3, 0x45, 0x12, 0x34, 10, 0x60, 0x33, 0x77, 0x08, 0x99});
        const auto &a = c.Players().at(7).appearance;
        check(a.equipment.size() == 2 && a.equipment[0].model == 0x345 && a.equipment[0].color == 0x1234, "16bit color flag");
        check(a.equipment[1].color == 0x77 && a.equipment[1].effect == 0x899 && a.hood == 1 && a.leftWeapon == 1, "color/effect lengths");
        // Equipment can arrive before the remote object-create packet.
        feed(c, 0x15, {0, 9, 0, 0x10, 1, 10, 0, 4});
        Bytes remote(24, 0);
        remote[1] = 43;
        remote[3] = 9;
        remote[15] = 14;
        remote[22] = 1;
        remote[23] = 'R';
        remote.insert(remote.end(), {4, 'T', 'e', 's', 't'});
        feed(c, 0xd4, remote);
        check(c.Players().count(9) != 0, "legacy guild packet creates player");
        check(c.Players().at(9).guildName == "Test", "legacy player guild is retained for nameplates");
        auto truncatedGuild = remote;
        truncatedGuild.pop_back();
        truncatedGuild[23] = 'X';
        auto malformed = c;
        feed(malformed, 0xd4, truncatedGuild);
        check(malformed.GetState() == State::Playing && malformed.Players().at(9).guildName == "Test" && malformed.Players().at(9).name == c.Players().at(9).name && !malformed.TakePacketDiagnostics().empty(), "truncated guild preserves the last complete identity and reports rejection");
        check(c.Players().at(9).appearance.equipment.size() == 1 && c.Players().at(9).appearance.equipment[0].model == 4, "early equipment cached for object creation");
        feed(c, 0x15, {0, 9, 0, 0xff, 0});
        check(c.Players().at(9).appearance.equipment.empty(), "remote unequip snapshot clears previous items");
        // Local inventory updates are partial: preserve other slots, remove empty items.
        Bytes inventory = {2, 0, 0x10, 0};
        inventory.push_back(25);
        Bytes chest(18, 0);
        chest[0] = 1;
        chest[12] = 58;
        chest[14] = 21;
        chest[17] = 4;
        inventory.insert(inventory.end(), chest.begin(), chest.end());
        inventory.insert(inventory.end(), {'R', 'o', 'b', 'e'});
        inventory.push_back(10);
        inventory.insert(inventory.end(), 18, 0);
        feed(c, 0x02, inventory);
        const auto &equipped = c.Players().at(7).appearance.equipment;
        check(equipped.size() == 2 && std::any_of(equipped.begin(), equipped.end(), [](const Equipment &e) { return e.slot == 25 && e.model == 58 && e.color == 21; }), "inventory adds equipped armor while preserving unrelated slots");
        check(std::none_of(equipped.begin(), equipped.end(), [](const Equipment &e) { return e.slot == 10; }), "empty inventory item unequips weapon");
        feed(c, 0xdb, {0, 7, 0, 14, 0, 0, 0, 0});
        check(c.Players().at(7).appearance.model == 14, "model change");
        feed(c, 0xdb, {0, 7, 0xb1, 0xdf, 0, 0, 0, 0});
        check(c.Players().at(7).appearance.model == 479 && c.Players().at(7).appearance.modelCode == 45535, "packed Saracen model retains flags and resolves base ID");
        feed(c, 0x15, {0, 7, 0, 0, 0});
        check(c.Players().at(7).appearance.equipment.empty(), "empty update clears armor");
        feed(c, 0xda, {0, 8, 0, 0, 0, 0, 0, 50, 0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 47, 100, 5, 32, 32, 4, 'W', 'o', 'l', 'f', 3, 'P', 'a', 'c', 0});
        const auto &npc = c.Players().at(8);
        check(npc.npc && npc.name == "Wolf" && npc.appearance.model == 47 && npc.appearance.size == 100, "NPC model/size/name");
        check(npc.flying && npc.guildName == "Pac", "NPC guild and airborne flag survive creation");
        feed(c, 0xa1, {0, 150, 0, 10, 0, 10, 0, 110, 0, 20, 0, 20, 0, 50, 0, 50, 0, 8, 0, 0, 100, 0, 0, 0});
        check(c.Players().at(8).speed == 150 && c.Players().at(8).targetX == 110 && c.Players().at(8).targetZ == 50, "NPC movement destinations");
        check(!c.Players().at(8).flying, "NPC update can return a flying creature to the ground");
        feed(c, 0xa1, {0, 150, 0, 10, 0, 10, 0, 110, 0, 20, 0, 20, 0, 50, 0, 50, 0, 8, 0, 0, 100, 32, 0, 0});
        check(c.Players().at(8).flying, "NPC movement carries the airborne flag");
        feed(c, 0xa2, {0, 8});
        check(c.Players().count(8) == 0, "NPC deletion");
        feed(c, 0x15, {0, 7, 0, 0, 1, 21, 0x80, 1, 0});
        check(c.GetState() == State::Playing && c.Players().at(7).appearance.equipment.empty() && !c.TakePacketDiagnostics().empty(), "truncated variable item preserves confirmed appearance");
        Client packed(s);
        login(packed);
        auto packedOverview = overview();
        const size_t pp = 24 + 7 * 184;
        packedOverview[pp + 124] = 0xdf;
        packedOverview[pp + 125] = 0xb1;
        feed(packed, 0xfd, packedOverview);
        check(packed.Characters()[0].appearance.model == 479 && packed.Characters()[0].appearance.modelCode == 45535, "actual login model decodes from LE overview");
        feed(packed, 0x28, {42, 0});
        feed(packed, 0x2d, {0});
        feed(packed, 0x20, {0, 7, 1, 2, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 0});
        check(packed.Players().at(7).appearance.model == 479, "login preserves decoded selected appearance");
        Client early(s);
        login(early);
        feed(early, 0xfd, overview());
        Bytes initial = {1, 0, 0xff, 0, 25};
        Bytes robe(18, 0);
        robe[12] = 58;
        initial.insert(initial.end(), robe.begin(), robe.end());
        feed(early, 0x02, initial);
        feed(early, 0x28, {42, 0});
        feed(early, 0x2d, {0});
        feed(early, 0x20, {0, 7, 1, 2, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 0});
        check(std::any_of(early.Players().at(7).appearance.equipment.begin(), early.Players().at(7).appearance.equipment.end(), [](const Equipment &e) { return e.slot == 25 && e.model == 58; }), "initial inventory survives player initialization");
        feed(early, 0x15, {0, 10, 0, 0x10, 1, 10, 0, 4});
        feed(early, 0xa2, {0, 10});
        remote[3] = 10;
        feed(early, 0xd4, remote);
        check(early.Players().at(10).appearance.equipment.empty(), "deleted object does not reuse stale queued equipment");
        Client badInventory(s);
        login(badInventory);
        feed(badInventory, 0xfd, overview());
        feed(badInventory, 0x02, {1, 0, 0, 0, 25, 1});
        check(badInventory.GetState() == State::Failed, "truncated inventory rejected atomically");
        Client truncated(s);
        login(truncated);
        auto shortPacket = overview();
        shortPacket.resize(24 + 8 * 184 - 1);
        feed(truncated, 0xfd, shortPacket);
        check(truncated.GetState() == State::Failed, "truncated slot table rejected");
        Settings modernSettings = s;
        modernSettings.version = 74;
        Client modern(modernSettings);
        modern.Start(0);
        auto handshake = modern.TakeOutgoing();
        check(handshake.size() == 1 && handshake[0][13] == 7 && handshake[0][14] == 4, "Catacombs version handshake");
        feed(modern, 0x22, {0, 0, 0, 0, 0});
        feed(modern, 0x2a, {});
        Bytes modernOverview(24 + 10 * 184 + 104, 0);
        const size_t mp = 24 + 9 * 184;
        const std::string modernName = "Tester";
        std::copy(modernName.begin(), modernName.end(), modernOverview.begin() + mp);
        modernOverview[mp + 25] = 0x65;
        modernOverview[mp + 26] = 0x43;
        modernOverview[mp + 27] = 0x21;
        modernOverview[mp + 28] = 0x70;
        modernOverview[mp + 29] = 0x30;
        modernOverview[mp + 30] = 0x20;
        modernOverview[mp + 31] = 0x54;
        modernOverview[mp + 32] = 0x61;
        modernOverview[mp + 33] = 2;
        modernOverview[mp + 34] = 0x80;
        modernOverview[mp + 124] = 14;
        modernOverview[mp + 140 + 4 * 2] = 58;
        feed(modern, 0xfd, modernOverview);
        check(modern.GetState() == State::Selecting && modern.Characters()[0].slot == 9, "tenth Catacombs character slot");
        auto modernPackets = modern.TakeOutgoing();
        const auto &modernSelection = modernPackets.back();
        check(modernSelection[13] == 0 && std::string(modernSelection.begin() + 14, modernSelection.begin() + 20) == "Tester", "1.74 selection padding preserves full name");
        const auto &ma = modern.Characters()[0].appearance;
        check(ma.eyeSize == 0x65 && ma.lipSize == 0x43 && ma.moodType == 0x80 && ma.hairStyle == 0x20 && ma.faceType == 0x30, "full Catacombs customization bytes");
        check(ma.hood == 1 && ma.equipment[0].extension == 6, "overview hood and torso extension");
        feed(modern, 0x28, {42, 0});
        feed(modern, 0x2d, {0});
        feed(modern, 0x20, {0, 7, 1, 2, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 0});
        feed(modern, 0x2b, {0});
        feed(modern, 0x15, {0, 7, 1, 0x10, 2, 25, 0x40, 58, 3, 21, 10, 0, 4});
        check(modern.Players().at(7).appearance.equipment[0].extension == 3 && modern.Players().at(7).appearance.equipment[0].color == 21, "Catacombs equipment extension does not shift color");
        Bytes modernInventory = {1, 0, 0xff, 0, 25};
        Bytes modernRobe(19, 0);
        modernRobe[12] = 46;
        modernRobe[13] = 5;
        modernRobe[15] = 22;
        modernInventory.insert(modernInventory.end(), modernRobe.begin(), modernRobe.end());
        feed(modern, 0x02, modernInventory);
        check(modern.Players().at(7).appearance.equipment.back().model == 46 && modern.Players().at(7).appearance.equipment.back().extension == 5, "Catacombs inventory fixed record width");
        Bytes modernRemote = {0, 43, 0, 8, 0, 14, 0, 50, 0, 0, 0, 10, 0, 20, 0, 0, 0x65, 0x43, 0x80, 0x21, 50, 0x70, 0x30, 0x20, 4, 0, 4, 'T', 'e', 's', 't', 0, 0, 0, 0};
        feed(modern, 0x4b, modernRemote);
        check(modern.Players().at(8).name == "Test" && modern.Players().at(8).appearance.moodType == 0x80, "modern remote character appearance offsets");
        check(modern.Move(10, 20, 50, 0, 100), "Catacombs movement retains legacy position format");
        std::cout << "Character overview and live appearance protocol tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
