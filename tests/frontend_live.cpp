// Explicit local integration runner. Never registered as a CTest: it creates and
// removes only its own temporary characters when --create is supplied.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Camelot/CamelotClient.h"
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cmath>
using namespace camelot;
void require(bool ok, const char *error)
{
    if (!ok)
        throw std::runtime_error(error);
}
double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int main(int argc, char **argv)
{
    SOCKET socketHandle = INVALID_SOCKET;
    try
    {
        std::cout << std::unitbuf;
        WSADATA wsa;
        require(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "WSA startup");
        Settings settings;
        const char *user = std::getenv("CAMELOT_USERNAME"), *secret = std::getenv("CAMELOT_PASSWORD");
        require(user && secret, "Set CAMELOT_USERNAME and CAMELOT_PASSWORD privately");
        settings.username = user;
        settings.password = secret;
        settings.version = 74;
        settings.interactive = true;
        bool world = false, retail = false, create = false, armor = false, combat = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            world |= arg == "--world";
            retail |= arg == "--retail-wire";
            create |= arg == "--create";
            armor |= arg == "--armor";
            combat |= arg == "--combat";
            if (arg == "--version173")
                settings.version = 73;
        }
        if (world)
        {
            const char *name = std::getenv("CAMELOT_CHARACTER");
            require(name, "Set CAMELOT_CHARACTER");
            settings.character = name;
            settings.realm = 2;
            settings.interactive = false;
        }
        // This explicit local test fights at Testaavb's Myrkwood starting position.
        // The Unreal subsystem instead loads the complete WorldZones catalog.
        if (combat)
        {
            settings.zone = 105;
            settings.zoneOriginX = 753664;
            settings.zoneOriginY = 868352;
        }
        Client client(settings);
        socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        require(socketHandle != INVALID_SOCKET, "socket");
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(10300);
        inet_pton(AF_INET, "127.0.0.1", &endpoint.sin_addr);
        require(connect(socketHandle, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) == 0, "local DOL connect");
        const auto flush = [&]() {
            for (auto packet : client.TakeOutgoing())
            {
                if (retail)
                {
                    const auto op = packet[9];
                    if (op == 0xf4 || op == 0xa7)
                        packet[11] = 4; // Catacombs retail type.
                    if (op == 0xfc || op == 0xbf)
                        packet.erase(packet.begin() + 10, packet.begin() + 13);
                    if (op == 0x10)
                        packet.insert(packet.begin() + 10, 0); // Retail selection prefix is four bytes.
                    const auto size = packet.size() - 12;
                    packet[0] = uint8_t(size >> 8);
                    packet[1] = uint8_t(size);
                    const auto sum = Client::Checksum(packet.data(), packet.size() - 2);
                    packet[packet.size() - 2] = uint8_t(sum >> 8);
                    packet.back() = uint8_t(sum);
                }
                size_t offset = 0;
                while (offset < packet.size())
                {
                    const int count = send(socketHandle, reinterpret_cast<const char *>(packet.data() + offset), int(packet.size() - offset), 0);
                    require(count > 0, "send");
                    offset += count;
                }
            }
        };
        const auto wait = [&](const std::function<bool()> &predicate, double seconds = 20) {
            const double until = now() + seconds;
            while (!predicate())
            {
                client.Tick(now());
                if (client.GetState() == State::Failed)
                    throw std::runtime_error(client.Error());
                flush();
                require(now() < until, "server reply timed out");
                fd_set ready;
                FD_ZERO(&ready);
                FD_SET(socketHandle, &ready);
                timeval timeout{0, 100000};
                if (select(0, &ready, nullptr, nullptr, &timeout) > 0)
                {
                    uint8_t data[8192];
                    int count = recv(socketHandle, reinterpret_cast<char *>(data), sizeof(data), 0);
                    require(count > 0, "server closed connection");
                    client.Feed(data, count, now());
                }
            }
        };
        client.Start(now());
        if (world)
        {
            wait([&]() { return client.GetState() == State::Playing; });
            if (combat)
            {
                std::cout << "Combat test entered world\n"
                          << std::flush;
                const auto spawn = client.Players().at(client.LocalObjectId());
                client.Move(spawn.x, spawn.y, spawn.z, spawn.heading, 0);
                uint16_t wolf = 0;
                double closest = 150 * 150;
                wait([&]() {
                    const auto local = client.Players().find(client.LocalObjectId());
                    if (local == client.Players().end())
                        return false;
                    for (const auto &entry : client.Players())
                    {
                        const auto &p = entry.second;
                        if (!p.npc || p.name != "young grey wolf")
                            continue;
                        const double dx = double(p.x) - local->second.x, dy = double(p.y) - local->second.y, d = dx * dx + dy * dy;
                        if (d < closest)
                        {
                            closest = d;
                            wolf = p.objectId;
                        }
                    }
                    return wolf != 0;
                });
                std::cout << "Combat wolf selected " << wolf << "\n"
                          << std::flush;
                const auto local = client.Players().at(client.LocalObjectId()), target = client.Players().at(wolf);
                const double dx = double(target.x) - local.x, dy = double(target.y) - local.y;
                const uint16_t heading = uint16_t(int(std::atan2(-dx, dy) * 4096 / (2 * 3.141592653589793)) + 4096) & 4095;
                // DOL grants 15 seconds of world-entry immunity in this test server.
                const double readyAt = now() + 17;
                double nextPose = 0;
                wait(
                    [&]() {
                        if (now() >= nextPose)
                        {
                            client.Move(local.x, local.y, local.z, heading, 0, true);
                            nextPose = now() + 1;
                        }
                        return now() >= readyAt;
                    },
                    25);
                require(client.Move(local.x, local.y, local.z, heading, 0, true), "combat facing");
                require(client.SelectTarget(wolf, true) && client.SetAttack(true), "combat start");
                bool death = false, damage = false;
                int melee = 0;
                const auto initialProgress = client.Combat().levelPermill;
                wait(
                    [&]() {
                        for (const auto &event : client.TakeCombatEvents())
                        {
                            if (event.type == CombatEventType::LineOfSight)
                                client.ReplyLineOfSight(event.source, event.target, true);
                            if (event.type == CombatEventType::Melee)
                                std::cout << "Melee " << event.source << " -> " << event.target << " health " << int(event.health) << "\n";
                            if (event.type == CombatEventType::Melee && event.target == wolf)
                            {
                                ++melee;
                                damage |= event.health < 100;
                                death |= event.health == 0;
                            }
                            if (event.type == CombatEventType::Death && event.target == wolf)
                                death = true;
                            if (event.type == CombatEventType::Message)
                            {
                                std::cout << "Combat: " << event.text << "\n";
                                if (event.text.find("You attack the young grey wolf") != std::string::npos && event.text.find(" damage!") != std::string::npos)
                                    damage = true;
                                if (event.text.find("young grey wolf dies!") != std::string::npos)
                                    death = true;
                            }
                        }
                        if (now() >= nextPose)
                        {
                            client.Move(local.x, local.y, local.z, heading, 0, true);
                            nextPose = now() + 1;
                        }
                        require(!client.Combat().dead, "player died before wolf kill");
                        return death;
                    },
                    150);
                require(damage, "server never reported melee damage");
                wait([&]() { return client.Combat().progressKnown && client.Combat().levelPermill != initialProgress; }, 10);
                wait([&]() { return !client.Combat().attacking; }, 10);
                std::cout << "Verified server wolf death, " << melee << " melee updates, XP " << client.Combat().levelPermill / 10.0 << "%\n";
                wait(
                    [&]() {
                        if (now() >= nextPose)
                        {
                            client.Move(local.x, local.y, local.z, heading, 0);
                            nextPose = now() + 1;
                        }
                        for (const auto &entry : client.Players())
                        {
                            const auto &p = entry.second;
                            if (p.npc && p.name == target.name && p.x == target.x && p.y == target.y)
                            {
                                const auto hp = client.Combat().entityHealth.find(p.objectId);
                                if (hp == client.Combat().entityHealth.end() || hp->second > 0)
                                    return true;
                            }
                        }
                        return false;
                    },
                    80);
                std::cout << "Verified starter wolf server respawn\n";
            }
            if (armor)
            {
                wait([&]() {
                    const auto p = client.Players().find(client.LocalObjectId());
                    return p != client.Players().end() && p->second.appearance.equipment.size() >= 5;
                });
                const auto &items = client.Players().at(client.LocalObjectId()).appearance.equipment;
                for (const auto expected : {std::pair<int, int>(25, 2787), {27, 2788}, {28, 2789}, {23, 2791}, {22, 2792}})
                {
                    bool found = false;
                    for (const auto &item : items)
                        if (item.slot == expected.first && item.model == expected.second)
                        {
                            found = true;
                            require(item.extension == 3, "Armor extension missing from server appearance");
                        }
                    require(found, "Inconnu armor missing from server appearance");
                }
                std::cout << "Verified five Inconnu armor models and server extensions\n";
            }
            std::cout << "DOL world entry Playing: " << settings.character << " object " << client.LocalObjectId() << "\n";
            closesocket(socketHandle);
            WSACleanup();
            return 0;
        }
        wait([&]() { return client.AllowedRealm() >= 0; });
        std::cout << "Realm discovery: " << client.AllowedRealm() << "\n";
        require(client.AllowedRealm() == 0, "Account is realm locked; not changing server policy");
        for (uint8_t realm = 1; realm <= 3; ++realm)
        {
            require(client.RequestRealm(realm, now()), "realm request");
            wait([&]() { return client.CharactersReady(); });
            std::cout << "Realm " << int(realm) << ": roster " << client.Characters().size() << "\n";
            if (!create)
                continue;
            CharacterDraft draft;
            draft.realm = realm;
            draft.slot = 0;
            draft.race = realm == 1 ? 1 : realm == 2 ? 5
                                                     : 9;
            draft.classId = realm == 1 ? 14 : realm == 2 ? 35
                                                         : 52;
            draft.model = realm == 1 ? 32 : realm == 2 ? 503
                                                       : 302;
            draft.region = realm == 1 ? 1 : realm == 2 ? 100
                                                       : 200;
            draft.stats.fill(60);
            if (realm == 2)
            {
                draft.stats[0] = draft.stats[2] = 70;
                draft.stats[1] = draft.stats[3] = 50;
            }
            draft.stats[0] += 10;
            draft.stats[1] += 10;
            draft.stats[2] += 10;
            draft.faceType = 0x20;
            draft.hairStyle = 0x10;
            draft.hairColor = 0x70;
            draft.eyeColor = 0x21;
            for (const auto &c : client.Characters())
                if (c.slot >= draft.slot)
                    draft.slot = c.slot + 1;
            require(draft.slot < 10, "No free slots; refusing to overwrite characters");
            draft.name = "Uefront";
            auto stamp = uint64_t(now() * 1000);
            for (int i = 0; i < 8; ++i)
            {
                draft.name += char('a' + stamp % 26);
                stamp /= 26;
            }
            draft.name += char('a' + realm);
            require(client.CheckCharacterName(draft.name), "check temporary name");
            wait([&]() { return client.NameResult() >= 0; });
            require(client.NameResult() == 0, "temporary name unavailable");
            require(client.CreateCharacter(draft, now()), "create");
            wait([&]() { return client.CharactersReady(); });
            require(client.Error().empty(), "server rejected creation");
            bool found = false;
            for (const auto &c : client.Characters())
                if (c.name == draft.name)
                {
                    found = true;
                    require(c.appearance.race == draft.race && c.appearance.realm == realm && c.appearance.faceType == 0x20 && c.appearance.hairStyle == 0x10 && c.stats == draft.stats, "created data differs");
                }
            require(found, "temporary character missing");
            std::cout << "Created and verified " << draft.name << "\n";
            require(client.DeleteCharacter(draft.name, now()), "delete own temporary character");
            wait([&]() { return client.CharactersReady(); });
            for (const auto &c : client.Characters())
                require(c.name != draft.name, "temporary character not removed");
            std::cout << "Removed own temporary character\n";
        }
        closesocket(socketHandle);
        WSACleanup();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        if (socketHandle != INVALID_SOCKET)
            closesocket(socketHandle);
        WSACleanup();
        return 1;
    }
}
