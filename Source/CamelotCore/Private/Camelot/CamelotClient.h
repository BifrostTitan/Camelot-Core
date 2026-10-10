// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <array>
#include "CamelotCombat.h"
#include "CamelotInventory.h"
#include "CamelotTrainer.h"

namespace camelot
{
using Bytes = std::vector<uint8_t>;
/// Protocol session phase, including deferred world readiness and terminal failure.
enum class State
{
    Disconnected,
    Crypt,
    Login,
    Characters,
    Selecting,
    Opening,
    World,
    Loading,
    Initializing,
    Playing,
    Failed
};
/// Server-region bounds used to resolve an absolute position to a zone.
struct Zone
{
    uint16_t id = 0, region = 0;
    uint32_t x = 0, y = 0, width = 65536, height = 65536;
    bool outdoor = true;
};
/// Transport-independent session credentials, protocol version and destination configuration.
struct Settings
{
    std::string username, password, character;
    uint8_t realm = 1;    // Albion=1, Midgard=2, Hibernia=3
    uint8_t version = 68; // 68 legacy, 72..74 Catacombs appearance layouts
    double timeoutSeconds = 20;
    bool interactive = false;
    uint16_t zone = 0;
    uint32_t zoneOriginX = 0, zoneOriginY = 0;
    std::vector<Zone> zones;
};
/// Decoded classic weather front and its receipt time.
struct Weather
{
    bool known = false;
    uint16_t region = 0, speed = 0, fogDiffusion = 0, intensity = 0;
    uint32_t position = 0, width = 0;
    double receivedAt = 0;
};
/// Wire-level slot appearance identifiers for a replicated character.
struct Equipment
{
    uint8_t slot = 0;
    uint16_t model = 0, color = 0, effect = 0;
    uint8_t extension = 0;
};
/// Wire-level model, customization and equipment state with its revision.
struct Appearance
{
    uint16_t model = 0, modelCode = 0;
    uint8_t race = 0, gender = 0, realm = 0, level = 0, classId = 0, size = 0;
    uint8_t rightWeapon = 255, leftWeapon = 255, hood = 0;
    uint8_t eyeSize = 0, lipSize = 0, eyeColor = 0, hairColor = 0, faceType = 0, hairStyle = 0, moodType = 0, customisationStep = 0;
    std::vector<Equipment> equipment;
    bool equipmentKnown = false;
    uint32_t revision = 0;
};
/// Server character-selection snapshot with roster slot and appearance.
struct CharacterOverview
{
    std::string name, location, className, raceName;
    Appearance appearance;
    uint32_t databaseId = 0;
    uint8_t slot = 0, region = 0, expansion = 0;
    std::array<uint8_t, 8> stats{};
};
/// Replicated player or NPC identity, movement endpoints and appearance.
struct Player
{
    uint16_t objectId = 0, sessionId = 0, zone = 0, heading = 0;
    uint32_t x = 0, y = 0;
    uint16_t z = 0;
    std::string name, guildName;
    bool local = false;
    bool npc = false;
    bool flying = false;
    bool attackModeKnown = false, attacking = false;
    uint16_t speed = 0;
    uint32_t targetX = 0, targetY = 0;
    uint16_t targetZ = 0;
    double positionTime = 0;
    Appearance appearance;
};
/// Classic character-creation fields; the server validates eligibility and stats.
struct CharacterDraft
{
    std::string name;
    uint8_t slot = 0, race = 1, gender = 0, realm = 1, classId = 14, region = 1;
    uint16_t model = 32;
    bool isles = false;
    uint8_t eyeSize = 0x55, lipSize = 0x55, eyeColor = 0x10, hairColor = 0xb0, faceType = 0, hairStyle = 0, moodType = 0x55;
    std::array<uint8_t, 8> stats{}; // STR, DEX, CON, QUI, INT, PIE, EMP, CHR (wire order)
};
/// Single-threaded, transport-independent protocol engine. Feed/Tick on the owner thread.
/// TCP-only mode uses DOL's existing UDP-to-TCP fallback for replication.
class Client
{
  public:
    /// Initializes protocol state from the supplied transport-independent settings.
    explicit Client(Settings settings);
    /// Begins the protocol handshake using the supplied monotonic time in seconds.
    void Start(double now);
    /// Consumes possibly fragmented TCP bytes on the owner thread and dispatches complete packets.
    void Feed(const uint8_t *data, size_t count, double now);
    /// Polls handshake, replication and request deadlines using monotonic time in seconds.
    void Tick(double now);
    /// Requests world entry for the named server roster character.
    bool SelectCharacter(const std::string &name, double now);
    /// Requests an allowed realm and a refreshed character roster.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    bool RequestRealm(uint8_t realm, double now);
    /// Requests authoritative availability checks before creating a character.
    bool CheckCharacterName(const std::string &name);
    /// Sends a validated creation draft; the server determines whether creation succeeds.
    bool CreateCharacter(const CharacterDraft &draft, double now);
    /// Requests deletion of the named roster entry and waits for a roster refresh.
    bool DeleteCharacter(const std::string &name, double now);
    /// Reports whether the complete server roster is available for selection.
    bool CharactersReady() const
    {
        return charactersReady_;
    }
    /// Returns the version of the current character-selection snapshot.
    uint32_t RosterRevision() const
    {
        return rosterRevision_;
    }
    /// Returns -1 while unknown, 0 for any realm, or the permitted realm ID 1..3.
    int AllowedRealm() const
    {
        return allowedRealm_;
    } // -1 unknown, 0 any, 1..3 locked
    /// Returns the character name associated with the latest name-availability check.
    const std::string &CheckedName() const
    {
        return checkedName_;
    }
    /// Returns -1 pending, 0 available, 1 invalid or 2 already taken.
    int NameResult() const
    {
        return nameResult_;
    } // -1 pending, 0 available, 1 invalid, 2 taken
    /// Closes the connection and clears session-owned gameplay state.
    void Disconnect();
    /// Records a terminal session error and clears pending gameplay operations.
    void Fail(const std::string &message);
    /// Queues position, heading, speed and visibility in classic server units.
    bool Move(uint32_t x, uint32_t y, uint16_t z, uint16_t heading, uint16_t speed = 0, bool targetVisible = false, bool groundVisible = false, bool flying = false);
    /// Queues printable command text through the existing classic command packet.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    bool SendCommand(const std::string &command);
    /// Reports developer-mode authorization received from the server.
    bool DebugEnabled() const
    {
        return debugEnabled_;
    }
    /// Returns the current decoded combat snapshot; valid while this client exists.
    const CombatState &Combat() const
    {
        return combat_;
    }
    /// Returns the current decoded inventory snapshot; valid while this client exists.
    const InventoryState &Inventory() const
    {
        return inventory_;
    }
    /// Returns the current decoded trainer snapshot; valid while this client exists.
    const TrainerState &Trainer() const
    {
        return trainer_;
    }
    /// Returns the character level reported by the player-sheet update.
    uint8_t TrainingLevel() const
    {
        return trainingLevel_;
    }
    /// Begins a nearby NPC interaction and waits for server trainer choices.
    bool Interact(uint16_t object, double now);
    /// Queues a revision-checked training allocation for the active nearby trainer.
    bool Train(bool realm, uint8_t index, uint8_t desired, uint64_t revision, double now);
    /// Closes training and stops unsent queued ranks; accepted purchases cannot be undone.
    void CloseTrainer();
    /// Returns the affordable training limit from reported costs, points and character level.
    int MaxTrainingRank(const TrainerLine &line, bool realm) const;
    /// Queues a classic inventory move only if the supplied snapshot revision is current.
    bool MoveInventoryItem(uint16_t from, uint16_t to, uint16_t count, uint64_t revision);
    /// Queues a slot action with current action context and inventory revision.
    bool UseInventoryItem(uint16_t slot, uint8_t use, const ActionContext &context, uint64_t revision);
    /// Starts one serialized item-detail request bound to its slot and revision.
    bool InspectInventoryItem(uint16_t slot, uint64_t revision, double now);
    /// Drains decoded combat events for presentation and UI consumers.
    std::vector<CombatEvent> TakeCombatEvents();
    /// Selects a known target through the session interface.
    bool SelectTarget(uint16_t object, bool visible);
    /// Queues the requested attack mode through the active session.
    bool SetAttack(bool enabled);
    /// Forwards the character release request through the session.
    bool Release();
    /// Queues a hybrid learned action using its absolute server catalog index.
    bool UseSkill(uint16_t absoluteIndex, const ActionContext &context, double now);
    /// Queues a spell using its server line and within-line spell index.
    bool UseSpell(uint8_t line, uint8_t spellIndex, const ActionContext &context, double now);
    /// Sets the ground target using server coordinates and its visibility state.
    bool SetGroundTarget(uint32_t x, uint32_t y, uint16_t z, bool visible);
    /// Replies to a server visibility check for the supplied source and target IDs.
    bool ReplyLineOfSight(uint16_t source, uint16_t target, bool visible);
    /// Queues cancellation using the effect internal ID rather than its icon or list position.
    bool CancelEffect(uint16_t internalId);
    /// Drains encoded packets for the transport to send; the client does not own a socket.
    std::vector<Bytes> TakeOutgoing();
    /// Returns the current state snapshot without simulating authoritative results.
    State GetState() const
    {
        return state_;
    }
    /// Returns the latest connection or protocol error description.
    const std::string &Error() const
    {
        return error_;
    }
    /// Returns replicated entities keyed by server object ID, including remote players and mobs.
    const std::map<uint16_t, Player> &Players() const
    {
        return players_;
    }
    /// Returns the latest server character roster.
    const std::vector<CharacterOverview> &Characters() const
    {
        return characters_;
    }
    /// Returns the server object ID assigned to the local character.
    uint16_t LocalObjectId() const
    {
        return objectId_;
    }
    /// Returns the current server region ID.
    uint16_t Region() const
    {
        return region_;
    }
    /// Returns the latest decoded server weather front.
    const Weather &CurrentWeather() const
    {
        return weather_;
    }
    /// Returns the currently resolved server zone ID.
    uint16_t ZoneId() const
    {
        return settings_.zone;
    }
    /// Returns the active zone absolute X origin in server units.
    uint32_t ZoneOriginX() const
    {
        return settings_.zoneOriginX;
    }
    /// Returns the active zone absolute Y origin in server units.
    uint32_t ZoneOriginY() const
    {
        return settings_.zoneOriginY;
    }
    /// Returns the destination version used to invalidate stale world entry.
    uint32_t DestinationRevision() const
    {
        return destinationRevision_;
    }
    /// Acknowledges prepared world readiness before entering active gameplay.
    bool ConfirmWorldReady(double now);
    /// Computes the classic packet checksum over the supplied byte range.
    static uint16_t Checksum(const uint8_t *bytes, size_t count);

  private:
    /// Encodes the supplied opcode and payload into the outgoing packet queue.
    void Send(uint8_t opcode, const Bytes &data);
    /// Dispatches one validated complete packet to its protocol handler.
    void Handle(uint8_t opcode, const Bytes &data, double now);
    /// Consumes supported combat packets and queues confirmed presentation events.
    bool HandleCombat(uint8_t opcode, const Bytes &data, double now);
    /// Adds a decoded event to the bounded consumer event queue.
    void QueueCombatEvent(CombatEvent event);
    CombatState combat_;
    InventoryState inventory_;
    TrainerState trainer_;
    uint8_t trainingLevel_ = 0;
    uint16_t interactionTarget_ = 0;
    double interactionExpires_ = 0, trainingExpires_ = 0;
    bool trainingRealm_ = false;
    uint8_t trainingIndex_ = 0, trainingFrom_ = 0, trainingDesired_ = 0;
    std::string trainingName_;
    /// Validates trainer or player-sheet packets before committing their state.
    bool HandleTrainer(uint8_t opcode, const Bytes &data, double now);
    /// Advances trainer deadlines and cancels invalid or unconfirmed allocations.
    void TickTrainer(double now);
    /// Checks target identity, session state and the classic trainer interaction range.
    bool NearTrainer(uint16_t object) const;
    /// Sends exactly one pending rank purchase after the previous confirmation.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    bool SendTrainingRank(double now);
    /// Validates inventory packets and commits confirmed owned-item updates.
    bool HandleInventory(uint8_t opcode, const Bytes &data, double now);
    std::string pendingItemDetailName_;
    uint64_t pendingItemDetailRevision_ = 0;
    double pendingItemDetailExpires_ = 0;
    /// Clears inventory and pending detail state while advancing its revision.
    void ResetInventory();
    std::vector<Skill> pendingSkills_;
    std::vector<CombatEvent> combatEvents_;
    bool receivingSkills_ = false;
    std::map<uint8_t, SpellLine> previousSpellLines_;
    std::map<uint16_t, double> previousSpellCooldowns_;
    /// Changes protocol phase and establishes its timeout deadline.
    void Transition(State state, double now);
    Settings settings_;
    State state_ = State::Disconnected;
    std::string error_;
    Bytes incoming_;
    std::vector<Bytes> outgoing_;
    std::map<uint16_t, Player> players_;
    std::vector<CharacterOverview> characters_;
    Appearance selectedAppearance_;
    bool debugEnabled_ = false;
    bool charactersReady_ = false;
    uint32_t rosterRevision_ = 0;
    int allowedRealm_ = -1, nameResult_ = -1;
    std::string checkedName_, pendingCreation_;
    std::map<uint16_t, Appearance> pendingEquipment_;
    /// Applies equipment received before the corresponding character identity.
    void ApplyPendingEquipment(Player &player);
    /// Resolves absolute coordinates into a supported zone and advances destination state.
    bool ResolveDestination(uint32_t x, uint32_t y, double now, bool initialize);
    Weather weather_;
    uint16_t region_ = 0;
    uint32_t destinationRevision_ = 0;
    bool initializeAfterLoad_ = false;
    uint16_t sequence_ = 0, sessionId_ = 0, objectId_ = 0;
    double deadline_ = 0, nextPing_ = 0, lastReceive_ = 0;
};
/// Returns a stable display label for a protocol connection state.
const char *StateName(State state);
} // namespace camelot
