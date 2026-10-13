// SPDX-License-Identifier: GPL-2.0-or-later
#include "CamelotCombat.h"
#include "CamelotClient.h"
#include <algorithm>
#include <utility>
namespace camelot
{
bool Client::Release()
{
    if (GetState() != State::Playing || !combat_.dead)
        return false;
    // PlayerCommandHandler skips the eight-byte Catacombs command prefix.
    Bytes command(8, 0);
    const std::string text = "&release";
    command.insert(command.end(), text.begin(), text.end());
    command.push_back(0);
    Send(0xaf, command);
    return true; // DOL decides release delay, bind destination and penalties.
}
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
uint32_t Dword(const Bytes &b, size_t p)
{
    return (uint32_t(Word(b, p)) << 16) | Word(b, p + 2);
}
void Long(Bytes &b, uint32_t n)
{
    Put(b, uint16_t(n >> 16));
    Put(b, uint16_t(n));
}
bool Pascal(const Bytes &b, size_t &p, std::string &s)
{
    if (p >= b.size())
        return false;
    size_t n = b[p++];
    if (n > b.size() - p)
        return false;
    s.assign(b.begin() + p, b.begin() + p + n);
    p += n;
    return true;
}
uint16_t Flags(const ActionContext &c)
{
    return uint16_t((c.speed & 511) | (c.backward ? 0x200 : 0) | (c.strafing ? 0x4000 : 0) | (c.targetVisible ? 0xa000 : 0) | (c.groundVisible ? 0x1000 : 0));
}
size_t NonSpec(const CombatState &s)
{
    auto i = std::find_if(s.skills.begin(), s.skills.end(), [](const Skill &a) { return a.type != SkillType::Specialization; });
    return size_t(i - s.skills.begin());
}
} // namespace
void Client::QueueCombatEvent(CombatEvent e)
{
    if (combatEvents_.size() >= 256)
        combatEvents_.erase(combatEvents_.begin());
    combatEvents_.push_back(std::move(e));
}
std::vector<CombatEvent> Client::TakeCombatEvents()
{
    std::vector<CombatEvent> result;
    result.swap(combatEvents_);
    return result;
}
bool Client::SelectTarget(uint16_t id, bool visible)
{
    if (state_ != State::Playing || (id && !players_.count(id)))
        return false;
    Bytes b;
    Put(b, id);
    Put(b, uint16_t((visible ? 0x6000 : 0) | (combat_.attacking ? 1 : 0)));
    Send(0xb0, b);
    // Normal target selection has no acknowledgement. ChangeTarget can override this UI selection.
    combat_.target = id;
    return true;
}
bool Client::SetAttack(bool enabled)
{
    if (state_ != State::Playing || (enabled && (combat_.dead || !combat_.target)))
        return false;
    Send(0x74, {uint8_t(enabled), 0});
    return true;
}
bool Client::UseSkill(uint16_t index, const ActionContext &context, double now)
{
    if (state_ != State::Playing || combat_.dead || !combat_.skillsReady || receivingSkills_ || index >= combat_.skills.size() || combat_.skills[index].type == SkillType::Specialization)
        return false;
    const size_t first = NonSpec(combat_);
    if (index < first || index - first > 255)
        return false;
    auto cool = combat_.skillCooldowns.find(uint16_t(index - first));
    if (cool != combat_.skillCooldowns.end() && cool->second > now)
        return false;
    // Synchronize pose first: UseSkill itself has no position fields in 1.68..1.74.
    if (!Move(context.x, context.y, context.z, context.heading, context.speed, context.targetVisible, context.groundVisible))
        return false;
    Bytes b;
    Put(b, Flags(context));
    b.push_back(uint8_t(index - first));
    b.push_back(1);
    Send(0xbb, b);
    return true;
}
bool Client::UseSpell(uint8_t line, uint8_t index, const ActionContext &context, double now)
{
    const auto it = combat_.spellLines.find(line);
    if (state_ != State::Playing || combat_.dead || receivingSkills_ || it == combat_.spellLines.end() || index >= it->second.spells.size())
        return false;
    auto cool = combat_.spellCooldowns.find(uint16_t((line << 8) | index));
    if (cool != combat_.spellCooldowns.end() && cool->second > now)
        return false;
    if (!Move(context.x, context.y, context.z, context.heading, context.speed, context.targetVisible, context.groundVisible))
        return false;
    Bytes b;
    Put(b, Flags(context));
    Put(b, context.heading & 4095);
    if (settings_.version > 71)
    {
        Put(b, uint16_t(context.x - settings_.zoneOriginX));
        Put(b, uint16_t(context.y - settings_.zoneOriginY));
        Put(b, settings_.zone);
        Put(b, context.z);
    }
    b.push_back(it->second.spells[index].level);
    b.push_back(line);
    Send(0x7d, b);
    return true;
}
bool Client::SetGroundTarget(uint32_t x, uint32_t y, uint16_t z, bool visible)
{
    if (state_ != State::Playing || x > INT32_MAX || y > INT32_MAX)
        return false;
    Bytes b;
    Long(b, x);
    Long(b, y);
    Long(b, z);
    Put(b, visible ? 0x100 : 0);
    Send(0xec, b);
    combat_.groundX = x;
    combat_.groundY = y;
    combat_.groundZ = z;
    combat_.groundKnown = true;
    return true;
}
bool Client::ReplyLineOfSight(uint16_t source, uint16_t target, bool visible)
{
    if (state_ != State::Playing)
        return false;
    Bytes b;
    Put(b, source);
    Put(b, target);
    Put(b, visible ? 0x100 : 0);
    Put(b, 0);
    Send(0xd0, b);
    return true;
}
bool Client::CancelEffect(uint16_t id)
{
    if (state_ != State::Playing || !std::any_of(combat_.effects.begin(), combat_.effects.end(), [&](const Effect &e) { return e.id == id && e.index != 255; }))
        return false;
    Bytes b;
    Put(b, id);
    Send(0xf8, b);
    return true;
}
bool Client::HandleCombat(uint8_t op, const Bytes &b, double now)
{
    const auto bad = [&]() {
        // An incomplete catalog invalidates its assembly, not the last confirmed skills.
        if (op == 0x16)
        {
            pendingSkills_.clear();
            receivingSkills_ = false;
        }
        RejectPacket(op, b.size(), "Malformed combat payload");
        return true;
    };
    CombatEvent e;
    switch (op)
    {
    case 0x91: // PacketLib168.SendUpdatePoints (also used by Catacombs).
        if (b.size() < 16)
            return bad();
        combat_.realmPoints = Dword(b, 0);
        combat_.levelPermill = std::min<uint16_t>(1000, Word(b, 4));
        combat_.skillPoints = Word(b, 6);
        combat_.bountyPoints = Dword(b, 8);
        combat_.realmSkillPoints = Word(b, 12);
        combat_.progressKnown = true;
        return true;
    case 0x16: {
        if (b.empty())
            return bad();
        if (b[0] != 1 && b[0] != 2)
            return false;
        if (b.size() < 4)
            return bad();
        size_t p = 4;
        if (b[0] == 1)
        {
            if (b[2] != 3 && b[2] != 99)
                return false;
            std::vector<Skill> chunk;
            for (size_t i = 0; i < b[1]; ++i)
            {
                if (b.size() - p < 8)
                    return bad();
                Skill s;
                s.index = uint16_t(b[3] + i);
                s.level = b[p];
                s.type = SkillType(b[p + 1]);
                s.special = Word(b, p + 2);
                s.bonus = b[p + 4];
                s.icon = Word(b, p + 5);
                p += 7;
                if (!Pascal(b, p, s.name))
                    return bad();
                chunk.push_back(std::move(s));
            }
            if (p != b.size() || size_t(b[3]) + chunk.size() > 256)
                return bad();
            if (b[3] == 0)
            {
                pendingSkills_.clear();
                receivingSkills_ = true;
            }
            if (!receivingSkills_ || pendingSkills_.size() != b[3])
                return bad();
            pendingSkills_.insert(pendingSkills_.end(), chunk.begin(), chunk.end());
            if (b[2] == 3)
            {
                // Cooldowns are indexed by list position: preserve only when identities match.
                bool same = pendingSkills_.size() == combat_.skills.size();
                for (size_t i = 0; same && i < pendingSkills_.size(); ++i)
                {
                    const auto &a = pendingSkills_[i];
                    const auto &old = combat_.skills[i];
                    same = a.name == old.name && a.type == old.type && a.icon == old.icon;
                }
                if (!same)
                    combat_.skillCooldowns.clear();
                combat_.skills.swap(pendingSkills_);
                pendingSkills_.clear();
                previousSpellLines_ = std::move(combat_.spellLines);
                previousSpellCooldowns_ = std::move(combat_.spellCooldowns);
                combat_.spellLines.clear();
                combat_.spellCooldowns.clear();
                combat_.skillsReady = true;
                receivingSkills_ = false;
                ++combat_.skillsRevision;
            }
        }
        else
        {
            if (b[2] != 2)
                return false;
            if (!b[1] || b.size() - p < 4)
                return bad();
            SpellLine line;
            line.index = b[3];
            p += 3;
            if (!Pascal(b, p, line.name))
                return bad();
            for (size_t i = 1; i < b[1]; ++i)
            {
                if (b.size() - p < 4)
                    return bad();
                ListSpell s;
                s.index = uint8_t(i - 1);
                s.level = b[p];
                s.icon = Word(b, p + 1);
                p += 3;
                if (!Pascal(b, p, s.name))
                    return bad();
                line.spells.push_back(std::move(s));
            }
            if (p != b.size())
                return bad();
            const auto old = previousSpellLines_.find(line.index);
            if (old != previousSpellLines_.end() && old->second.name == line.name && old->second.spells.size() == line.spells.size())
            {
                bool same = true;
                for (size_t i = 0; same && i < line.spells.size(); ++i)
                {
                    const auto &a = line.spells[i];
                    const auto &z = old->second.spells[i];
                    same = a.name == z.name && a.level == z.level && a.icon == z.icon;
                }
                if (same)
                    for (const auto &cooldown : previousSpellCooldowns_)
                        if ((cooldown.first >> 8) == line.index && cooldown.second > now)
                            combat_.spellCooldowns[cooldown.first] = cooldown.second;
            }
            previousSpellLines_.erase(line.index);
            for (auto it = previousSpellCooldowns_.begin(); it != previousSpellCooldowns_.end();)
                if ((it->first >> 8) == line.index)
                    it = previousSpellCooldowns_.erase(it);
                else
                    ++it;
            combat_.spellLines[line.index] = std::move(line);
            ++combat_.skillsRevision;
        }
        return true;
    }
    case 0xad:
        if (b.size() < 8)
            return bad();
        combat_.statusKnown = true;
        combat_.health = std::min<uint8_t>(b[0], 100);
        combat_.mana = std::min<uint8_t>(b[1], 100);
        combat_.dead = Word(b, 2) != 0;
        combat_.sitting = b[4] != 0;
        combat_.endurance = std::min<uint8_t>(b[5], 100);
        combat_.concentration = std::min<uint8_t>(b[6], 100);
        if (combat_.dead)
        {
            combat_.attacking = false;
            combat_.castUntil = 0;
        }
        return true;
    case 0x74:
        if (b.size() < 4)
            return bad();
        combat_.attacking = b[0] != 0;
        return true;
    case 0xdf:
        if (b.size() < 12)
            return bad();
        combat_.groundX = Dword(b, 0);
        combat_.groundY = Dword(b, 4);
        combat_.groundZ = Dword(b, 8);
        combat_.groundKnown = true;
        return true;
    case 0xf6:
        if (b.size() < 4)
            return bad();
        combat_.target = Word(b, 0);
        return true;
    case 0xd6: {
        if (b.size() < 4)
            return bad();
        if (b[3] != 1 && b[3] != 2)
            return false;
        size_t stride = b[3] == 1 ? 4 : 2;
        if (b.size() != 4 + b[2] * stride)
            return bad();
        for (size_t p = 4; p < b.size(); p += stride)
        {
            const auto key = Word(b, p);
            const auto seconds = b[3] == 1 ? Word(b, p + 2) : Word(b, 0);
            auto &map = b[3] == 1 ? combat_.skillCooldowns : combat_.spellCooldowns;
            if (seconds)
                map[key] = now + seconds;
            else
                map.erase(key);
        }
        return true;
    }
    case 0x7f: {
        if (b.size() < 4)
            return bad();
        const bool incremental = settings_.version >= 73;
        std::vector<Effect> effects = incremental ? combat_.effects : std::vector<Effect>{};
        size_t p = 4;
        for (size_t i = 0; i < b[0]; ++i)
        {
            const size_t prefix = incremental ? 9 : 8;
            if (b.size() - p < prefix + 1)
                return bad();
            Effect f;
            f.slot = b[p];
            f.index = b[p + (incremental ? 1 : 0)];
            f.icon = Word(b, p + (incremental ? 3 : 2));
            f.seconds = Word(b, p + (incremental ? 5 : 4));
            f.id = Word(b, p + (incremental ? 7 : 6));
            p += prefix;
            if (!Pascal(b, p, f.name))
                return bad();
            if (incremental)
                effects.erase(std::remove_if(effects.begin(), effects.end(), [&](const Effect &old) { return old.slot == f.slot; }), effects.end());
            if (f.icon != 0)
                effects.push_back(std::move(f));
        }
        if (p != b.size())
            return bad();
        combat_.effects.swap(effects);
        return true;
    }
    case 0xbc:
        if (b.size() < 12)
            return bad();
        e.type = CombatEventType::Melee;
        e.source = Word(b, 0);
        e.target = Word(b, 2);
        e.weapon = Word(b, 4);
        e.shield = Word(b, 6);
        e.style = uint16_t(b[8] + ((b[10] & 128) ? 256 : 0));
        e.stance = b[9];
        e.result = b[10] & 127;
        e.health = b[11];
        if (e.health <= 100)
        {
            const auto previous = combat_.entityHealth.find(e.target);
            const bool wasDead = previous != combat_.entityHealth.end() && previous->second == 0;
            combat_.entityHealth[e.target] = e.health;
            if (e.target == objectId_)
                combat_.health = e.health;
            const auto target = players_.find(e.target);
            // DOL sends 0xae for player deaths. NPC melee deaths arrive as
            // authoritative zero target health in the combat animation packet.
            if (e.health == 0 && !wasDead && target != players_.end() && target->second.npc)
            {
                QueueCombatEvent(e);
                e.type = CombatEventType::Death;
                if (combat_.target == e.target)
                    combat_.attacking = false;
            }
        }
        break;
    case 0x72:
        if (b.size() < 8)
            return bad();
        e.type = CombatEventType::Cast;
        e.source = Word(b, 0);
        e.effect = Word(b, 2);
        e.seconds = Word(b, 4) / 10.0;
        if (e.source == objectId_)
        {
            combat_.castEffect = e.effect;
            combat_.castUntil = now + e.seconds;
        }
        break;
    case 0x73:
        if (b.size() < 4)
            return bad();
        e.type = CombatEventType::Interrupt;
        e.source = Word(b, 0);
        if (e.source == objectId_)
            combat_.castUntil = 0;
        break;
    case 0x1b:
        // PacketLib174 omits PacketLib168's two trailing reserved bytes.
        if (b.size() < 10)
            return bad();
        e.type = CombatEventType::SpellEffect;
        e.source = Word(b, 0);
        e.effect = Word(b, 2);
        e.target = Word(b, 4);
        e.seconds = Word(b, 6) / 10.0;
        e.result = b[9];
        e.noSound = b[8] != 0;
        if (e.source == objectId_)
            combat_.castUntil = 0;
        break;
    case 0xae:
        if (b.size() < 8)
            return bad();
        e.type = CombatEventType::Death;
        e.source = Word(b, 2);
        e.target = Word(b, 0);
        combat_.entityHealth[e.target] = 0;
        if (e.target == combat_.target)
            combat_.attacking = false;
        if (e.target == objectId_)
        {
            combat_.dead = true;
            combat_.health = 0;
            combat_.attacking = false;
            combat_.castUntil = 0;
        }
        break;
    case 0x89:
        if (b.size() < 4)
            return bad();
        e.type = CombatEventType::Revive;
        e.target = Word(b, 0);
        combat_.entityHealth.erase(e.target);
        if (e.target == objectId_)
            combat_.dead = false;
        break;
    case 0xd0:
        if (b.size() < 8)
            return bad();
        e.type = CombatEventType::LineOfSight;
        e.source = Word(b, 0);
        e.target = Word(b, 2);
        break;
    case 0xc9:
        if (b.size() < 12)
            return bad();
        e.type = CombatEventType::Sound;
        e.effect = Word(b, 0);
        e.zone = Word(b, 2);
        e.x = Word(b, 4);
        e.y = Word(b, 6);
        e.z = Word(b, 8);
        e.radius = Word(b, 10);
        break;
    case 0xaf:
        if (b.size() < 9)
            return bad();
        e.type = CombatEventType::Message;
        e.chatType = b[4];
        {
            auto end = std::find(b.begin() + 8, b.end(), uint8_t(0));
            e.text.assign(b.begin() + 8, end);
        }
        break;
    default:
        return false;
    }
    QueueCombatEvent(std::move(e));
    return true;
}
} // namespace camelot
