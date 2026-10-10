#include "CamelotLiveCrowd.h"
#include "ProfilingDebugging/CsvProfiler.h"
CSV_DEFINE_CATEGORY(CamelotLive, true);
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "CamelotCrowdComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DaocCombatAnimationComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

namespace
{
// Compare exact visual values without formatting strings, allocating equipment
// copies or trusting a revision supplied by a Blueprint caller.
bool SameVisualAppearance(const FDaocCharacterAppearance &A, const FDaocCharacterAppearance &B)
{
    if (A.Model != B.Model || A.ModelFlags != B.ModelFlags || A.FaceType != B.FaceType || A.HairStyle != B.HairStyle || A.HairColor != B.HairColor || A.EyeColor != B.EyeColor || A.EyeSize != B.EyeSize || A.LipSize != B.LipSize || A.MoodType != B.MoodType || A.CustomisationStep != B.CustomisationStep || A.Race != B.Race || A.Gender != B.Gender || A.bHoodUp != B.bHoodUp || A.RightWeapon != B.RightWeapon || A.LeftWeapon != B.LeftWeapon || A.bEquipmentKnown != B.bEquipmentKnown || A.Equipment.Num() != B.Equipment.Num())
        return false;
    for (int32 I = 0; I < A.Equipment.Num(); ++I)
    {
        const auto &L = A.Equipment[I];
        const auto &R = B.Equipment[I];
        if (L.Slot != R.Slot || L.Model != R.Model || L.ColorOrEmblem != R.ColorOrEmblem || L.Extension != R.Extension || L.Effect != R.Effect)
            return false;
    }
    return true;
}
} // namespace

FString UCamelotLiveCrowdPresenter::AppearanceKey(const FDaocCharacterAppearance &A)
{
    CSV_SCOPED_TIMING_STAT(CamelotLive, AppearanceKey);
    // Exact visual values, rather than a hash: collisions must never swap armor
    // or dyes.
    FString Key = FString::Printf(TEXT("%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d"), A.Model, A.ModelFlags, A.FaceType, A.HairStyle, A.HairColor, A.EyeColor, A.EyeSize, A.LipSize, A.MoodType, A.CustomisationStep, A.Race, A.Gender, A.bHoodUp, A.RightWeapon, A.LeftWeapon, A.bEquipmentKnown);
    auto Equipment = A.Equipment;
    Equipment.Sort([](const auto &L, const auto &R) { return L.Slot < R.Slot; });
    for (const auto &E : Equipment)
        Key += FString::Printf(TEXT("/%d:%d:%d:%d:%d"), E.Slot, E.Model, E.ColorOrEmblem, E.Extension, E.Effect);
    return Key;
}
void UCamelotLiveCrowdPresenter::Initialize(UWorld *World, UCamelotLiveCrowdCatalog *PreparedCatalog)
{
    Clear();
    RenderWorld = World;
    Catalog = PreparedCatalog;
}
bool UCamelotLiveCrowdPresenter::HasAppearance(int32 Id, const FDaocCharacterAppearance &A) const
{
    const auto *Character = Characters.Find(Id);
    return Character && (SameVisualAppearance(Character->Appearance, A) || Character->Key == AppearanceKey(A));
}
UCamelotLiveCrowdPresenter::FBatch *UCamelotLiveCrowdPresenter::CreateBatch(const FString &Key, USkeletalMeshComponent *Body, int32 Model, int32 MaxBatches, float Tolerance, UAnimSequence *CombatIdle)
{
    if (!Catalog || !RenderWorld.IsValid() || Shared.Num() >= FMath::Clamp(MaxBatches, 1, 64) || Unsupported.Contains(Key))
        return nullptr;
    const auto *Prepared = Catalog->Models.Find(Model);
    if (!Prepared)
    {
        Unsupported.Add(Key);
        return nullptr;
    }
    TArray<USkeletalMeshComponent *> Parts{Body};
    TArray<USceneComponent *> Children;
    Body->GetChildrenComponents(true, Children);
    for (auto *Child : Children)
        if (auto *Part = Cast<USkeletalMeshComponent>(Child))
            if (Part->GetSkeletalMeshAsset() && Part->IsVisible())
                Parts.Add(Part);
    TArray<UAnimSequenceTransformProviderData *> Providers;
    for (auto *Part : Parts)
    {
        const auto *Path = Prepared->MeshProviders.Find(FSoftObjectPath(Part->GetSkeletalMeshAsset()));
        auto *Set = Path ? Path->LoadSynchronous() : nullptr;
        if (!Set || Set->Providers.Num() != 1 || !Set->Providers[0])
        {
            Unsupported.Add(Key);
            return nullptr;
        }
        // Loading saved provider data can finish asynchronously. Retain the full
        // presenter until it is ready, then retry without authoring any assets.
        if (Set->Providers[0]->IsCompiling() || Part->GetSkeletalMeshAsset()->IsCompiling())
            return nullptr;
        Providers.Add(Set->Providers[0]);
    }
    FActorSpawnParameters Params;
    Params.ObjectFlags |= RF_Transient;
    auto *Actor = RenderWorld->SpawnActor<AActor>(Params);
    if (!Actor)
        return nullptr;
    Actor->SetActorTickEnabled(false);
    auto *Batch = NewObject<UCamelotCrowdComponent>(Actor);
    Batch->RegisterComponent();
    if (!Batch->Initialize(Body, Providers, UCamelotCrowdComponent::MaxTrackCount) || !Batch->ConfigureActionPool(16, FMath::Clamp(Tolerance, 0.f, .1f)))
    {
        Unsupported.Add(Key);
        Actor->Destroy();
        return nullptr;
    }
    FBatch Data;
    for (int32 I = 0; I < Providers[0]->GetSequences().Num(); ++I)
        Data.Sequences.Add(Providers[0]->GetSequences()[I].Sequence, I);
    Data.CombatIdle = CombatIdle;
    UAnimSequence *Loops[] = {Prepared->Idle.LoadSynchronous(), Prepared->Walk.LoadSynchronous(), Prepared->Run.LoadSynchronous(), CombatIdle ? CombatIdle : Prepared->Idle.LoadSynchronous()};
    for (int32 L = 0; L < 4; ++L)
    {
        const auto *Index = Data.Sequences.Find(Loops[L]);
        Data.LoopSequences[L] = Index ? *Index : INDEX_NONE;
        if (Index)
            for (int32 Phase = 0; Phase < 4; ++Phase)
                if (!Batch->PlayTrack(L * 4 + Phase, *Index, true, Loops[L]->GetPlayLength() * Phase / 4.f))
                {
                    Unsupported.Add(Key);
                    Actor->Destroy();
                    return nullptr;
                }
    }
    Data.Slot = Batches.IndexOfByKey(nullptr);
    if (Data.Slot == INDEX_NONE)
    {
        Data.Slot = Batches.Num();
        Batches.Add(Batch);
        Actors.Add(Actor);
    }
    else
    {
        Batches[Data.Slot] = Batch;
        Actors[Data.Slot] = Actor;
    }
    return &Shared.Add(Key, MoveTemp(Data));
}
bool UCamelotLiveCrowdPresenter::Present(int32 Id, USkeletalMeshComponent *Body, const FDaocCharacterAppearance &A, UDaocCombatAnimationComponent *Animation, int32 Locomotion, int32 MaxBatches, float Tolerance)
{
    CSV_SCOPED_TIMING_STAT(CamelotLive, Present);
    if (!Body || !Animation || Locomotion < 0 || Locomotion > 2)
    {
        Remove(Id, Animation);
        return false;
    }
    UAnimSequence *Clip = nullptr;
    float Position = 0, Rate = 1;
    bool Loop = false, Action = false, Hold = false;
    uint64 Revision = 0;
    if (!Animation->GetPlaybackState(Clip, Position, Loop, Rate, Action, Hold, Revision))
    {
        CSV_CUSTOM_STAT(CamelotLive, MissingPlayback, 1, ECsvCustomStatOp::Accumulate);
        Remove(Id, Animation);
        return false;
    }
    const auto *Existing = Characters.Find(Id);
    const bool SameAppearance = Existing && SameVisualAppearance(Existing->Appearance, A);
    const FString Key = SameAppearance ? Existing->Key : AppearanceKey(A);
    if (Existing && Existing->Key != Key)
        Remove(Id, Animation);
    if (const auto *Failed = FailedActions.Find(Id))
        if (Failed->Key == Key && Failed->Revision == Revision && FPlatformTime::Seconds() < Failed->RetryAfter)
        {
            CSV_CUSTOM_STAT(CamelotLive, RetryCooldown, 1, ECsvCustomStatOp::Accumulate);
            return false;
        }
    auto *Data = Shared.Find(Key);
    if (!Data)
        Data = CreateBatch(Key, Body, A.Model, MaxBatches, Tolerance, Animation->GetCombatIdleClip());
    if (!Data || Data->LoopSequences[Locomotion] == INDEX_NONE)
    {
        CSV_CUSTOM_STAT(CamelotLive, MissingAppearance, 1, ECsvCustomStatOp::Accumulate);
        Remove(Id, Animation);
        return false;
    }
    auto *Batch = Batches[Data->Slot].Get();
    const int32 ReturnGroup = Locomotion == 0 && Data->CombatIdle && Animation->GetReturnLocomotionClip() == Data->CombatIdle ? 3 : Locomotion;
    if (Data->LoopSequences[ReturnGroup] == INDEX_NONE)
    {
        Remove(Id, Animation);
        return false;
    }
    const int32 ReturnTrack = ReturnGroup * 4 + (GetTypeHash(Id) % 4);
    auto *Character = Characters.Find(Id);
    if (!Character)
    {
        // Keep visible mixed poses until their short blend completes. Offscreen
        // skeletal poses may stop ticking indefinitely, including while casting
        // shadows; their stale blend must not pin them to full presentation.
        const float VisibilityWindow = FMath::Max(.2f, RenderWorld->GetDeltaSeconds() + UE_KINDA_SMALL_NUMBER);
        const bool RecentlyVisible = RenderWorld->TimeSince(Body->GetLastRenderTimeOnScreen()) <= VisibilityWindow;
        if (Animation->IsPresentationBlending() && RecentlyVisible)
        {
            CSV_CUSTOM_STAT(CamelotLive, PendingNativeBlend, 1, ECsvCustomStatOp::Accumulate);
            return false;
        }
        if (!Batch->AddCharacter(Id, Body->GetComponentTransform(), ReturnTrack))
            return false;
        FCharacter New;
        New.Key = Key;
        New.Appearance = A;
        New.Position = Body->GetComponentLocation();
        Character = &Characters.Add(Id, MoveTemp(New));
    }
    else if (!SameAppearance)
        Character->Appearance = A;
    bool Success = true;
    if (Action)
    {
        const auto *Index = Data->Sequences.Find(Clip);
        if (!Index)
        {
            CSV_CUSTOM_STAT(CamelotLive, MissingActionClip, 1, ECsvCustomStatOp::Accumulate);
            Success = false;
        }
        else if (Character->Revision != Revision || !Character->bAction)
        {
            const int32 Before = Batch->GetActionPoolMissCount();
            Success = Batch->PlayCharacterAction(Id, *Index, ReturnTrack, Position, Rate, Hold, Loop, Character->Revision == MAX_uint64 ? 0.f : Animation->GetTransitionDuration());
            PoolMisses += Batch->GetActionPoolMissCount() - Before;
            if (!Success)
                CSV_CUSTOM_STAT(CamelotLive, ActionFallback, 1, ECsvCustomStatOp::Accumulate);
        }
    }
    if (Success)
    {
        Success = Batch->SetCharacterLocomotion(Id, ReturnTrack, !Action, Action ? 0.f : Animation->GetTransitionDuration());
        if (!Success)
            CSV_CUSTOM_STAT(CamelotLive, LocomotionFallback, 1, ECsvCustomStatOp::Accumulate);
    }
    if (!Success)
    {
        Remove(Id, Animation);
        FailedActions.Add(Id, {Key, Revision, FPlatformTime::Seconds() + .2});
        return false;
    }
    Character->Revision = Revision;
    Character->bAction = Action;
    const FVector Location = Body->GetComponentLocation();
    Batch->MoveCharacter(Id, Body->GetComponentTransform(), FVector::DistSquared(Location, Character->Position) > FMath::Square(1000.f));
    Character->Position = Location;
    Data->EmptySince = 0;
    FailedActions.Remove(Id);
    return true;
}
void UCamelotLiveCrowdPresenter::Remove(int32 Id, UDaocCombatAnimationComponent *Animation)
{
    auto *Character = Characters.Find(Id);
    if (Character)
        if (auto *Data = Shared.Find(Character->Key))
        {
            auto *Batch = Batches[Data->Slot].Get();
            if (Animation)
            {
                TArray<UAnimSequence *> Clips;
                TArray<float> Positions, Weights;
                UAnimSequence *Current = nullptr;
                float Position = 0, Rate = 1;
                bool Loop = false, Action = false, Hold = false;
                uint64 Revision = 0;
                Animation->GetPlaybackState(Current, Position, Loop, Rate, Action, Hold, Revision);
                if (Batch->GetCharacterPose(Id, Clips, Positions, Weights))
                    Animation->RestoreCrowdSamples(Clips, Positions, Weights, Character->Revision == Revision);
            }
            Batch->RemoveCharacter(Id);
            if (Batch->GetCharacterCount() == 0)
                Data->EmptySince = FPlatformTime::Seconds();
        }
    Characters.Remove(Id);
    FailedActions.Remove(Id);
}
void UCamelotLiveCrowdPresenter::CollectEmptyBatches()
{
    const double Now = FPlatformTime::Seconds();
    for (auto It = Shared.CreateIterator(); It; ++It)
        if (It.Value().EmptySince > 0 && Now - It.Value().EmptySince > 2)
        {
            const int32 Slot = It.Value().Slot;
            Batches[Slot]->DestroyComponent();
            Actors[Slot]->Destroy();
            Batches[Slot] = nullptr;
            Actors[Slot] = nullptr;
            It.RemoveCurrent();
        }
}
int32 UCamelotLiveCrowdPresenter::GetActivePoseCount() const
{
    int32 Count = 0;
    for (auto Batch : Batches)
        if (Batch)
            Count += Batch->GetActivePoseCount();
    return Count;
}
void UCamelotLiveCrowdPresenter::Clear()
{
    for (auto Batch : Batches)
        if (IsValid(Batch.Get()))
            Batch->DestroyComponent();
    for (auto Actor : Actors)
        if (IsValid(Actor.Get()))
            Actor->Destroy();
    Batches.Reset();
    Actors.Reset();
    Shared.Reset();
    Characters.Reset();
    FailedActions.Reset();
    Unsupported.Reset();
    PoolMisses = 0;
}
