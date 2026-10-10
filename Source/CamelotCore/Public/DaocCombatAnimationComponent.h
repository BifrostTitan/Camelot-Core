#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DaocCombatTypes.h"
#include "DaocCombatAnimationCatalog.h"
#include "DaocCombatAnimationComponent.generated.h"
class USkeletalMeshComponent;
class UAnimSequence;
class USkeleton;
/// Presents confirmed combat actions and transitions for one visible character.
UCLASS(ClassGroup = (DAoC), meta = (BlueprintSpawnableComponent))
class CAMELOTCORE_API UDaocCombatAnimationComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocCombatAnimationComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Animation")
    TObjectPtr<UDaocCombatAnimationCatalog> Catalog;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Animation")
    FString ActiveAction;
    /// Binds the visible body and its model/object identity to combat presentation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Animation")
    void SetCharacter(USkeletalMeshComponent *Mesh, int32 Model, int32 ObjectId);
    /// Applies a confirmed session event to this character presentation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Animation")
    void HandleEvent(const FDaocCombatEvent &Event);
    /// Clears pending reactions, action timing and confirmed combat presentation state.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Animation")
    void ResetCombat();
    /// Updates movement loops and combat stance without inventing combat outcomes.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Animation")
    void UpdateLocomotion(UAnimSequence *Sequence, bool bLoop, bool bAttackMode, int32 WeaponModel, bool bInferCombatStance = false);
    /// Resolves the ready stance for the current weapon and combat presentation.
    UAnimSequence *GetCombatIdleClip();
    /// Returns the locomotion clip selected for the transition out of an action.
    UAnimSequence *GetReturnLocomotionClip() const
    {
        return ReturnLocomotionClip;
    }
    /// Updates this component using the current frame delta and tick context.
    virtual void TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction *Tick) override;
    /// Resolves a style or weapon-family action into its source animation-table index.
    static int32 AttackAction(int32 Style, const UDaocCombatAnimationCatalog *Catalog, EDaocWeaponAnimation Weapon);
    /// The live GPU presenter consumes this same confirmed state machine.
    bool GetPlaybackState(UAnimSequence *&Clip, float &Position, bool &bLoop, float &Rate, bool &bAction, bool &bHold, uint64 &Revision) const;
    /// Restores the current confirmed action phase during representation changes.
    void RestorePlaybackPosition(float Position);
    /// The confirmed state/clock keeps advancing while the GPU owns the pose.
    /// Returning to full presentation restores that clock once.
    void SetSharedPresentation(bool bShared);
    /// Returns the active transition duration in seconds.
    float GetTransitionDuration() const
    {
        return PlaybackBlendSeconds;
    }
    /// Reports whether the visible native pose transition is unfinished.
    bool IsPresentationBlending() const;
    /// Restores a two-clip crowd transition into native skeletal presentation.
    void RestoreCrowdPose(UAnimSequence *Target, float Position, UAnimSequence *Source, float SourcePosition, float TargetWeight);
    /// Restores weighted crowd contributors while preserving their confirmed revision.
    void RestoreCrowdSamples(TConstArrayView<UAnimSequence *> Clips, TConstArrayView<float> Positions, TConstArrayView<float> Weights, bool bSameRevision);

  private:
    /// Resolves the named animation key through the bound combat animation catalog.
    UAnimSequence *Resolve(const FString &Key);
    /// Starts a mapped action with the requested looping, speed and restart policy.
    bool PlayAction(const FString &Key, bool bLoop = false, float Rate = 1, bool bRestart = true);
    /// Begins a native clip transition using the configured blend duration.
    void StartPlayback(UAnimSequence *Clip, bool bLoop, float Rate, float BlendSeconds);
    /// Returns the animation clock honoring world pause and time dilation.
    double AnimationTime() const;
    int32 LastWeaponModel = 0;
    double CombatStanceUntil = 0;
    uint64 LastReactionFrame = MAX_uint64;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> ReturnLocomotionClip;
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> Body;
    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UAnimSequence>> Loaded;
    TWeakObjectPtr<USkeleton> CheckedSkeleton;
    TWeakObjectPtr<UDaocCombatAnimationCatalog> CheckedCatalog;
    bool bSkeletonCompatible = false;
    bool bCatalogLookupAttempted = false;
    int32 ModelId = 0, EntityId = 0, LoopAction = 17;
    bool bDead = false, bCasting = false, bCastLoop = false;
    double Until = 0, CastUntil = 0;
    FString PendingLoop;
    double TransitionAt = 0;
    // Only the latest hit waits for an outgoing attack. Reactions never form a queue.
    FString PendingReaction;
    bool bAttackAction = false;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> PlaybackClip;
    double PlaybackStarted = 0;
    uint64 PlaybackRevision = 0;
    float PlaybackRate = 1;
    float PlaybackBlendSeconds = .12f;
    bool bPlaybackLoop = true;
    bool bVisualActionUnavailable = false;
    bool bSharedPresentation = false;
};
