#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DaocCombatAnimationCatalog.generated.h"
class UAnimSequence;
class USoundBase;
/// Weapon-family classification used to choose compatible combat clips.
UENUM(BlueprintType)
enum class EDaocWeaponAnimation : uint8
{
    OneHand,
    TwoHand,
    Pole,
    Staff,
    Bow,
    Crossbow,
    HandToHand,
    Scythe,
    Flexible
};
/// Named combat clip references for one imported character model.
USTRUCT(BlueprintType)
struct FDaocModelCombatClips
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<FString, TSoftObjectPtr<UAnimSequence>> Actions;
};
/// Spell event mapping to cast, effect and sound presentation identifiers.
USTRUCT(BlueprintType)
struct FDaocSpellPresentation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Start = 16;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Loop = 17;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Finish = 18;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 GatherSound = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 CastSound = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 HitSound = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 MissSound = 0;
};
/// Saved sound reference with its playback distance settings.
USTRUCT(BlueprintType)
struct FDaocCombatSound
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<USoundBase> Sound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float MinDistance = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float MaxDistance = 1000;
};
/// Shared action vocabulary; clips retain the skeleton required by each model.
UCLASS(BlueprintType)
class CAMELOTCORE_API UDaocCombatAnimationCatalog : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, FDaocModelCombatClips> Models;
    /// Generated only for identical bone hierarchy, bind pose and animation set.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, int32> ModelAliases;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, int32> Styles;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, FDaocSpellPresentation> Spells;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, FDaocCombatSound> Sounds;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, EDaocWeaponAnimation> Weapons;
    /// Associates a model/action key with a saved animation asset reference.
    UFUNCTION(BlueprintCallable)
    void SetClip(int32 Model, const FString &Action, const FString &Path);
    /// Associates a sound ID with a saved asset and minimum/maximum distances.
    UFUNCTION(BlueprintCallable)
    void SetSound(int32 Id, const FString &Path, float Min, float Max);
    /// Registers cast and effect presentation IDs for a server spell identifier.
    UFUNCTION(BlueprintCallable)
    void SetSpell(int32 Id, int32 Start, int32 Loop, int32 Finish, int32 Gather, int32 Cast, int32 Hit, int32 Miss);
};
