// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace camelot
{
/// Wire discriminator for hybrid learned-skill entries.
enum class SkillType : uint8_t
{
    Specialization,
    Ability,
    Style,
    Spell,
    Song,
    AbilitySpell,
    RealmAbility
};
/// One hybrid catalog entry retaining its server index and icon metadata.
struct Skill
{
    uint16_t index = 0, special = 0, icon = 0;
    uint8_t level = 0, bonus = 0;
    SkillType type = SkillType::Specialization;
    std::string name;
};
/// One spell entry indexed within a classic spell line.
struct ListSpell
{
    uint8_t index = 0, level = 0;
    uint16_t icon = 0;
    std::string name;
};
/// Server spell-line identity and its indexed learned spells.
struct SpellLine
{
    uint8_t index = 0;
    std::string name;
    std::vector<ListSpell> spells;
};
/// Reported active effect and its cancellation identity.
struct Effect
{
    uint8_t index = 255;
    /// Display slot for 1.73+ incremental icon updates, distinct from cancelability.
    uint8_t slot = 255;
    uint16_t icon = 0, id = 0, seconds = 0;
    std::string name;
};
/// Native discriminator for a decoded combat or chat event.
enum class CombatEventType
{
    Melee,
    Cast,
    Interrupt,
    SpellEffect,
    Death,
    Revive,
    Message,
    LineOfSight,
    Sound
};
/// Decoded event queued for consumers after protocol processing.
struct CombatEvent
{
    CombatEventType type = CombatEventType::Message;
    uint16_t source = 0, target = 0, effect = 0, weapon = 0, shield = 0, style = 0;
    uint8_t result = 0, stance = 0, health = 0, chatType = 0;
    bool noSound = false;
    uint16_t zone = 0, x = 0, y = 0, z = 0, radius = 0;
    double seconds = 0;
    std::string text;
};
/// Native authoritative combat snapshot and learned catalogs.
struct CombatState
{
    bool statusKnown = false, dead = false, sitting = false, attacking = false, skillsReady = false;
    uint8_t health = 0, mana = 0, endurance = 0, concentration = 0;
    uint16_t target = 0, castEffect = 0;
    double castUntil = 0;
    uint32_t skillsRevision = 0;
    bool progressKnown = false;
    uint16_t levelPermill = 0, skillPoints = 0, realmSkillPoints = 0;
    uint32_t realmPoints = 0, bountyPoints = 0;
    bool groundKnown = false;
    uint32_t groundX = 0, groundY = 0, groundZ = 0;
    std::vector<Skill> skills;
    std::map<uint8_t, SpellLine> spellLines;
    std::vector<Effect> effects;
    // DOL identifies cooldowns by non-specialization index or line/index, never by icon.
    std::map<uint16_t, double> skillCooldowns;
    std::map<uint16_t, double> spellCooldowns;
    std::map<uint16_t, uint8_t> entityHealth;
};
/// Position, heading and visibility supplied with an action request.
struct ActionContext
{
    uint32_t x = 0, y = 0;
    uint16_t z = 0, heading = 0, speed = 0;
    bool backward = false, strafing = false, targetVisible = false, groundVisible = false;
};
} // namespace camelot
