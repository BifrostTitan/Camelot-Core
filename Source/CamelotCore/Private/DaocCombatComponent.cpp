#include "DaocCombatComponent.h"
#include "DaocNetworkSubsystem.h"
#include "DaocZoneActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"

UDaocCombatComponent::UDaocCombatComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    Hotbar.SetNum(24);
}
void UDaocCombatComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!Network)
        SetNetworkClient(UDaocNetworkSubsystem::Get(this));
}
void UDaocCombatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    SetNetworkClient(nullptr);
    Super::EndPlay(Reason);
}
void UDaocCombatComponent::SetNetworkClient(UDaocNetworkSubsystem *Client)
{
    if (Network)
        Network->OnCombatEvent.RemoveDynamic(this, &UDaocCombatComponent::HandleCombatEvent);
    Network = Client;
    LastSkillsRevision = -1;
    Hotbar.SetNum(24);
    VisibleHotbarCount = 1;
    for (auto &A : Hotbar)
        A = {};
    LastFeedback.Empty();
    CombatMessages.Empty();
    if (Network)
    {
        Network->OnCombatEvent.AddDynamic(this, &UDaocCombatComponent::HandleCombatEvent);
        if (const auto *Pawn = Cast<APawn>(GetOwner()); Pawn && Pawn->IsLocallyControlled())
            Network->LocalPlayerPawn = GetOwner();
    }
}
FDaocCombatState UDaocCombatComponent::GetState() const
{
    return Network ? Network->GetCombatState() : FDaocCombatState();
}
TArray<FDaocCombatAbility> UDaocCombatComponent::GetLearnedAbilities() const
{
    return Network ? Network->GetCombatAbilities() : TArray<FDaocCombatAbility>();
}
TArray<FDaocCombatEffect> UDaocCombatComponent::GetEffects() const
{
    return Network ? Network->GetCombatEffects() : TArray<FDaocCombatEffect>();
}
TArray<FDaocCombatTarget> UDaocCombatComponent::GetTargets() const
{
    return Network ? Network->GetCombatTargets() : TArray<FDaocCombatTarget>();
}
bool UDaocCombatComponent::SelectTarget(int32 Id)
{
    return Network && Network->SelectCombatTarget(Id);
}
TArray<FDaocChatMessage> UDaocCombatComponent::GetChatMessages() const
{
    return Network ? Network->GetChatMessages() : TArray<FDaocChatMessage>();
}
int64 UDaocCombatComponent::GetChatRevision() const
{
    return Network ? Network->GetChatRevision() : -1;
}
bool UDaocCombatComponent::SetAttack(bool Enabled)
{
    return Network && Network->RequestAttack(Enabled);
}
void UDaocCombatComponent::Release()
{
    if (Network && Network->RequestRelease())
        LastFeedback = TEXT("Release requested; waiting for DOL.");
}
bool UDaocCombatComponent::SetGroundTarget(FVector P)
{
    return Network && Network->SetCombatGroundTarget(P);
}
bool UDaocCombatComponent::CancelEffect(int32 Id)
{
    return Network && Network->RequestCancelEffect(Id);
}
bool UDaocCombatComponent::ActivateAbility(const FDaocCombatAbility &A)
{
    const bool Sent = Network && Network->RequestAbility(A, GetOwner());
    LastFeedback = Sent ? FString::Printf(TEXT("Requested: %s"), *A.Name) : TEXT("Ability unavailable: check connection, learned list, cooldown, and life state.");
    return Sent;
}
bool UDaocCombatComponent::AssignHotbar(int32 Slot, const FDaocCombatAbility &A)
{
    if (!Hotbar.IsValidIndex(Slot))
        return false;
    const auto Abilities = GetLearnedAbilities();
    const auto *Match = Abilities.FindByPredicate([&](const FDaocCombatAbility &B) { return B.Kind == A.Kind && B.ServerIndex == A.ServerIndex && B.SpellLine == A.SpellLine && B.Name == A.Name && B.Level == A.Level && B.Kind != EDaocAbilityKind::Specialization; });
    if (!Match)
        return false;
    Hotbar[Slot] = *Match;
    return true;
}
bool UDaocCombatComponent::ActivateHotbar(int32 Slot)
{
    RefreshHotbar();
    return Slot >= 0 && Slot < FMath::Clamp(VisibleHotbarCount, 1, 3) * 8 && Hotbar.IsValidIndex(Slot) && !Hotbar[Slot].Name.IsEmpty() && ActivateAbility(Hotbar[Slot]);
}
bool UDaocCombatComponent::ClearHotbar(int32 Slot)
{
    if (!Hotbar.IsValidIndex(Slot))
        return false;
    Hotbar[Slot] = {};
    return true;
}
void UDaocCombatComponent::SetVisibleHotbarCount(int32 Count)
{
    VisibleHotbarCount = FMath::Clamp(Count, 1, 3);
}
int32 UDaocCombatComponent::KeyboardSlot(int32 Slot) const
{
    const auto *Pawn = Cast<APawn>(GetOwner());
    const auto *PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (PC && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)))
        return Slot + 16;
    if (PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)))
        return Slot + 8;
    return Slot;
}
void UDaocCombatComponent::RefreshHotbar()
{
    const auto State = GetState();
    if (!State.bSkillsReady)
    {
        for (auto &A : Hotbar)
            A = {};
        LastSkillsRevision = -1;
        return;
    }
    const auto All = GetLearnedAbilities();
    for (auto &A : Hotbar)
        if (!A.Name.IsEmpty())
        {
            const auto *Current = All.FindByPredicate([&](const FDaocCombatAbility &B) { return B.Name == A.Name && B.Kind == A.Kind && B.LineName == A.LineName && B.Level == A.Level && B.Icon == A.Icon; });
            A = Current ? *Current : FDaocCombatAbility();
        }
    if (bAutoFillHotbar && LastSkillsRevision != State.SkillsRevision)
    {
        for (const auto &A : All)
        {
            if (A.Kind == EDaocAbilityKind::Specialization)
                continue;
            if (MakeArrayView(Hotbar.GetData(), 8).ContainsByPredicate([&](const FDaocCombatAbility &B) { return B.Kind == A.Kind && B.SpellLine == A.SpellLine && B.ServerIndex == A.ServerIndex && !B.Name.IsEmpty(); }))
                continue;
            auto *Empty = MakeArrayView(Hotbar.GetData(), 8).FindByPredicate([](const FDaocCombatAbility &B) { return B.Name.IsEmpty(); });
            if (!Empty)
                break;
            *Empty = A;
        }
    }
    LastSkillsRevision = State.SkillsRevision;
}
void UDaocCombatComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction *Function)
{
    Super::TickComponent(Dt, Type, Function);
    RefreshHotbar();
}
void UDaocCombatComponent::HandleCombatEvent(const FDaocCombatEvent &E)
{
    if (E.Kind == EDaocCombatEventKind::Message)
    {
        LastFeedback = E.Text;
        if (!E.Text.IsEmpty())
        {
            CombatMessages.Add(E.Text);
            if (CombatMessages.Num() > 6)
                CombatMessages.RemoveAt(0);
        }
    }
    OnCombatEvent.Broadcast(E);
}
void UDaocCombatComponent::CycleTarget()
{
    auto Targets = GetTargets();
    Targets.RemoveAll([](const FDaocCombatTarget &T) { return T.bLocal || T.HealthPercent == 0; });
    const FVector Origin = GetOwner()->GetActorLocation();
    Targets.Sort([&](const FDaocCombatTarget &A, const FDaocCombatTarget &B) { return FVector::DistSquared(A.Position, Origin) < FVector::DistSquared(B.Position, Origin); });
    if (Targets.IsEmpty())
    {
        ClearTarget();
        return;
    }
    const int32 Current = Targets.IndexOfByPredicate([&](const FDaocCombatTarget &T) { return T.ObjectId == GetState().TargetObjectId; });
    SelectTarget(Targets[(Current + 1) % Targets.Num()].ObjectId);
}
void UDaocCombatComponent::TargetSelf()
{
    if (Network)
        SelectTarget(Network->PlayerObjectId);
}
void UDaocCombatComponent::ClearTarget()
{
    SelectTarget(0);
}
void UDaocCombatComponent::ToggleAttack()
{
    SetAttack(!GetState().bAttacking);
}
void UDaocCombatComponent::BindDefaultInput(UInputComponent *Input)
{
    if (!Input)
        return;
    Input->BindKey(EKeys::Tab, IE_Pressed, this, &UDaocCombatComponent::CycleTarget);
    Input->BindKey(EKeys::T, IE_Pressed, this, &UDaocCombatComponent::TargetSelf);
    Input->BindKey(EKeys::BackSpace, IE_Pressed, this, &UDaocCombatComponent::ClearTarget);
    Input->BindKey(EKeys::F, IE_Pressed, this, &UDaocCombatComponent::ToggleAttack);
    Input->BindKey(EKeys::R, IE_Pressed, this, &UDaocCombatComponent::Release);
    Input->BindKey(EKeys::One, IE_Pressed, this, &UDaocCombatComponent::Slot0);
    Input->BindKey(FInputChord(EKeys::One, true, false, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot0);
    Input->BindKey(FInputChord(EKeys::One, false, true, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot0);
    Input->BindKey(EKeys::Two, IE_Pressed, this, &UDaocCombatComponent::Slot1);
    Input->BindKey(FInputChord(EKeys::Two, true, false, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot1);
    Input->BindKey(FInputChord(EKeys::Two, false, true, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot1);
    Input->BindKey(EKeys::Three, IE_Pressed, this, &UDaocCombatComponent::Slot2);
    Input->BindKey(FInputChord(EKeys::Three, true, false, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot2);
    Input->BindKey(FInputChord(EKeys::Three, false, true, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot2);
    Input->BindKey(EKeys::Four, IE_Pressed, this, &UDaocCombatComponent::Slot3);
    Input->BindKey(FInputChord(EKeys::Four, true, false, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot3);
    Input->BindKey(FInputChord(EKeys::Four, false, true, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot3);
    Input->BindKey(EKeys::Five, IE_Pressed, this, &UDaocCombatComponent::Slot4);
    Input->BindKey(FInputChord(EKeys::Five, true, false, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot4);
    Input->BindKey(FInputChord(EKeys::Five, false, true, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot4);
    Input->BindKey(EKeys::Six, IE_Pressed, this, &UDaocCombatComponent::Slot5);
    Input->BindKey(FInputChord(EKeys::Six, true, false, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot5);
    Input->BindKey(FInputChord(EKeys::Six, false, true, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot5);
    Input->BindKey(EKeys::Seven, IE_Pressed, this, &UDaocCombatComponent::Slot6);
    Input->BindKey(FInputChord(EKeys::Seven, true, false, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot6);
    Input->BindKey(FInputChord(EKeys::Seven, false, true, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot6);
    Input->BindKey(EKeys::Eight, IE_Pressed, this, &UDaocCombatComponent::Slot7);
    Input->BindKey(FInputChord(EKeys::Eight, true, false, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot7);
    Input->BindKey(FInputChord(EKeys::Eight, false, true, false, false), IE_Pressed, this, &UDaocCombatComponent::Slot7);
}
