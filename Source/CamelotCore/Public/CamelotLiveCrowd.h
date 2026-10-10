#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DaocCharacterAppearance.h"
#include "CamelotLiveCrowd.generated.h"

class UCamelotCrowdAnimationSet;
class UCamelotCrowdComponent;
class USkeletalMeshComponent;
class UDaocCombatAnimationComponent;
class UAnimSequence;

/// Prepared crowd animation providers associated with a character model.
USTRUCT(BlueprintType)
struct FCamelotLiveCrowdModel
{
    GENERATED_BODY()
    /// Only the providers for visible body/armor parts are loaded at runtime.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<FSoftObjectPath, TSoftObjectPtr<UCamelotCrowdAnimationSet>> MeshProviders;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UAnimSequence> Idle;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UAnimSequence> Walk;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UAnimSequence> Run;
};

/// Saved model-to-provider catalog consumed by live character presentation.
UCLASS(BlueprintType)
class CAMELOTCORE_API UCamelotLiveCrowdCatalog : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, FCamelotLiveCrowdModel> Models;
};

/// Live visual routing only. The existing network and combat state remain authoritative.
UCLASS()
class CAMELOTCORE_API UCamelotLiveCrowdPresenter : public UObject
{
    GENERATED_BODY()
  public:
    /// Binds crowd presentation to a world and its prepared animation-provider catalog.
    void Initialize(UWorld *World, UCamelotLiveCrowdCatalog *PreparedCatalog);
    /// Attempts prepared GPU presentation; false keeps the existing skeletal fallback.
    bool Present(int32 Id, USkeletalMeshComponent *Body, const FDaocCharacterAppearance &Appearance, UDaocCombatAnimationComponent *Animation, int32 Locomotion, int32 MaxBatches, float PhaseTolerance);
    /// Removes one character from live GPU presentation and restores supplied native animation ownership.
    void Remove(int32 Id, UDaocCombatAnimationComponent *Animation = nullptr);
    /// Releases all live appearance batches and character routing state.
    void Clear();
    /// Reports whether the supplied object ID currently has a GPU instance.
    bool IsInstanced(int32 Id) const
    {
        return Characters.Contains(Id);
    }
    /// Compares exact visual identity to determine whether the existing batch can be retained.
    bool HasAppearance(int32 Id, const FDaocCharacterAppearance &Appearance) const;
    /// Counts character IDs currently retained by this presentation interface.
    int32 GetCharacterCount() const
    {
        return Characters.Num();
    }
    /// Counts prepared appearance batches currently retained.
    int32 GetBatchCount() const
    {
        return Shared.Num();
    }
    /// Counts occupied animation poses rather than the number of characters.
    int32 GetActivePoseCount() const;
    /// Returns accumulated action-pool capacity failures across live batches.
    int32 GetPoolMissCount() const
    {
        return PoolMisses;
    }
    /// Returns the prepared catalog currently bound to this presenter.
    UCamelotLiveCrowdCatalog *GetCatalog() const
    {
        return Catalog;
    }
    /// Expires unused appearance batches after their retention interval.
    void CollectEmptyBatches();
    /// Builds the canonical exact body/equipment/customization identity used for batching.
    static FString AppearanceKey(const FDaocCharacterAppearance &Appearance);

  private:
    /// Prepared GPU appearance batch retained by the live presenter.
    struct FBatch
    {
        int32 Slot = INDEX_NONE;
        int32 LoopSequences[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
        UAnimSequence *CombatIdle = nullptr;
        TMap<UAnimSequence *, int32> Sequences;
        double EmptySince = 0;
    };
    /// Per-character presentation bookkeeping within its owning crowd interface.
    struct FCharacter
    {
        FString Key;
        FDaocCharacterAppearance Appearance;
        uint64 Revision = MAX_uint64;
        bool bAction = false;
        FVector Position = FVector::ZeroVector;
    };
    /// Retry state for an appearance that cannot currently use GPU presentation.
    struct FFailure
    {
        FString Key;
        uint64 Revision = 0;
        double RetryAfter = 0;
    };
    TWeakObjectPtr<UWorld> RenderWorld;
    UPROPERTY(Transient)
    TObjectPtr<UCamelotLiveCrowdCatalog> Catalog;
    UPROPERTY(Transient)
    TArray<TObjectPtr<AActor>> Actors;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UCamelotCrowdComponent>> Batches;
    TMap<FString, FBatch> Shared;
    TMap<int32, FCharacter> Characters;
    TMap<int32, FFailure> FailedActions;
    TSet<FString> Unsupported;
    int32 PoolMisses = 0;
    /// Creates a compatible appearance batch within the configured limit, or returns nullptr.
    FBatch *CreateBatch(const FString &Key, USkeletalMeshComponent *Body, int32 Model, int32 MaxBatches, float Tolerance, UAnimSequence *CombatIdle);
};
