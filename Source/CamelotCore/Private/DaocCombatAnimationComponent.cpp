#include "DaocCombatAnimationComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "CamelotCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
UDaocCombatAnimationComponent::UDaocCombatAnimationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}
void UDaocCombatAnimationComponent::SetCharacter(USkeletalMeshComponent *Mesh, int32 Model, int32 ObjectId)
{
    if (Body != Mesh || ModelId != Model || EntityId != ObjectId)
    {
        ResetCombat();
        Loaded.Reset();
        CheckedSkeleton.Reset();
        bSkeletonCompatible = false;
        Body = Mesh;
        ModelId = Model;
        EntityId = ObjectId;
        PlaybackClip = nullptr;
        bSharedPresentation = false;
        if (auto *Instance = Body ? Cast<UCamelotCharacterAnimInstance>(Body->GetAnimInstance()) : nullptr)
            Instance->SetAnimationAsset(nullptr);
        ++PlaybackRevision;
    }
    if (!Catalog && !bCatalogLookupAttempted)
    {
        bCatalogLookupAttempted = true;
        Catalog = LoadObject<UDaocCombatAnimationCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_CombatAnimations.DA_CombatAnimations"), nullptr, LOAD_NoWarn);
    }
    if (Catalog != CheckedCatalog.Get())
    {
        Loaded.Reset();
        CheckedSkeleton.Reset();
        bSkeletonCompatible = false;
        CheckedCatalog = Catalog;
    }
}
void UDaocCombatAnimationComponent::ResetCombat()
{
    bDead = bCasting = bCastLoop = false;
    Until = CastUntil = 0;
    ActiveAction.Empty();
    PendingLoop.Empty();
    PendingReaction.Empty();
    bAttackAction = false;
    CombatStanceUntil = 0;
    LastReactionFrame = MAX_uint64;
    ReturnLocomotionClip = nullptr;
    bVisualActionUnavailable = false;
}
UAnimSequence *UDaocCombatAnimationComponent::Resolve(const FString &Key)
{
    if (!Body || !Body->GetSkeletalMeshAsset() || !Catalog)
        return nullptr;
    if (!Loaded.Contains(Key))
    {
        UAnimSequence *Clip = nullptr;
        const int32 SourceModel = Catalog->ModelAliases.Contains(ModelId) ? Catalog->ModelAliases.FindChecked(ModelId) : ModelId;
        if (const auto *M = Catalog->Models.Find(SourceModel))
            if (const auto *P = M->Actions.Find(Key))
                Clip = P->LoadSynchronous();
        Loaded.Add(Key, Clip);
    }
    auto *Clip = Loaded.FindRef(Key).Get();
    if (!Clip || !Clip->GetSkeleton())
        return nullptr;
    if (Clip->GetSkeleton() != CheckedSkeleton.Get())
    {
        CheckedSkeleton = Clip->GetSkeleton();
        bSkeletonCompatible = Clip->GetSkeleton() == Body->GetSkeletalMeshAsset()->GetSkeleton();
        if (!bSkeletonCompatible && Catalog->ModelAliases.Contains(ModelId))
        {
            const auto &Source = Clip->GetSkeleton()->GetReferenceSkeleton();
            const auto &Target = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
            bSkeletonCompatible = Source.GetNum() == Target.GetNum();
            for (int32 I = 0; bSkeletonCompatible && I < Source.GetNum(); ++I)
                bSkeletonCompatible = Source.GetBoneName(I) == Target.GetBoneName(I) && Source.GetParentIndex(I) == Target.GetParentIndex(I) && Source.GetRefBonePose()[I].Equals(Target.GetRefBonePose()[I], .0001);
        }
    }
    return bSkeletonCompatible ? Clip : nullptr;
}
bool UDaocCombatAnimationComponent::PlayAction(const FString &Key, bool Loop, float Rate, bool Restart)
{
    auto *Clip = Resolve(Key);
    if (!Clip)
    {
        bVisualActionUnavailable = true;
        return false;
    }
    bVisualActionUnavailable = false;
    auto *Instance = Body->GetSingleNodeInstance();
    if (Restart || PlaybackClip != Clip || (!bSharedPresentation && (!Instance || Instance->GetCurrentAsset() != Clip)))
    {
        const bool Reaction = Key == TEXT("C_3") || Key == TEXT("C_31") || Key == TEXT("C_33") || Key == TEXT("C_40");
        // Several mobs can hit the same body in one rendered frame. The same
        // reaction already starts at zero: preserve every event/revision without
        // repeatedly rebuilding the identical animation and interrupted snapshot.
        if (Reaction && LastReactionFrame == GFrameCounter && PlaybackClip == Clip)
            ++PlaybackRevision;
        else
            StartPlayback(Clip, Loop, Rate, Reaction ? .08f : .12f);
        if (Reaction)
            LastReactionFrame = GFrameCounter;
    }
    if (!bSharedPresentation)
        Body->SetPlayRate(Rate);
    ActiveAction = Key;
    Until = AnimationTime() + Clip->GetPlayLength() / FMath::Max(.01f, Rate);
    return true;
}
double UDaocCombatAnimationComponent::AnimationTime() const
{
    return GetWorld() ? GetWorld()->GetTimeSeconds() : FPlatformTime::Seconds();
}
void UDaocCombatAnimationComponent::StartPlayback(UAnimSequence *Clip, bool Loop, float Rate, float BlendSeconds)
{
    // Preserve gait phase when switching between walking and running.
    float Position = 0;
    const auto IsGait = [](UAnimSequence *Sequence) { return Sequence && (Sequence->GetName().EndsWith(TEXT("_Walk")) || Sequence->GetName().EndsWith(TEXT("_Run"))); };
    if (Loop && bPlaybackLoop && IsGait(PlaybackClip) && IsGait(Clip))
    {
        const float PreviousLength = PlaybackClip->GetPlayLength();
        if (PreviousLength > 0)
            Position = FMath::Fmod(float(AnimationTime() - PlaybackStarted) * PlaybackRate, PreviousLength) / PreviousLength * Clip->GetPlayLength();
    }
    if (!bSharedPresentation)
        if (auto *Instance = UCamelotCharacterAnimInstance::Ensure(Body))
            Instance->PlayBlended(Clip, Loop, Rate, Position, BlendSeconds);
    PlaybackClip = Clip;
    PlaybackRate = Rate;
    bPlaybackLoop = Loop;
    PlaybackBlendSeconds = BlendSeconds;
    PlaybackStarted = AnimationTime() - Position / FMath::Max(.01f, Rate);
    ++PlaybackRevision;
}
int32 UDaocCombatAnimationComponent::AttackAction(int32 Style, const UDaocCombatAnimationCatalog *C, EDaocWeaponAnimation W)
{
    if (Style == 500)
        return W == EDaocWeaponAnimation::Crossbow ? 22 : 15;
    if (Style == 503)
        return W == EDaocWeaponAnimation::Crossbow ? 24 : 17;
    if (Style == 501)
        return 25;
    if (Style == 502)
        return 28;
    if (C)
        if (const auto *Action = C->Styles.Find(Style))
            return *Action;
    switch (W)
    {
    case EDaocWeaponAnimation::TwoHand:
        return 8;
    case EDaocWeaponAnimation::Pole:
        return 11;
    case EDaocWeaponAnimation::Staff:
        return 18;
    case EDaocWeaponAnimation::HandToHand:
        return 105;
    case EDaocWeaponAnimation::Scythe:
        return 114;
    case EDaocWeaponAnimation::Flexible:
        return 91;
    default:
        return 0;
    }
}
void UDaocCombatAnimationComponent::HandleEvent(const FDaocCombatEvent &E)
{
    const double Now = AnimationTime();
    if (E.Kind == EDaocCombatEventKind::Revive && E.TargetObjectId == EntityId)
    {
        ResetCombat();
        return;
    }
    if (E.Kind == EDaocCombatEventKind::Death && E.TargetObjectId == EntityId)
    {
        ResetCombat();
        bDead = true;
        if (!PlayAction(TEXT("G_9")) && Body && !bSharedPresentation)
            Body->Stop();
        return;
    }
    if (bDead)
        return;
    if (E.Kind == EDaocCombatEventKind::Melee && (E.SourceObjectId == EntityId || E.TargetObjectId == EntityId))
        CombatStanceUntil = Now + 10;
    if (E.Kind == EDaocCombatEventKind::Cast && E.SourceObjectId == EntityId)
    {
        PendingReaction.Empty();
        bAttackAction = false;
        PendingLoop.Empty();
        const auto *Spell = Catalog ? Catalog->Spells.Find(E.EffectId) : nullptr;
        const int32 Start = Spell ? Spell->Start : 16;
        LoopAction = Spell ? Spell->Loop : 17;
        if (Start <= 0 && LoopAction <= 0)
        {
            bCasting = bCastLoop = false;
            Until = 0;
            return;
        }
        bCasting = true;
        bCastLoop = false;
        CastUntil = Now + E.Duration;
        if (!PlayAction(FString::Printf(TEXT("G_%d"), Start)))
        {
            bCastLoop = true;
            PlayAction(FString::Printf(TEXT("G_%d"), LoopAction), true);
        }
        return;
    }
    if (E.Kind == EDaocCombatEventKind::Interrupt && E.SourceObjectId == EntityId)
    {
        PendingReaction.Empty();
        bAttackAction = false;
        bCasting = bCastLoop = false;
        Until = 0;
        ActiveAction.Empty();
        PendingLoop.Empty();
        return;
    }
    if (E.Kind == EDaocCombatEventKind::SpellEffect && E.SourceObjectId == EntityId)
    {
        PendingReaction.Empty();
        bAttackAction = false;
        const auto *Spell = Catalog ? Catalog->Spells.Find(E.EffectId) : nullptr;
        bCasting = bCastLoop = false;
        PendingLoop.Empty();
        Until = 0;
        PlayAction(FString::Printf(TEXT("G_%d"), Spell ? Spell->Finish : 18));
        return;
    }
    if (E.Kind == EDaocCombatEventKind::Melee && E.SourceObjectId == EntityId)
    {
        PendingLoop.Empty();
        EDaocWeaponAnimation W = EDaocWeaponAnimation::OneHand;
        if (Catalog)
            if (const auto *Kind = Catalog->Weapons.Find(E.WeaponId))
                W = *Kind;
        bCasting = bCastLoop = false;
        const int32 Action = AttackAction(E.StyleId, Catalog, W);
        if (!PlayAction(FString::Printf(TEXT("C_%d"), Action)))
            PlayAction(TEXT("C_0"));
        bAttackAction = true;
        if (E.StyleId == 500 && E.Stance > 0)
        {
            TransitionAt = Until;
            Until = Now + E.Stance / 10.f;
            PendingLoop = FString::Printf(TEXT("C_%d"), W == EDaocWeaponAnimation::Crossbow ? 23 : 16);
        }
        return;
    }
    if (E.Kind == EDaocCombatEventKind::Melee && E.TargetObjectId == EntityId && !bCasting)
    {
        const int32 Reaction = E.ResultCode == 1 ? 31 : E.ResultCode == 2                        ? 40
                                                    : E.ResultCode == 3                          ? 33
                                                    : (E.ResultCode == 10 || E.ResultCode == 11) ? 3
                                                                                                 : -1;
        if (Reaction >= 0)
        {
            const FString Key = FString::Printf(TEXT("C_%d"), Reaction);
            if (bAttackAction && Now < Until && (PendingLoop.IsEmpty() || Now < TransitionAt))
                PendingReaction = Key;
            else
            {
                PendingReaction.Empty();
                PendingLoop.Empty();
                bAttackAction = false;
                PlayAction(Key); // A second hit restarts now, blended from the displayed pose.
            }
        }
    }
}
void UDaocCombatAnimationComponent::UpdateLocomotion(UAnimSequence *Clip, bool Loop, bool Attack, int32 Weapon, bool InferCombatStance)
{
    if (!Body || !Body->GetSkeletalMeshAsset() || bDead)
        return;
    LastWeaponModel = Weapon;
    ReturnLocomotionClip = Clip;
    bool bCombatIdle = false;
    if ((Attack || (InferCombatStance && AnimationTime() < CombatStanceUntil)) && Clip && Clip->GetName().EndsWith(TEXT("_Idle")))
        if (auto *Ready = GetCombatIdleClip())
        {
            ReturnLocomotionClip = Ready;
            bCombatIdle = true;
        }
    if (bCasting || AnimationTime() < Until)
        return;
    if (!PendingReaction.IsEmpty())
    {
        const FString Key = MoveTemp(PendingReaction);
        PendingReaction.Empty();
        bAttackAction = false;
        if (PlayAction(Key))
            return;
    }
    bAttackAction = false;
    Clip = ReturnLocomotionClip;
    const bool WasAction = !ActiveAction.IsEmpty();
    ActiveAction.Empty();
    bVisualActionUnavailable = false;
    auto *Instance = Body->GetSingleNodeInstance();
    if (Clip && (bCombatIdle || Clip->GetSkeleton() == Body->GetSkeletalMeshAsset()->GetSkeleton()) && (PlaybackClip != Clip || (!bSharedPresentation && (!Instance || Instance->GetCurrentAsset() != Clip))))
    {
        StartPlayback(Clip, Loop, 1, WasAction ? .14f : .18f);
    }
}
UAnimSequence *UDaocCombatAnimationComponent::GetCombatIdleClip()
{
    int32 Idle = 4;
    if (Catalog)
        if (const auto *Weapon = Catalog->Weapons.Find(LastWeaponModel))
            switch (*Weapon)
            {
            case EDaocWeaponAnimation::TwoHand:
                Idle = 5;
                break;
            case EDaocWeaponAnimation::Pole:
                Idle = 6;
                break;
            case EDaocWeaponAnimation::Staff:
                Idle = 14;
                break;
            case EDaocWeaponAnimation::Bow:
                Idle = 7;
                break;
            case EDaocWeaponAnimation::HandToHand:
                Idle = 104;
                break;
            case EDaocWeaponAnimation::Scythe:
                Idle = 113;
                break;
            default:
                break;
            }
    return Resolve(FString::Printf(TEXT("C_%d"), Idle));
}
bool UDaocCombatAnimationComponent::GetPlaybackState(UAnimSequence *&Clip, float &Position, bool &Loop, float &Rate, bool &Action, bool &Hold, uint64 &Revision) const
{
    Clip = PlaybackClip;
    if (!Clip || Clip->GetPlayLength() <= 0 || bVisualActionUnavailable)
        return false;
    Loop = bPlaybackLoop;
    Rate = PlaybackRate;
    Action = !ActiveAction.IsEmpty() || bDead;
    Hold = bDead;
    const float Elapsed = FMath::Max(0., AnimationTime() - PlaybackStarted) * Rate;
    Position = Loop ? FMath::Fmod(Elapsed, Clip->GetPlayLength()) : FMath::Min(Elapsed, Clip->GetPlayLength());
    Revision = PlaybackRevision;
    return true;
}
void UDaocCombatAnimationComponent::RestorePlaybackPosition(float Position)
{
    bSharedPresentation = false;
    if (!Body || !PlaybackClip)
        return;
    Position = FMath::Clamp(Position, 0.f, PlaybackClip->GetPlayLength());
    if (auto *Instance = UCamelotCharacterAnimInstance::Ensure(Body))
        Instance->PlayBlended(PlaybackClip, bPlaybackLoop, PlaybackRate, Position, 0);
    PlaybackStarted = AnimationTime() - Position / FMath::Max(.01f, PlaybackRate);
}
bool UDaocCombatAnimationComponent::IsPresentationBlending() const
{
    const auto *Instance = Body ? Cast<UCamelotCharacterAnimInstance>(Body->GetAnimInstance()) : nullptr;
    return !bSharedPresentation && Instance && Instance->IsBlending();
}
void UDaocCombatAnimationComponent::RestoreCrowdPose(UAnimSequence *Target, float Position, UAnimSequence *Source, float SourcePosition, float TargetWeight)
{
    UAnimSequence *Clips[] = {Source, Target};
    float Positions[] = {SourcePosition, Position}, Weights[] = {1 - TargetWeight, TargetWeight};
    RestoreCrowdSamples(Clips, Positions, Weights, true);
}
void UDaocCombatAnimationComponent::RestoreCrowdSamples(TConstArrayView<UAnimSequence *> Clips, TConstArrayView<float> Positions, TConstArrayView<float> Weights, bool SameRevision)
{
    if (!Body || Clips.IsEmpty() || Clips.Num() != Positions.Num() || Clips.Num() != Weights.Num())
        return;
    TArray<FCamelotAnimationSample, TInlineAllocator<4>> Samples;
    for (int32 I = 0; I < Clips.Num(); ++I)
        if (Clips[I] && Weights[I] > 0)
            Samples.Add({Clips[I], Positions[I], Weights[I]});
    if (Samples.IsEmpty())
        return;
    if (auto *Instance = UCamelotCharacterAnimInstance::Ensure(Body))
    {
        Instance->SetCrowdSamples(Samples);
        Body->TickAnimation(0, false);
        Body->RefreshBoneTransforms();
        UAnimSequence *Current = nullptr;
        float Now = 0, Rate = 1;
        bool Loop = false, Action = false, Hold = false;
        uint64 Revision = 0;
        if (GetPlaybackState(Current, Now, Loop, Rate, Action, Hold, Revision))
        {
            // The newest target can still have zero weight. Its clock belongs to
            // the new event even when older samples happen to use the same clip.
            const float Position = SameRevision && Current == Clips.Last() ? Positions.Last() : Now;
            const bool SamePose = Samples.Num() == 1 && Current == Samples[0].Clip && FMath::IsNearlyEqual(Position, Samples[0].Position, .001f);
            Instance->PlayBlended(Current, Loop, Rate, Position, SamePose ? 0.f : .08f);
            PlaybackStarted = AnimationTime() - Position / FMath::Max(.01f, Rate);
        }
    }
    bSharedPresentation = false;
}
void UDaocCombatAnimationComponent::SetSharedPresentation(bool Shared)
{
    if (bSharedPresentation == Shared)
        return;
    bSharedPresentation = Shared;
    if (!Shared)
    {
        UAnimSequence *Clip = nullptr;
        float Position = 0, Rate = 1;
        bool Loop = false, Action = false, Hold = false;
        uint64 Revision = 0;
        if (GetPlaybackState(Clip, Position, Loop, Rate, Action, Hold, Revision))
            RestorePlaybackPosition(Position);
        else if (Body && bDead)
            Body->Stop();
    }
}
void UDaocCombatAnimationComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction *Tick)
{
    Super::TickComponent(Dt, Type, Tick);
    const double Now = AnimationTime();
    if (!bDead && !bCasting && !PendingReaction.IsEmpty() && Now >= Until)
    {
        const FString Key = MoveTemp(PendingReaction);
        PendingReaction.Empty();
        bAttackAction = false;
        PlayAction(Key);
    }
    if (!PendingLoop.IsEmpty() && Now >= TransitionAt)
    {
        const double End = Until;
        const FString Aim = PendingLoop;
        PendingLoop.Empty();
        if (Now < End)
        {
            PlayAction(Aim, true);
            Until = End;
            bAttackAction = false;
            if (!PendingReaction.IsEmpty())
            {
                const FString Reaction = MoveTemp(PendingReaction);
                PendingReaction.Empty();
                PlayAction(Reaction);
            }
        }
    }
    if (!bCasting || bDead)
        return;
    if (Now > CastUntil + .5)
    {
        bCasting = bCastLoop = false;
        Until = 0;
        return;
    }
    if (!bCastLoop && Now >= Until)
    {
        bCastLoop = true;
        PlayAction(FString::Printf(TEXT("G_%d"), LoopAction), true);
    }
}
