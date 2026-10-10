#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Engine/DataAsset.h"
#include "CamelotCrowdComponent.generated.h"

class USkeletalMeshComponent;
class UInstancedStaticMeshComponent;
class UAnimSequenceTransformProviderData;
class UAnimSequenceTransformProviderDataInstance;
class UAnimSequence;

/** Saved per-appearance animation providers; referencing this asset includes its
 * meshes, clips and generated provider data in a cook. */
UCLASS(BlueprintType)
class CAMELOTCORE_API UCamelotCrowdAnimationSet : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
    TArray<TObjectPtr<UAnimSequenceTransformProviderData>> Providers;
};

/** One appearance batch. The consumer supplies equipped templates and authoritative events.
 * Tracks are bounded by visual animation diversity, never by the number of characters.
 * Keep interactive/local characters on the ordinary skeletal presenter. */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class CAMELOTCORE_API UCamelotCrowdComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    static constexpr int32 MaxTrackCount = 128;
    /// Initializes default settings and owned components.
    UCamelotCrowdComponent();
    /// Providers must match the template's body, then its skeletal children, in that order.
    /// Prepare and save them in the editor for packaged consumers.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool Initialize(USkeletalMeshComponent *EquippedTemplate, const TArray<UAnimSequenceTransformProviderData *> &Providers, int32 TrackCount);
    /// Editor preparation only; returns empty in packaged builds. Does not change source meshes.
    static TArray<UAnimSequenceTransformProviderData *> PrepareProviders(USkeletalMeshComponent *EquippedTemplate, const TArray<UAnimSequence *> &Sequences, UObject *Outer);
    /// Prepares compatible override layers on an editor animation provider.
    static bool PrepareBlendLayers(UAnimSequenceTransformProviderData *Provider);
    /// Explicit editor opt-in for just this appearance's meshes/materials. Saving
    /// makes the unified bone maps and material permutations available to cooks.
    UFUNCTION(BlueprintCallable, Category = "Crowd|Editor", meta = (DevelopmentOnly))
    static bool PrepareTemplateForInstancing(USkeletalMeshComponent *EquippedTemplate, bool bSavePackages = false);
    // Creates a new saved asset; existing packages are never overwritten.
    /// Authors a GPU animation provider for the equipped template and supplied clips.
    UFUNCTION(BlueprintCallable, Category = "Crowd|Editor", meta = (DevelopmentOnly))
    static UCamelotCrowdAnimationSet *PrepareAnimationSet(USkeletalMeshComponent *EquippedTemplate, const TArray<UAnimSequence *> &Sequences, const FString &PackagePath);
    /// Adds a stable object ID to this appearance batch using a valid shared track.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool AddCharacter(int32 ObjectId, const FTransform &WorldTransform, int32 Track);
    /// Updates transform and track explicitly, replacing any managed action.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool UpdateCharacter(int32 ObjectId, const FTransform &WorldTransform, int32 Track);
    /// Preserve the current action track while applying authoritative/interpolated movement.
    /// Teleports reset transform history instead of producing a battlefield-wide velocity streak.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool MoveCharacter(int32 ObjectId, const FTransform &WorldTransform, bool bTeleport = false);
    /// Removes the object ID and its instances without invalidating other character identities.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool RemoveCharacter(int32 ObjectId);
    /// Call once for a shared cohort, not once per member. A free/cohort track must be chosen
    /// by the consumer so unrelated one-shot events never restart another character's pose.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool PlayTrack(int32 Track, int32 Sequence, bool bLoop, float Position = 0, float Rate = 1);
    /// Opt in after Initialize, before actions. The first LoopTracks remain consumer-owned
    /// locomotion cohorts; the rest form a bounded action pool. PhaseToleranceSeconds=0
    /// preserves exact event phase; nonzero tolerances are for distant representations only.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool ConfigureActionPool(int32 LoopTracks, float PhaseToleranceSeconds = 0);
    /// One confirmed visual action. Sharing requires compatible sequence/rate/phase/hold.
    /// False leaves the character untouched: promote/fall back rather than lose the event.
    /// Death poses may hold; attacks/casts return to the latest supplied locomotion track.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool PlayCharacterAction(int32 ObjectId, int32 Sequence, int32 ReturnLoopTrack, float Position = 0, float Rate = 1, bool bHoldAtEnd = false, bool bLoop = false, float BlendSeconds = 0);
    /// Returns the active clip and phase for representation transitions.
    bool GetCharacterPlayback(int32 ObjectId, UAnimSequence *&Clip, float &Position, bool &bLoop, float &Rate) const;
    /// Returns the target/source clips, positions and current transition weight.
    bool GetCharacterBlend(int32 ObjectId, UAnimSequence *&Target, float &TargetPosition, UAnimSequence *&Source, float &SourcePosition, float &TargetWeight) const;
    /// Returns all weighted contributors needed for continuous skeletal promotion.
    bool GetCharacterPose(int32 ObjectId, TArray<UAnimSequence *> &PoseClips, TArray<float> &Positions, TArray<float> &Weights) const;
    /// Releases component-owned presentation resources before destruction.
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
    /// Changes an action return cohort; interrupting held poses requires explicit opt-in.
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    bool SetCharacterLocomotion(int32 ObjectId, int32 LoopTrack, bool bInterruptAction = false, float BlendSeconds = 0);
    /// Returns the character track index, or an invalid index when absent.
    UFUNCTION(BlueprintPure, Category = "Crowd")
    int32 GetCharacterTrack(int32 ObjectId) const;
    /// Counts occupied animation poses rather than the number of characters.
    int32 GetActivePoseCount() const;
    /// Returns cumulative action allocations that required presentation fallback.
    int32 GetActionPoolMissCount() const
    {
        return ActionPoolMisses;
    }
    /// Counts character IDs currently retained by this presentation interface.
    UFUNCTION(BlueprintPure, Category = "Crowd")
    int32 GetCharacterCount() const
    {
        return Characters.Num();
    }
    /// Returns the configured shared animation-track capacity.
    UFUNCTION(BlueprintPure, Category = "Crowd")
    int32 GetTrackCount() const
    {
        return Poses.Num();
    }
    /// Counts visible body, armor and hair instances in the batch.
    int32 GetSkinnedInstanceCount() const;
    /// Counts instanced rigid weapon attachments.
    int32 GetWeaponInstanceCount() const;
    /// Counts skinned batches with recent render visibility.
    int32 GetRecentlyRenderedBatchCount() const;
    /// Updates this component using the current frame delta and tick context.
    virtual void TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction *Tick) override;
    /// Releases runtime bindings and owned resources when gameplay ends.
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    /// Per-character presentation bookkeeping within its owning crowd interface.
    struct FCharacter
    {
        FTransform Transform;
        int32 Track;
        TArray<FPrimitiveInstanceId> Parts;
        int32 ReturnTrack = 0;
        bool bAction = false;
        bool bMoved = false;
        bool bTeleportWeapons = true;
        float ReturnBlendSeconds = 0, ActionPosition = 0;
        double ActionStarted = 0;
    };
    /// Instanced weapon attachment sampled from the shared animation pose.
    struct FWeapon
    {
        FName Socket;
        FTransform Relative;
    };
    /// Weighted clip sample used when evaluating a shared pose.
    struct FSample
    {
        int32 Sequence = 0;
        float Position = 0, Rate = 1, Weight = 1;
        bool bLoop = true;
    };
    /// Shared animation-track playback and transition state.
    struct FPlayback
    {
        int32 Sequence = 0;
        float Position = 0, Rate = 1;
        bool bLoop = true;
        int32 SourceSequence = INDEX_NONE;
        float SourcePosition = 0, SourceRate = 1, BlendSeconds = 0;
        bool bSourceLoop = false;
        double Started = 0;
        TArray<FSample, TInlineAllocator<4>> Samples;
    };
    TMap<int32, FCharacter> Characters;
    TArray<int32> Order;
    TArray<FWeapon> Attachments;
    TArray<FPlayback> Playback;
    TArray<int32> TrackUsers;
    TArray<bool> HeldActions;
    int32 LoopTrackCount = 0, ActionPoolMisses = 0;
    float PhaseTolerance = 0;
    bool bLayeredBlending = false;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedSkinnedMeshComponent>> Parts;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAnimSequenceTransformProviderDataInstance>> Animations;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Weapons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> Poses;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAnimSequence>> Clips;
    /// Releases batch instances, providers and character/track bookkeeping.
    void ResetBatch();
    /// Associates character instances and attachment bookkeeping with a shared track.
    void AssignTrack(FCharacter &Character, int32 Track);
    /// Starts a shared clip track with optional transition source and blending.
    bool StartTrack(int32 Track, int32 Sequence, bool bLoop, float Position, float Rate, float BlendSeconds = 0, int32 SourceTrack = INDEX_NONE);
    /// Allocates a compatible track for the requested character pose transition.
    bool TransitionCharacter(FCharacter &Character, int32 Sequence, bool Loop, float Position, float Rate, bool Hold, float BlendSeconds);
    /// Returns the normalized transition weight for a shared track.
    float BlendWeight(int32 Track) const;
    /// Resolves the current two-clip transition samples for a shared track.
    void SampleTrack(int32 Track, UAnimSequence *&Target, float &TargetPosition, UAnimSequence *&Source, float &SourcePosition, float &TargetWeight) const;
    /// Collects active weighted clips for attachment-pose evaluation.
    void SampleLayers(int32 Track, TArray<FSample, TInlineAllocator<4>> &Samples) const;
    /// Advances clip-layer weights and collapses completed transitions.
    void UpdateLayerWeights(int32 Track);
    /// Starts a track while retaining source contributors needed for continuous blending.
    bool StartLayeredTrack(int32 Track, FPlayback &State, int32 SourceTrack);
    /// Finds the destination clip layer in a shared transition.
    int32 TargetLayer(int32 Track) const;
};
