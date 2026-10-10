#pragma once
#include "CoreMinimal.h"
#include "DaocCombatTypes.generated.h"

/// Distinguishes specialization, spell, style, song and ability catalog entries.
UENUM(BlueprintType)
enum class EDaocAbilityKind : uint8
{
    Specialization,
    Ability,
    Style,
    Spell,
    Song,
    AbilitySpell,
    RealmAbility,
    ListSpell
};
/// Learned action identity and metadata used for activation and hotbar assignment.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocCombatAbility
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    EDaocAbilityKind Kind = EDaocAbilityKind::Ability;
    /// Session indexes supplied by DOL, not database IDs.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ServerIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 SpellLine = -1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString LineName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Level = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Icon = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 SpecialField = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Bonus = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float CooldownRemaining = 0;
};
/// Reported active effect with duration and cancellation metadata.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocCombatEffect
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 InternalId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Icon = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString Name;
    /// Last server report; not a locally simulated expiration.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ReportedSeconds = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bCancelable = false;
};
/// Snapshot of confirmed combat resources, targeting, progress and action state.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocCombatState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bStatusKnown = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bDead = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bSitting = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bAttacking = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bSkillsReady = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 HealthPercent = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ManaPercent = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 EndurancePercent = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ConcentrationPercent = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 TargetObjectId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 CastingEffectId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float CastingRemaining = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int64 SkillsRevision = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bProgressKnown = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float ExperiencePercent = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 SkillPoints = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 RealmSkillPoints = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int64 RealmPoints = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int64 BountyPoints = 0;
};
/// Target identity, relationship and visible presentation data for gameplay UI.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocCombatTarget
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ObjectId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString GuildName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FVector Position = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bNPC = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bLocal = false;
    /// -1 until a server combat update reports this entity's health.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 HealthPercent = -1;
};
/// One retained server chat message with its category and sequence.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocChatMessage
{
    GENERATED_BODY()
    /// Local presentation identity; repeated identical messages remain separate rows.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int64 SequenceId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString Text;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ChatType = 0;
};
/// Discriminator identifying the confirmed event carried by a combat update.
UENUM(BlueprintType)
enum class EDaocCombatEventKind : uint8
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
/// Confirmed session event forwarded to animation, sound, chat and gameplay interfaces.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocCombatEvent
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    EDaocCombatEventKind Kind = EDaocCombatEventKind::Message;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 SourceObjectId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 TargetObjectId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 EffectId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 StyleId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 WeaponId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ShieldId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ResultCode = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Stance = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 HealthPercent = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ChatType = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bNoSound = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 SoundZone = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FVector SoundServerPosition = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 SoundRadius = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float Duration = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString Text;
};
/// Publishes a decoded server combat event for gameplay presentation.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDaocCombatEventReceived, const FDaocCombatEvent &, Event);
