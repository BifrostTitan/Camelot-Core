#include "DaocEffectSubsystem.h"
#include "CamelotLiveCrowd.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
bool LiveAnchor(const TWeakObjectPtr<USceneComponent> &Anchor)
{
    return Anchor.IsValid() && Anchor->IsRegistered() && Anchor->GetOwner() && !Anchor->GetOwner()->IsActorBeingDestroyed();
}
} // namespace

void UDaocEffectSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);
    MaxActiveSystems = FMath::Clamp(MaxActiveSystems, 1, 512);
    MaxParticles = FMath::Clamp(MaxParticles, 1, 262144);
    MaxStartsPerFrame = FMath::Clamp(MaxStartsPerFrame, 1, 128);
    if (!DefaultCatalog.IsNull())
        LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(DefaultCatalog.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &UDaocEffectSubsystem::CatalogReady));
}
bool UDaocEffectSubsystem::ShouldCreateSubsystem(UObject *Outer) const
{
    const auto *World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}
void UDaocEffectSubsystem::CatalogReady()
{
    Catalog = DefaultCatalog.Get();
}
void UDaocEffectSubsystem::SetCatalog(UDaocEffectCatalog *Prepared)
{
    if (LoadHandle)
        LoadHandle->CancelHandle();
    ResetEffects();
    Catalog = Prepared;
}
void UDaocEffectSubsystem::Deinitialize()
{
    if (LoadHandle)
        LoadHandle->CancelHandle();
    ResetEffects();
    Catalog = nullptr;
    Super::Deinitialize();
}
TStatId UDaocEffectSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UDaocEffectSubsystem, STATGROUP_Tickables);
}
void UDaocEffectSubsystem::Release(int32 Index)
{
    if (auto *Component = Active[Index].Component.Get())
    {
        Component->DeactivateImmediate();
        Component->ReleaseToPool();
    }
    ReservedParticles -= Active[Index].Cost;
    Active.RemoveAtSwap(Index);
    ActiveSystems = Active.Num();
}
void UDaocEffectSubsystem::ResetEffects()
{
    while (!Active.IsEmpty())
        Release(Active.Num() - 1);
    Pending.Reset();
    ReservedParticles = ActiveSystems = 0;
    CrowdPresenter.Reset();
    ReleasesThisFrame.Reset();
}
void UDaocEffectSubsystem::CancelGather(int32 ObjectId)
{
    Pending.RemoveAll([&](const auto &R) { return R.bGather && R.SourceId == ObjectId; });
    for (int32 I = Active.Num() - 1; I >= 0; --I)
        if (Active[I].Request.bGather && Active[I].Request.SourceId == ObjectId)
            Release(I);
}
void UDaocEffectSubsystem::Queue(const TArray<int32> &Ids, const FDaocEffectRequest &Request)
{
    for (int32 Id : Ids)
    {
        if (!Catalog || !Catalog->Effects.Contains(Id))
            continue;
        const uint64 ReleaseKey = (uint64(uint32(Request.SourceId)) << 32) | uint32(Id);
        if (Request.bRelease)
        {
            if (ReleaseFrame != GFrameCounter)
            {
                ReleasesThisFrame.Reset();
                ReleaseFrame = GFrameCounter;
            }
            if (ReleasesThisFrame.Contains(ReleaseKey))
            {
                // AoE packets repeat caster visuals for every victim. Keep one release,
                // while retaining the projectile/result requests for each target.
                if (Request.bLocal)
                {
                    if (auto *Queued = Pending.FindByPredicate([&](const auto &R) { return R.bRelease && R.SourceId == Request.SourceId && R.Effect == Id; }))
                        Queued->bLocal = true;
                    if (auto *Running = Active.FindByPredicate([&](const auto &A) { return A.Request.bRelease && A.Request.SourceId == Request.SourceId && A.Request.Effect == Id; }))
                        Running->Request.bLocal = true;
                }
                continue;
            }
        }
        if (Pending.Num() >= 1024)
        {
            if (Request.bLocal)
            {
                const int32 Replace = Pending.IndexOfByPredicate([](const auto &R) { return !R.bLocal; });
                if (Replace != INDEX_NONE)
                    Pending.RemoveAtSwap(Replace);
                else
                {
                    ++DroppedEffects;
                    continue;
                }
            }
            else
            {
                ++DroppedEffects;
                continue;
            }
            ++DroppedEffects;
        }
        auto R = Request;
        R.Effect = Id;
        Pending.Add(R);
        if (Request.bRelease)
            ReleasesThisFrame.Add(ReleaseKey);
    }
}
void UDaocEffectSubsystem::HandleCombatEvent(const FDaocCombatEvent &Event, USceneComponent *Source, USceneComponent *Target, bool bLocal, UCamelotLiveCrowdPresenter *Crowd)
{
    CrowdPresenter = Crowd;
    if (Event.Kind == EDaocCombatEventKind::Interrupt || Event.Kind == EDaocCombatEventKind::Death)
    {
        CancelGather(Event.SourceObjectId);
        return;
    }
    if (!Catalog || !Source || !GetWorld())
        return;
    const auto *Spell = Catalog->Spells.Find(Event.EffectId);
    if (!Spell)
        return;
    FDaocEffectRequest R;
    R.SourceId = Event.SourceObjectId;
    R.TargetId = Event.TargetObjectId;
    R.Anchor = Source;
    R.At = GetWorld()->GetTimeSeconds();
    R.bLocal = bLocal;
    if (Event.Kind == EDaocCombatEventKind::Cast)
    {
        CancelGather(Event.SourceObjectId);
        R.bGather = true;
        R.Duration = FMath::Clamp(Event.Duration, 0.f, 60.f);
        Queue(Spell->Gather, R);
    }
    else if (Event.Kind == EDaocCombatEventKind::SpellEffect)
    {
        CancelGather(Event.SourceObjectId);
        R.bRelease = true;
        Queue(Spell->Release, R);
        R.bRelease = false;
        if (!Target)
            return;
        const float Travel = FMath::Clamp(Event.Duration, 0.f, 10.f);
        R.Destination = Target;
        R.Travel = Travel;
        if (Travel > 0)
            Queue(Spell->Projectile, R);
        R.Travel = 0;
        R.Destination.Reset();
        R.Anchor = Target;
        R.bTargetAnchor = true;
        R.At += Travel;
        Queue(Event.ResultCode ? Spell->Hit : Spell->Miss, R);
    }
}
FVector UDaocEffectSubsystem::AnchorLocation(USceneComponent *Anchor, FName Socket, int32 ObjectId) const
{
    const bool Left = Socket == TEXT("Bip01 L Held"), Right = Socket == TEXT("Bip01 R Held");
    if (Left || Right)
        Socket = Left ? TEXT("Bip01 L Hand") : TEXT("Bip01 R Hand");
    FTransform Shared;
    if (CrowdPresenter.IsValid() && CrowdPresenter->GetCharacterSocketTransform(ObjectId, Socket, Shared))
        return Shared.GetLocation();
    if (auto *Body = Cast<USkeletalMeshComponent>(Anchor))
        if (!Body->bHiddenInGame && Body->DoesSocketExist(Socket))
            return Body->GetSocketLocation(Socket);
    if (Left || Right)
        return Anchor->GetComponentTransform().TransformPosition(FVector(0, Left ? -18 : 18, 90));
    return Anchor->GetComponentLocation();
}
bool UDaocEffectSubsystem::Start(const FDaocEffectRequest &R, double Now)
{
    if (!Catalog || !LiveAnchor(R.Anchor))
        return false;
    const auto *Effect = Catalog->Effects.Find(R.Effect);
    if (!Effect || !Effect->System || Effect->ParticleCost <= 0)
        return false;
    const int32 Cost = Effect->ParticleCost;
    if (Cost > MaxParticles)
        return false;
    if (R.bLocal)
        while (Active.Num() >= MaxActiveSystems || ReservedParticles + Cost > MaxParticles)
        {
            const int32 Replace = Active.IndexOfByPredicate([](const auto &A) { return !A.Request.bLocal; });
            if (Replace == INDEX_NONE)
                break;
            Release(Replace);
            ++DroppedEffects;
        }
    if (Active.Num() >= MaxActiveSystems || ReservedParticles + Cost > MaxParticles)
        return false;
    const FVector Position = AnchorLocation(R.Anchor.Get(), Effect->Socket, R.bTargetAnchor ? R.TargetId : R.SourceId);
    auto *Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Effect->System, Position, FRotator::ZeroRotator, FVector::OneVector, false, false, ENCPoolMethod::ManualRelease, false);
    if (!Component)
        return false;
    Component->SetCastShadow(false);
    if (Effect->bAutoScale)
        Component->SetWorldScale3D(R.Anchor->GetComponentScale());
    Component->SetForceSolo(false);
    Component->SetCustomTimeDilation(Effect->PlaybackRate);
    Component->Activate(true);
    FDaocActiveEffect A;
    A.Component = Component;
    A.Request = R;
    A.Origin = Position;
    A.Started = Now;
    const float Duration = R.Travel > 0 ? R.Travel : R.Duration > 0 ? R.Duration
                                                                    : Effect->Duration;
    A.StopAt = Now + FMath::Clamp(Duration, .01f, 60.f);
    A.ReleaseAt = A.StopAt + FMath::Clamp(Effect->Tail / FMath::Max(.01f, Effect->PlaybackRate), 0.f, 10.f);
    A.Cost = Cost;
    Active.Add(A);
    ActiveSystems = Active.Num();
    ReservedParticles += Cost;
    return true;
}
void UDaocEffectSubsystem::Tick(float DeltaSeconds)
{
    if (!GetWorld() || GetWorld()->IsPaused())
        return;
    const double Now = GetWorld()->GetTimeSeconds();
    for (int32 I = Active.Num() - 1; I >= 0; --I)
    {
        auto &A = Active[I];
        auto *C = A.Component.Get();
        if (!C || !LiveAnchor(A.Request.Anchor) || (A.Request.Travel > 0 && !LiveAnchor(A.Request.Destination)) || Now >= A.ReleaseAt || C->IsComplete())
        {
            Release(I);
            continue;
        }
        if (Now >= A.StopAt && !A.bStopped)
        {
            C->Deactivate();
            A.bStopped = true;
        }
        if (A.Request.Travel > 0)
            C->SetWorldLocation(FMath::Lerp(A.Origin, AnchorLocation(A.Request.Destination.Get(), NAME_None, A.Request.TargetId), FMath::Clamp(float((Now - A.Started) / A.Request.Travel), 0.f, 1.f)));
        else if (A.Request.bGather)
            if (const auto *Effect = Catalog ? Catalog->Effects.Find(A.Request.Effect) : nullptr)
                C->SetWorldLocation(AnchorLocation(A.Request.Anchor.Get(), Effect->Socket, A.Request.SourceId));
    }
    FVector View = FVector::ZeroVector;
    FRotator Rotation;
    auto *Controller = GetWorld()->GetFirstPlayerController();
    if (Controller)
        Controller->GetPlayerViewPoint(View, Rotation);
    Pending.Sort([&](const auto &A, const auto &B) {
        if (A.bLocal != B.bLocal)
            return A.bLocal;
        return FVector::DistSquared(A.Anchor.IsValid() ? A.Anchor->GetComponentLocation() : FVector(UE_BIG_NUMBER), View) < FVector::DistSquared(B.Anchor.IsValid() ? B.Anchor->GetComponentLocation() : FVector(UE_BIG_NUMBER), View);
    });
    int32 Starts = 0;
    for (int32 I = 0; I < Pending.Num();)
    {
        const auto R = Pending[I];
        if (!LiveAnchor(R.Anchor))
        {
            Pending.RemoveAt(I);
            continue;
        }
        if (R.At > Now || Starts >= MaxStartsPerFrame)
        {
            ++I;
            continue;
        }
        Pending.RemoveAt(I);
        // Never replay old battlefield bursts after a hitch or overload.
        ++Starts;
        if (Now - R.At > .2 || (Controller && !R.bLocal && FVector::DistSquared(R.Anchor->GetComponentLocation(), View) > FMath::Square(MaxDistance)) || !Start(R, Now))
            ++DroppedEffects;
    }
}
