// SPDX-License-Identifier: GPL-2.0-or-later
#include "CamelotClient.h"
#include <algorithm>
#include <utility>
#include <cmath>
#include <atomic>

namespace camelot
{
namespace
{
uint16_t U16(const Bytes &b, size_t p)
{
    return uint16_t((b[p] << 8) | b[p + 1]);
}
uint16_t LE16(const Bytes &b, size_t p)
{
    return uint16_t(b[p] | (b[p + 1] << 8));
}
uint32_t U32(const Bytes &b, size_t p)
{
    return (uint32_t(U16(b, p)) << 16) | U16(b, p + 2);
}
void SetPlayerModel(Appearance &a, uint16_t code)
{
    a.modelCode = code;
    a.model = code & 0x7ff;
}
void Put16(Bytes &b, uint16_t v)
{
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
}
void Put32(Bytes &b, uint32_t v)
{
    Put16(b, uint16_t(v >> 16));
    Put16(b, uint16_t(v));
}
void Fixed(Bytes &b, const std::string &s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        b.push_back(i < s.size() ? uint8_t(s[i]) : 0);
}
std::string String(const Bytes &b, size_t p, size_t n)
{
    size_t end = p;
    while (end < p + n && b[end])
        ++end;
    return std::string(b.begin() + p, b.begin() + end);
}
bool Ascii(const std::string &s, size_t max)
{
    return !s.empty() && s.size() <= max && std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
bool SameName(const std::string &a, const std::string &b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) { return (x >= 'A' && x <= 'Z' ? x + 32 : x) == (y >= 'A' && y <= 'Z' ? y + 32 : y); });
}
} // namespace
Client::Client(Settings s) : settings_(std::move(s))
{
    static std::atomic<uint64_t> sessions{0};
    inventory_.revision = (++sessions) << 32;
    trainer_.revision = inventory_.revision;
}
const char *StateName(State s)
{
    static const char *names[] = {"Disconnected", "Crypt", "Login", "Characters", "Selecting", "Opening", "World", "Loading", "Initializing", "Playing", "Failed"};
    return names[static_cast<size_t>(s)];
}
uint16_t Client::Checksum(const uint8_t *b, size_t count)
{
    uint8_t a = 0x7e, c = 0x7e;
    for (size_t i = 0; i < count; ++i)
    {
        a = uint8_t(a + b[i]);
        c = uint8_t(c + a);
    }
    return uint16_t(int(c) - ((int(a) + int(c)) << 8));
}
void Client::ApplyPendingEquipment(Player &player)
{
    auto it = pendingEquipment_.find(player.objectId);
    if (it == pendingEquipment_.end())
        return;
    auto &a = player.appearance;
    const auto &pending = it->second;
    a.equipment = pending.equipment;
    a.hood = pending.hood;
    a.rightWeapon = pending.rightWeapon;
    a.leftWeapon = pending.leftWeapon;
    a.equipmentKnown = true;
    ++a.revision;
    pendingEquipment_.erase(it);
}
void Client::Transition(State s, double now)
{
    state_ = s;
    deadline_ = now + settings_.timeoutSeconds;
}
void Client::Fail(const std::string &m)
{
    weather_ = {};
    debugEnabled_ = false;
    error_ = m;
    state_ = State::Failed;
    outgoing_.clear();
    incoming_.clear();
    players_.clear();
    pendingEquipment_.clear();
    combat_ = {};
    CloseTrainer();
    trainingLevel_ = 0;
    ResetInventory();
    pendingSkills_.clear();
    combatEvents_.clear();
    receivingSkills_ = false;
    previousSpellLines_.clear();
    previousSpellCooldowns_.clear();
    objectId_ = 0;
}
void Client::RejectPacket(uint8_t opcode, size_t payloadBytes, const std::string &reason)
{
    if (state_ != State::Playing && state_ != State::Initializing && state_ != State::Loading && state_ != State::World)
    {
        Fail(reason + " (opcode " + std::to_string(opcode) + ")");
        return;
    }
    for (auto &entry : packetDiagnostics_)
        if (entry.opcode == opcode)
        {
            entry.payloadBytes = payloadBytes;
            entry.reason = reason;
            if (entry.occurrences < UINT32_MAX)
                ++entry.occurrences;
            return;
        }
    if (packetDiagnostics_.size() < 32)
        packetDiagnostics_.push_back({opcode, payloadBytes, 1, reason});
}
std::vector<PacketDiagnostic> Client::TakePacketDiagnostics()
{
    std::vector<PacketDiagnostic> result;
    result.swap(packetDiagnostics_);
    return result;
}
void Client::Disconnect()
{
    packetDiagnostics_.clear();
    weather_ = {};
    debugEnabled_ = false;
    charactersReady_ = false;
    allowedRealm_ = -1;
    nameResult_ = -1;
    checkedName_.clear();
    pendingCreation_.clear();
    state_ = State::Disconnected;
    incoming_.clear();
    outgoing_.clear();
    players_.clear();
    pendingEquipment_.clear();
    characters_.clear();
    selectedAppearance_ = {};
    combat_ = {};
    CloseTrainer();
    trainingLevel_ = 0;
    ResetInventory();
    pendingSkills_.clear();
    combatEvents_.clear();
    receivingSkills_ = false;
    previousSpellLines_.clear();
    previousSpellCooldowns_.clear();
    objectId_ = 0;
}
void Client::Start(double now)
{
    Disconnect();
    error_.clear();
    sequence_ = sessionId_ = objectId_ = 0;
    lastReceive_ = now;
    nextPing_ = now + 5;
    if (!Ascii(settings_.username, 20) || !Ascii(settings_.password, 20) || (!settings_.character.empty() && !Ascii(settings_.character, 24)) || settings_.realm < 1 || settings_.realm > 3 || (settings_.version < 72 && settings_.zone > 255) || (settings_.version != 68 && (settings_.version < 72 || settings_.version > 74)) || settings_.timeoutSeconds <= 0 || !std::isfinite(settings_.timeoutSeconds) || settings_.zoneOriginX > UINT32_MAX - 65535 || settings_.zoneOriginY > UINT32_MAX - 65535)
    {
        Fail("Use nonempty ASCII credentials (max 20), optional character (max 24), realm 1..3 and a positive timeout");
        return;
    }
    Transition(State::Crypt, now);
    // Exactly 18 bytes on the wire: GameClient.OnReceive detects legacy version from offsets 12..14.
    // F4's local CryptKeyRequesttHandler reads RC4/type/version directly, without a prefix.
    Send(0xf4, {0, 7, 1, uint8_t(settings_.version / 10), uint8_t(settings_.version % 10), 'd'});
}
void Client::Send(uint8_t op, const Bytes &data)
{
    Bytes b;
    Put16(b, uint16_t(data.size()));
    Put16(b, ++sequence_);
    Put16(b, sessionId_);
    Put16(b, 0);
    b.push_back(0);
    b.push_back(op);
    b.insert(b.end(), data.begin(), data.end());
    Put16(b, Checksum(b.data(), b.size()));
    outgoing_.push_back(std::move(b));
}
std::vector<Bytes> Client::TakeOutgoing()
{
    std::vector<Bytes> result;
    result.swap(outgoing_);
    return result;
}
void Client::Feed(const uint8_t *data, size_t count, double now)
{
    if (state_ == State::Failed || state_ == State::Disconnected || !count)
        return;
    if (count > 65536 || incoming_.size() + count > 131072)
    {
        Fail("Receive buffer limit exceeded");
        return;
    }
    incoming_.insert(incoming_.end(), data, data + count);
    size_t offset = 0;
    while (incoming_.size() - offset >= 3)
    {
        size_t length = size_t(U16(incoming_, offset)) + 3;
        if (length > 8192)
        {
            Fail("Invalid server frame length");
            return;
        }
        if (incoming_.size() - offset < length)
            break;
        uint8_t op = incoming_[offset + 2];
        Bytes payload(incoming_.begin() + offset + 3, incoming_.begin() + offset + length);
        offset += length;
        lastReceive_ = now;
        Handle(op, payload, now);
        if (state_ == State::Failed || state_ == State::Disconnected)
            return;
    }
    incoming_.erase(incoming_.begin(), incoming_.begin() + offset);
}
void Client::Tick(double now)
{
    TickTrainer(now);
    if (state_ == State::Disconnected || state_ == State::Failed)
        return;
    if (state_ != State::Playing && deadline_ > 0 && now >= deadline_)
    {
        Fail(std::string("Timed out waiting in ") + StateName(state_));
        return;
    }
    if (state_ == State::Playing && now - lastReceive_ >= settings_.timeoutSeconds)
    {
        Fail("Server stopped responding");
        return;
    }
    if (now >= nextPing_ && state_ != State::Crypt)
    {
        Bytes b(4, 0);
        Put32(b, uint32_t(uint64_t(now * 1000)));
        Send(0xa3, b);
        nextPing_ = now + 5;
    }
}
void Client::Handle(uint8_t op, const Bytes &b, double now)
{
    if (HandleTrainer(op, b, now) || HandleInventory(op, b, now) || HandleCombat(op, b, now))
        return;
    const auto need = [&](size_t n) {
        if (b.size() < n)
        {
            RejectPacket(op, b.size(), "Truncated server payload");
            return false;
        }
        return true;
    };
    switch (op)
    {
    case 0x92: // PacketLib168.SendWeather: big-endian, 16-byte payload.
        if (!need(16))
            return;
        weather_.known = true;
        weather_.region = region_;
        weather_.receivedAt = now;
        weather_.position = U32(b, 0);
        weather_.width = U32(b, 4);
        weather_.fogDiffusion = U16(b, 8);
        weather_.speed = U16(b, 10);
        weather_.intensity = U16(b, 12);
        break;
    case 0x21: // DOL SendDebugMode: authorization, followed by a reserved byte.
        if (!need(2))
            return;
        debugEnabled_ = b[0] != 0;
        break;
    case 0x22:
        if (state_ != State::Crypt)
            break;
        if (!need(5))
            return;
        // The flag advertises RC4 availability. Our RC4=0 request leaves PacketEncoding168
        // in NotEncrypted, even when the server property advertises support.
        if (b.size() != 5)
        {
            Fail("RSA crypt-key extensions are not supported by this initial client");
            return;
        }
        {
            Bytes login = {0, 7, 1, settings_.version, 'd'};
            Fixed(login, settings_.password, 20);
            login.resize(login.size() + 7 + 12 + 31, 0);
            Fixed(login, settings_.username, 20);
            Send(0xa7, login);
            Transition(State::Login, now);
        }
        break;
    case 0x2c:
        if (need(1))
            Fail("Login denied, code " + std::to_string(b[0]));
        return;
    case 0x2a:
        if (state_ == State::Login)
        {
            if (settings_.interactive)
            {
                Bytes request = {1, settings_.version, 'd'};
                Fixed(request, settings_.username + "-X", 24);
                Send(0xfc, request);
                Transition(State::Characters, now);
            }
            else
                RequestRealm(settings_.realm, now);
        }
        break;
    case 0xfe:
        if (state_ == State::Characters && need(1) && b[0] <= 3)
        {
            allowedRealm_ = b[0];
            deadline_ = 0;
            if (allowedRealm_)
                RequestRealm(uint8_t(allowedRealm_), now);
        }
        break;
    case 0xcc:
        if (state_ == State::Characters && need(54) && String(b, 0, 30) == checkedName_)
            nameResult_ = b[50];
        break;
    case 0xfd:
        if (state_ != State::Characters)
            break;
        if (!need(24 + (settings_.version >= 73 ? 10 : 8) * 184))
            return;
        {

            characters_.clear();
            for (size_t slot = 0; slot < size_t(settings_.version >= 73 ? 10 : 8); ++slot)
            {
                const size_t p = 24 + slot * 184;
                CharacterOverview c;
                c.name = String(b, p, 24);
                if (c.name.empty())
                    continue;
                c.slot = uint8_t(slot);
                c.location = String(b, p + 48, 24);
                c.className = String(b, p + 72, 24);
                c.raceName = String(b, p + 96, 24);
                auto &a = c.appearance;
                a.level = b[p + 120];
                a.classId = b[p + 121];
                a.realm = b[p + 122];
                if (settings_.version >= 72)
                {
                    a.eyeSize = b[p + 25];
                    a.lipSize = b[p + 26];
                    a.eyeColor = b[p + 27];
                    a.hairColor = b[p + 28];
                    a.faceType = b[p + 29];
                    a.hairStyle = b[p + 30];
                    a.hood = b[p + 32] & 1;
                    a.customisationStep = b[p + 33];
                    if (settings_.version >= 74)
                        a.moodType = b[p + 34];
                }
                a.race = uint8_t((b[p + 123] & 15) | ((b[p + 123] & 64) >> 2));
                a.gender = uint8_t((b[p + 123] >> 4) & 3);
                SetPlayerModel(a, LE16(b, p + 124));
                a.revision = 1;
                c.region = b[p + 126];
                c.expansion = b[p + 127];
                c.databaseId = U32(b, p + 128);
                for (size_t i = 0; i < 8; ++i)
                {
                    c.stats[i] = b[p + 132 + i];
                    const auto model = LE16(b, p + 140 + i * 2);
                    if (model)
                        a.equipment.push_back({uint8_t(21 + i), model, uint16_t(i == 3 ? 0 : LE16(b, p + 156 + i * 2)), 0});
                }
                for (size_t i = 0; i < 4; ++i)
                {
                    const auto model = LE16(b, p + 172 + i * 2);
                    if (model)
                        a.equipment.push_back({uint8_t(10 + i), model, uint16_t(i == 1 ? LE16(b, p + 162) : 0), 0});
                }
                a.rightWeapon = b[p + 180];
                a.leftWeapon = b[p + 181];
                a.equipmentKnown = true;
                if (settings_.version >= 72)
                    for (auto &item : a.equipment)
                    {
                        if (item.slot == 25)
                            item.extension = b[p + 32] >> 4;
                        if (item.slot == 22)
                            item.extension = b[p + 31] >> 4;
                        if (item.slot == 23)
                            item.extension = b[p + 31] & 15;
                    }
                characters_.push_back(std::move(c));
            }
            charactersReady_ = true;
            ++rosterRevision_;
            deadline_ = 0;
            if (!pendingCreation_.empty())
            {
                const bool found = std::any_of(characters_.begin(), characters_.end(), [&](const CharacterOverview &c) { return SameName(c.name, pendingCreation_); });
                if (!found)
                    error_ = "Server rejected character creation; check name, race/class, realm and attribute points";
                pendingCreation_.clear();
            }
            if (!settings_.interactive && !settings_.character.empty())
                SelectCharacter(settings_.character, now);
        }
        break;
    case 0x28:
        if (!need(2))
            return;
        sessionId_ = uint16_t(b[0] | (b[1] << 8));
        if (state_ == State::Selecting)
        {
            Send(0xbf, {1, settings_.version, 'd', 0});
            Transition(State::Opening, now);
        }
        break;
    case 0x2d:
        if (state_ == State::Opening)
        {
            if (!need(1))
                return;
            if (b[0] != 0)
            {
                Fail("Game open rejected");
                return;
            }
            Send(0xd4, {});
            Transition(State::World, now);
        }
        break;
    case 0x20:
        if (!need(16))
            return;
        if (state_ != State::World && state_ != State::Loading && state_ != State::Initializing && state_ != State::Playing)
            break;
        {
            Player p;
            p.objectId = U16(b, 0);
            p.sessionId = sessionId_;
            p.z = U16(b, 2);
            p.x = U32(b, 4);
            p.y = U32(b, 8);
            p.heading = U16(b, 12) & 4095;
            if (settings_.version >= 74 && b.size() >= 22)
            {
                const uint16_t newRegion = U16(b, 20);
                if (newRegion != region_)
                    weather_ = {};
                region_ = newRegion;
            }
            const bool initialize = state_ == State::World || (state_ == State::Loading && initializeAfterLoad_);
            if (!ResolveDestination(p.x, p.y, now, initialize))
                return;
            p.zone = settings_.zone;
            p.name = settings_.character;
            p.local = true;
            auto existing = players_.find(p.objectId);
            p.appearance = existing != players_.end() ? existing->second.appearance : selectedAppearance_;
            if (objectId_ && objectId_ != p.objectId)
                players_.erase(objectId_);
            ApplyPendingEquipment(p);
            objectId_ = p.objectId;
            players_[objectId_] = p;
            if (state_ == State::World)
            {
                Send(0xe8, {});
                Transition(State::Initializing, now);
            }
        }
        break;
    case 0x2b:
        if (!need(1))
            return;
        if (state_ == State::Initializing)
        {
            // DOL sends inventory/skill initialization before this reply, omitting empty item collections.
            // This ordered TCP barrier also confirms an empty equipment list or backpack.
            inventory_.equipmentKnown = true;
            inventory_.backpackKnown = true;
            ++inventory_.revision;
            Transition(State::Playing, now);
        }
        break;
    case 0x04:
        if (!need(14))
            return;
        {
            auto it = players_.find(U16(b, 8));
            if (it != players_.end())
            {
                if (U32(b, 0) || U32(b, 4))
                {
                    const auto id = it->first;
                    if (id == objectId_ && !ResolveDestination(U32(b, 0), U32(b, 4), now, false))
                        return;
                    it = players_.find(id);
                    if (it == players_.end())
                        break;
                    it->second.x = U32(b, 0);
                    it->second.y = U32(b, 4);
                    it->second.z = U16(b, 10);
                    it->second.zone = settings_.zone;
                }
                it->second.heading = U16(b, 12) & 4095;
            }
        }
        break;
    case 0xd4:
        if (!need(23))
            return;
        // Only this zone can be mapped with the configured origin. Never misplace another zone.
        if (b[8] != settings_.zone)
            break;
        if (!need(23 + b[22]))
            return;
        if (b.size() > 23 + b[22] && !need(24 + b[22] + b[23 + b[22]]))
            return;
        {
            Player p;
            p.sessionId = U16(b, 0);
            p.objectId = U16(b, 2);
            p.zone = b[8];
            p.x = settings_.zoneOriginX + U16(b, 4);
            p.y = settings_.zoneOriginY + U16(b, 6);
            p.z = U16(b, 10);
            p.heading = U16(b, 12) & 4095;
            p.name = String(b, 23, b[22]);
            if (b.size() > 23 + b[22])
                p.guildName = String(b, 24 + b[22], b[23 + b[22]]);
            p.local = p.sessionId == sessionId_;
            auto previous = players_.find(p.objectId);
            if (previous != players_.end())
                p.appearance = previous->second.appearance;
            else if (p.local)
                p.appearance = selectedAppearance_;
            ApplyPendingEquipment(p);
            players_[p.objectId] = p;
            auto &created = players_[p.objectId];
            SetPlayerModel(created.appearance, U16(b, 14));
            created.appearance.realm = b[18];
            created.appearance.level = b[19];
            ++created.appearance.revision;
        }
        break;
    case 0x4b: // Catacombs PlayerCreate172/174: full face settings precede names.
    {
        const size_t nameOffset = settings_.version >= 74 ? 26 : 24;
        if (!need(nameOffset + 1))
            return;
        if (!need(nameOffset + 1 + b[nameOffset]))
            return;
        const size_t guildOffset = nameOffset + 1 + b[nameOffset];
        if (b.size() > guildOffset && !need(guildOffset + 1 + b[guildOffset]))
            return;
        if (U16(b, 8) != settings_.zone)
            break;
        Player p;
        p.sessionId = U16(b, 0);
        p.objectId = U16(b, 2);
        p.z = U16(b, 6);
        p.zone = U16(b, 8);
        p.x = settings_.zoneOriginX + U16(b, 10);
        p.y = settings_.zoneOriginY + U16(b, 12);
        p.heading = U16(b, 14) & 4095;
        p.name = String(b, nameOffset + 1, b[nameOffset]);
        if (b.size() > guildOffset)
            p.guildName = String(b, guildOffset + 1, b[guildOffset]);
        p.local = p.sessionId == sessionId_;
        const auto previous = players_.find(p.objectId);
        if (previous != players_.end())
            p.appearance = previous->second.appearance;
        else if (p.local)
            p.appearance = selectedAppearance_;
        auto &a = p.appearance;
        SetPlayerModel(a, U16(b, 4));
        a.eyeSize = b[16];
        a.lipSize = b[17];
        size_t i = 18;
        if (settings_.version >= 74)
            a.moodType = b[i++];
        a.eyeColor = b[i++];
        a.level = b[i++];
        a.hairColor = b[i++];
        a.faceType = b[i++];
        a.hairStyle = b[i++];
        a.realm = (b[i] >> 2) & 3;
        ++a.revision;
        ApplyPendingEquipment(p);
        players_[p.objectId] = p;
    }
    break;
    case 0xda: // PacketLib168/171: Catacombs adds four flag/reserved bytes before names.
        if (!need(settings_.version >= 71 ? 30 : 26))
            return;
        {
            size_t cursor = settings_.version >= 71 ? 28 : 24;
            if (!need(cursor + 1 + b[cursor]))
                return;
            const auto name = String(b, cursor + 1, b[cursor]);
            cursor += 1 + b[cursor];
            if (!need(cursor + 1))
                return;
            if (!need(cursor + 1 + b[cursor]))
                return;
            Player p;
            p.objectId = U16(b, 0);
            p.speed = U16(b, 2) & 2047;
            p.heading = U16(b, 4) & 4095;
            p.z = U16(b, 6);
            p.x = U32(b, 8);
            p.y = U32(b, 12);
            p.zone = settings_.zone;
            p.npc = true;
            p.name = name;
            p.guildName = String(b, cursor + 1, b[cursor]);
            p.flying = (b[22] & 0x20) != 0;
            if (p.x < settings_.zoneOriginX || p.y < settings_.zoneOriginY || p.x - settings_.zoneOriginX > 65535 || p.y - settings_.zoneOriginY > 65535)
                break;
            const auto previous = players_.find(p.objectId);
            if (previous != players_.end())
                p.appearance = previous->second.appearance;
            if (previous == players_.end())
                combat_.entityHealth.erase(p.objectId); // A respawn may reuse a dead NPC's ID.
            p.appearance.modelCode = U16(b, 18);
            p.appearance.model = p.appearance.modelCode & 32767;
            p.appearance.size = b[20];
            p.appearance.level = b[21];
            p.appearance.realm = b[22] >> 6;
            p.appearance.revision = previous == players_.end() ? 1 : previous->second.appearance.revision + 1;
            p.positionTime = now;
            p.targetX = p.x;
            p.targetY = p.y;
            p.targetZ = p.z;
            ApplyPendingEquipment(p);
            players_[p.objectId] = p;
        }
        break;
    case 0xa1: // PacketLib168.SendObjectUpdate, including NPC destinations.
        if (!need(24))
            return;
        {
            auto it = players_.find(U16(b, 16));
            if (it == players_.end() || !it->second.npc)
                break;
            const uint16_t zone = b[22] | ((b[21] & 4) << 6);
            if (zone != settings_.zone)
            {
                players_.erase(it);
                break;
            }
            auto &p = it->second;
            if (b[20] <= 100)
                combat_.entityHealth[p.objectId] = b[20];
            p.speed = U16(b, 0) & 2047;
            p.flying = (b[21] & 0x20) != 0;
            p.heading = U16(b, 2) & 4095;
            p.x = settings_.zoneOriginX + U16(b, 4);
            p.y = settings_.zoneOriginY + U16(b, 8);
            p.z = U16(b, 12);
            p.positionTime = now;
            const uint16_t targetZone = b[23] | ((b[21] & 8) << 5);
            // Stop at the current position if the destination is in another zone.
            p.targetX = targetZone == settings_.zone ? settings_.zoneOriginX + U16(b, 6) : p.x;
            p.targetY = targetZone == settings_.zone ? settings_.zoneOriginY + U16(b, 10) : p.y;
            p.targetZ = targetZone == settings_.zone ? U16(b, 14) : p.z;
        }
        break;
    case 0x15: // PacketLib168 equipment models carry optional color/emblem/effect payloads.
        if (!need(5))
            return;
        {
            std::vector<Equipment> items;
            size_t p = 5;
            for (size_t i = 0; i < b[4]; ++i)
            {
                if (!need(p + 3))
                    return;
                Equipment e;
                e.slot = b[p++];
                const uint16_t flags = U16(b, p);
                p += 2;
                e.model = flags & 8191;
                if (settings_.version >= 72 && (e.slot < 10 || e.slot > 13))
                {
                    if (!need(p + 1))
                        return;
                    e.extension = b[p++];
                }
                if (flags & 0x8000)
                {
                    if (!need(p + 2))
                        return;
                    e.color = U16(b, p);
                    p += 2;
                }
                else if (flags & 0x4000)
                {
                    if (!need(p + 1))
                        return;
                    e.color = b[p++];
                }
                if (flags & 0x2000)
                {
                    if (!need(p + 2))
                        return;
                    e.effect = U16(b, p);
                    p += 2;
                }
                items.push_back(e);
            }
            const auto id = U16(b, 0);
            auto it = players_.find(id);
            if (it == players_.end() && pendingEquipment_.size() >= 4096 && !pendingEquipment_.count(id))
                break;
            {
                auto &a = it != players_.end() ? it->second.appearance : pendingEquipment_[id];
                a.equipmentKnown = true;
                a.equipment = std::move(items);
                a.hood = b[2] & 1;
                a.rightWeapon = b[3] & 15;
                a.leftWeapon = b[3] >> 4;
                ++a.revision;
            }
        }
        break;
    case 0xdb:
        if (!need(8))
            return;
        {
            auto it = players_.find(U16(b, 0));
            if (it != players_.end())
            {
                auto &a = it->second.appearance;
                if (it->second.npc)
                {
                    a.modelCode = U16(b, 2);
                    a.model = a.modelCode & 32767;
                }
                else
                    SetPlayerModel(a, U16(b, 2));
                a.size = b[4];
                ++a.revision;
            }
        }
        break;
    case 0xa9:
        if (!need(14))
            return;
        {
            const auto sid = U16(b, 0);
            for (auto it = players_.begin(); it != players_.end(); ++it)
            {
                if (it->second.sessionId != sid)
                    continue;
                // Forged packets write [zone,0]; movement relay writes a BE ushort.
                // Both are unambiguous for the supported 8-bit legacy zone IDs.
                if ((b[10] && b[11]) || uint16_t(b[10] ? b[10] : b[11]) != settings_.zone)
                {
                    players_.erase(it);
                    break;
                }
                auto &p = it->second;
                p.z = U16(b, 4);
                p.x = settings_.zoneOriginX + U16(b, 6);
                p.y = settings_.zoneOriginY + U16(b, 8);
                p.heading = U16(b, 12) & 4095;
                p.speed = U16(b, 2) & 511;
                if (b.size() >= 18)
                {
                    p.attackModeKnown = true;
                    p.attacking = (b[17] & 128) != 0;
                }
                break;
            }
        }
        break;
    case 0xba:
        if (!need(4))
            return;
        for (auto &entry : players_)
            if (entry.second.sessionId == U16(b, 0))
            {
                entry.second.heading = U16(b, 2) & 4095;
                if (b.size() >= 9)
                {
                    entry.second.attackModeKnown = true;
                    entry.second.attacking = (b[8] & 128) != 0;
                }
            }
        break;
    case 0xe1:
    case 0xa2:
        if (need(2))
        {
            players_.erase(U16(b, 0));
            pendingEquipment_.erase(U16(b, 0));
            combat_.entityHealth.erase(U16(b, 0));
            if (combat_.target == U16(b, 0))
            {
                combat_.target = 0;
                combat_.attacking = false;
            }
        }
        break;
    case 0xa4:
        Fail("Server requested quit");
        break;
    case 0xb7:
        if (!need(2))
            return;
        if (state_ == State::Playing || state_ == State::Loading || state_ == State::Initializing)
        {
            CloseTrainer();
            weather_ = {};
            region_ = U16(b, 0);
            const auto local = players_.find(objectId_);
            if (local != players_.end())
                selectedAppearance_ = local->second.appearance;
            players_.clear();
            pendingEquipment_.clear();
            Send(0xd4, {});
            Transition(State::World, now);
        }
        break;
    default:
        break; // Other game/UI packets are outside the initial position prototype.
    }
}
bool Client::RequestRealm(uint8_t realm, double now)
{
    if ((state_ != State::Login && state_ != State::Characters) || realm < 1 || realm > 3 || (allowedRealm_ > 0 && realm != allowedRealm_))
        return false;
    settings_.realm = realm;
    charactersReady_ = false;
    characters_.clear();
    error_.clear();
    checkedName_.clear();
    nameResult_ = -1;
    const char *suffix[] = {"", "-S", "-N", "-H"};
    Bytes request = {1, settings_.version, 'd'};
    Fixed(request, settings_.username + suffix[realm], 24);
    Send(0xfc, request);
    Transition(State::Characters, now);
    return true;
}
bool Client::CheckCharacterName(const std::string &name)
{
    if (state_ != State::Characters || !charactersReady_ || !Ascii(name, 24))
        return false;
    checkedName_ = name;
    nameResult_ = -1;
    Bytes b;
    Fixed(b, name, 30);
    Send(0xcb, b);
    return true;
}
bool Client::CreateCharacter(const CharacterDraft &d, double now)
{
    if (state_ != State::Characters || !charactersReady_ || d.realm != settings_.realm || d.slot >= (settings_.version >= 73 ? 10 : 8) || !Ascii(d.name, 24) || d.realm < 1 || d.realm > 3 || d.name.size() < 3 || d.gender > 1 || d.race < 1 || d.race > 31 || !d.classId || !d.model)
        return false;
    // DOL rejects malformed names before validating the rest of a creation packet.
    if (d.name.front() < 'A' || d.name.front() > 'Z' || !std::all_of(d.name.begin(), d.name.end(), [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }))
        return false;
    if (std::any_of(characters_.begin(), characters_.end(), [&](const CharacterOverview &c) { return c.slot == d.slot; }))
        return false;
    if (checkedName_ != d.name || nameResult_ != 0)
        return false; // Wait for authoritative name validation.
    Bytes b;
    const char *suffix[] = {"", "-S", "-N", "-H"};
    Fixed(b, settings_.username + suffix[d.realm], 24);
    const size_t start = b.size();
    b.resize(start + (settings_.version >= 73 ? 10 : 8) * 184, 0);
    const size_t p = start + d.slot * 184;
    std::copy(d.name.begin(), d.name.end(), b.begin() + p);
    b[p + 24] = 1;
    b[p + 25] = d.eyeSize;
    b[p + 26] = d.lipSize;
    b[p + 27] = d.eyeColor;
    b[p + 28] = d.hairColor;
    b[p + 29] = d.faceType;
    b[p + 30] = d.hairStyle;
    b[p + 34] = d.moodType;
    b[p + 43] = 0x23;
    b[p + 44] = 0x45;
    b[p + 45] = 0x67;
    b[p + 46] = 0x89; // Create, network order.
    b[p + 120] = 1;
    b[p + 121] = d.classId;
    b[p + 122] = d.realm;
    b[p + 123] = (d.race & 15) | ((d.race & 16) << 2) | (d.gender << 4) | (d.isles ? 128 : 0);
    const uint16_t creationModel = d.model | ((d.model & 0x1800) ? 0 : 0x1000);
    b[p + 124] = uint8_t(creationModel);
    b[p + 125] = uint8_t(creationModel >> 8);
    const uint8_t classicRegions[] = {0, 1, 100, 200}, islesRegions[] = {0, 51, 151, 181};
    // The SI flag alone does not select a destination in DOL; transmit its region too.
    b[p + 126] = d.isles && (d.region == 0 || d.region == classicRegions[d.realm]) ? islesRegions[d.realm] : d.region;
    std::copy(d.stats.begin(), d.stats.end(), b.begin() + p + 132);
    b[p + 180] = b[p + 181] = 255;
    b[p + 183] = d.stats[2];
    Send(0xff, b);
    pendingCreation_ = d.name;
    charactersReady_ = false;
    error_.clear();
    deadline_ = now + settings_.timeoutSeconds;
    return true;
}
bool Client::DeleteCharacter(const std::string &name, double now)
{
    if (state_ != State::Characters || !charactersReady_)
        return false;
    auto c = std::find_if(characters_.begin(), characters_.end(), [&](const CharacterOverview &v) { return SameName(name, v.name); });
    if (c == characters_.end())
        return false;
    Bytes b;
    Fixed(b, c->name, 30);
    Send(0xc0, b);
    return RequestRealm(settings_.realm, now);
}
bool Client::SelectCharacter(const std::string &name, double now)
{
    if (state_ != State::Characters || !charactersReady_ || deadline_ != 0 || !Ascii(name, 24))
        return false;
    const auto found = std::find_if(characters_.begin(), characters_.end(), [&](const CharacterOverview &c) { return SameName(c.name, name); });
    if (found == characters_.end())
    {
        error_ = "Character not found in configured realm: " + name;
        return false;
    }
    weather_ = {};
    debugEnabled_ = false;
    selectedAppearance_ = found->appearance;
    region_ = found->region;
    players_.clear();
    pendingEquipment_.clear();
    objectId_ = 0;
    settings_.character = found->name;
    error_.clear();
    Bytes select = {1, settings_.version, 'd'};
    if (settings_.version >= 74)
        select.push_back(0);
    Fixed(select, found->name, 28);
    Send(0x10, select);
    Transition(State::Selecting, now);
    return true;
}
bool Client::ResolveDestination(uint32_t x, uint32_t y, double now, bool initialize)
{
    if (settings_.zones.empty())
        return true; // Explicit legacy/mock coordinate configuration.
    const Zone *destination = nullptr;
    for (const auto &zone : settings_.zones)
    {
        if (zone.region != region_ || x < zone.x || y < zone.y || x - zone.x >= zone.width || y - zone.y >= zone.height)
            continue;
        if (destination)
        {
            Fail("Destination coordinates match multiple zones");
            return false;
        }
        destination = &zone;
    }
    if (!destination)
    {
        Fail("Destination has no known client zone");
        return false;
    }
    if (!destination->outdoor)
    {
        Fail("Dungeon destinations are not supported yet");
        return false;
    }
    if (initialize || settings_.zone != destination->id || settings_.zoneOriginX != destination->x || settings_.zoneOriginY != destination->y || !destinationRevision_)
    {
        for (auto it = players_.begin(); it != players_.end();)
            if (it->first != objectId_)
                it = players_.erase(it);
            else
                ++it;
        pendingEquipment_.clear();
        settings_.zone = destination->id;
        CloseTrainer();
        settings_.zoneOriginX = destination->x;
        settings_.zoneOriginY = destination->y;
        ++destinationRevision_;
        initializeAfterLoad_ = initialize;
        state_ = State::Loading;
        deadline_ = 0; // Loading is renderer-owned; socket keepalives continue.
    }
    return true;
}
bool Client::ConfirmWorldReady(double now)
{
    if (state_ != State::Loading)
        return false;
    if (initializeAfterLoad_)
    {
        Send(0xe8, {});
        Transition(State::Initializing, now);
    }
    else
    {
        state_ = State::Playing;
        deadline_ = 0;
    }
    initializeAfterLoad_ = false;
    return true;
}
bool Client::SendCommand(const std::string &command)
{
    if (state_ != State::Playing || command.empty() || command.size() > 254)
        return false;
    // Client 1.68..1.74 has eight reserved prefix bytes, then a 255-byte string.
    Bytes b(263, 0);
    for (size_t i = 0; i < command.size(); ++i)
        b[8 + i] = uint8_t(command[i]);
    Send(0xaf, b);
    return true;
}
bool Client::Move(uint32_t x, uint32_t y, uint16_t z, uint16_t heading, uint16_t speed, bool targetVisible, bool groundVisible, bool flying)
{
    if (state_ != State::Playing || !objectId_ || !z)
        return false;
    if (!settings_.zones.empty() && !ResolveDestination(x, y, lastReceive_, false))
        return false;
    if (x < settings_.zoneOriginX || y < settings_.zoneOriginY || x - settings_.zoneOriginX > 65535 || y - settings_.zoneOriginY > 65535)
        return false;
    if (flying && !debugEnabled_)
        return false;
    Bytes b;
    Put16(b, objectId_);
    Put16(b, (speed & 511) | (flying ? (3 << 10) : 0));
    Put16(b, z);
    Put16(b, uint16_t(x - settings_.zoneOriginX));
    Put16(b, uint16_t(y - settings_.zoneOriginY));
    Put16(b, settings_.zone);
    Put16(b, heading & 4095);
    Put16(b, 0);
    b.push_back(uint8_t((targetVisible ? 0x10 : 0) | (groundVisible ? 0x08 : 0)));
    Send(0xa9, b);
    auto it = players_.find(objectId_);
    if (it != players_.end())
    {
        auto &p = it->second;
        p.x = x;
        p.y = y;
        p.z = z;
        p.zone = settings_.zone;
        p.heading = heading & 4095;
        p.speed = speed & 511;
    }
    return true;
}
} // namespace camelot
