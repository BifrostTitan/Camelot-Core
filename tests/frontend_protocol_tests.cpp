#include "Camelot/CamelotClient.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace camelot;
void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void feed(Client &client, uint8_t opcode, Bytes payload)
{
    Bytes wire{uint8_t(payload.size() >> 8), uint8_t(payload.size()), opcode};
    wire.insert(wire.end(), payload.begin(), payload.end());
    // Exercise fragmented replies, as real TCP can split any field.
    for (auto byte : wire)
        client.Feed(&byte, 1, 1);
}
void login(Client &client)
{
    client.Start(0);
    feed(client, 0x22, Bytes(5, 0));
    feed(client, 0x2a, {});
}
Bytes roster(uint8_t version)
{
    return Bytes(24 + (version >= 73 ? 10 : 8) * 184 + 104, 0);
}
void nameReply(Client &client, const std::string &name, uint8_t result)
{
    Bytes b(54, 0);
    std::copy(name.begin(), name.end(), b.begin());
    b[50] = result;
    feed(client, 0xcc, b);
}
int main()
{
    try
    {
        for (uint8_t version : {uint8_t(68), uint8_t(73), uint8_t(74)})
        {
            Settings settings;
            settings.username = "account";
            settings.password = "secret";
            settings.version = version;
            settings.interactive = true;
            Client client(settings);
            login(client);
            auto wire = client.TakeOutgoing();
            check(wire.back()[9] == 0xfc, "realm discovery opcode");
            check(std::string(wire.back().begin() + 13, wire.back().begin() + 22) == "account-X", "realm discovery suffix");
            feed(client, 0xfe, {0});
            check(client.AllowedRealm() == 0 && !client.CharactersReady(), "cross-realm discovery is not a roster");
            client.Tick(100);
            check(client.GetState() == State::Characters, "realm selection may wait indefinitely");
            client.TakeOutgoing();
            for (uint8_t realm = 1; realm <= 3; ++realm)
            {
                check(client.RequestRealm(realm, 101), "each realm can be requested");
                wire = client.TakeOutgoing();
                check(wire.back()[21] == (realm == 1 ? 'S' : realm == 2 ? 'N'
                                                                        : 'H'),
                      "chosen realm suffix");
                feed(client, 0xfd, roster(version));
                check(client.CharactersReady(), "empty realm roster is ready");
                CharacterDraft draft;
                draft.name = "Fronttest";
                draft.slot = version >= 73 ? 9 : 7;
                draft.realm = realm;
                draft.race = 18;
                draft.gender = 1;
                draft.model = 1075;
                draft.classId = 54;
                draft.region = 200;
                draft.isles = true;
                draft.eyeSize = 0x65;
                draft.lipSize = 0x43;
                draft.eyeColor = 3;
                draft.faceType = 0x21;
                draft.hairStyle = 0x32;
                draft.hairColor = 7;
                draft.moodType = 0x89;
                draft.stats = {60, 70, 80, 50, 40, 30, 20, 10};
                check(!client.CreateCharacter(draft, 102), "creation waits for server name validation");
                check(client.CheckCharacterName(draft.name), "name request");
                client.TakeOutgoing();
                nameReply(client, "Othername", 0);
                check(client.NameResult() == -1, "stale name reply ignored");
                nameReply(client, draft.name, 2);
                check(!client.CreateCharacter(draft, 102), "taken name rejected");
                nameReply(client, draft.name, 0);
                check(client.CreateCharacter(draft, 102), "valid creation request");
                wire = client.TakeOutgoing();
                const auto &creation = wire.back();
                const size_t p = 10 + 24 + draft.slot * 184;
                check(creation[9] == 0xff && creation.size() == 12 + 24 + (version >= 73 ? 10 : 8) * 184, "creation frame length and opcode");
                check(creation[p + 43] == 0x23 && creation[p + 46] == 0x89, "create operation is network order");
                check(creation[p + 123] == 0xd2 && creation[p + 124] == 0x33 && creation[p + 125] == 0x14, "race high bit, gender, SI start and LE model with medium height bits");
                check(creation[p + 25] == 0x65 && creation[p + 29] == 0x21 && creation[p + 34] == 0x89, "customization retained");
                check(std::equal(draft.stats.begin(), draft.stats.end(), creation.begin() + p + 132), "wire stat order");
                check(creation[10 + 24 + 43] == 0 && creation[p + 183] == 80, "other slots unchanged and new constitution");
                check(!client.SelectCharacter(draft.name, 103), "selection waits for refreshed roster");
                auto created = roster(version);
                const size_t q = 24 + draft.slot * 184;
                std::copy(draft.name.begin(), draft.name.end(), created.begin() + q);
                created[q + 122] = realm;
                created[q + 124] = 0x33;
                created[q + 125] = 4;
                feed(client, 0xfd, created);
                check(client.Error().empty() && client.GetState() == State::Characters, "interactive creation does not auto-enter world");
                check(!client.CreateCharacter(draft, 104), "occupied slot protected");
                check(client.DeleteCharacter("fronttest", 104), "delete uses canonical roster name");
                wire = client.TakeOutgoing();
                check(wire.size() == 2 && wire[0][9] == 0xc0 && wire[1][9] == 0xfc, "delete followed by roster refresh");
                check(std::string(wire[0].begin() + 10, wire[0].begin() + 19) == draft.name, "canonical delete name");
                feed(client, 0xfd, roster(version));
            }
            Client locked(settings);
            login(locked);
            locked.TakeOutgoing();
            feed(locked, 0xfe, {2});
            check(!locked.RequestRealm(1, 2) && locked.AllowedRealm() == 2, "server realm lock enforced");
            wire = locked.TakeOutgoing();
            check(wire.size() == 1 && wire.back()[21] == 'N', "locked realm auto-requests proper roster");
        }
        std::cout << "Interactive frontend protocol tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
