#include "DaocDeveloperComponent.h"
#include "DaocCombatHUD.h"
#include "DaocAdminPanel.h"
#include "DaocCombatComponent.h"
#include "DaocWeatherSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "DaocPlayerCharacter.h"
#include "DaocNetworkSubsystem.h"
#include "DaocZoneActor.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/CommandLine.h"
#include "InputCoreTypes.h"
#include "Framework/Commands/InputChord.h"
UDaocDeveloperComponent::UDaocDeveloperComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}
void UDaocDeveloperComponent::BindInput(UInputComponent *Input)
{
    UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(TEXT("DaocFlyVertical"), EKeys::SpaceBar, 1));
    UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(TEXT("DaocFlyVertical"), EKeys::LeftControl, -1));
    Input->BindAxis(TEXT("DaocFlyVertical"), this, &UDaocDeveloperComponent::Vertical);
    Input->BindKey(FInputChord(EKeys::A, false, true, true, false), IE_Pressed, this, &UDaocDeveloperComponent::ToggleAdminPanel);
    // F8/F9/F10 are editor debugger shortcuts. These chords reach the possessed
    // pawn during PIE without ejecting it or invoking Blueprint debugging.
    Input->BindKey(FInputChord(EKeys::F, false, true, true, false), IE_Pressed, this, &UDaocDeveloperComponent::ToggleFlight);
    Input->BindKey(FInputChord(EKeys::I, false, true, true, false), IE_Pressed, this, &UDaocDeveloperComponent::ToggleInspector);
    Input->BindKey(FInputChord(EKeys::C, false, true, true, false), IE_Pressed, this, &UDaocDeveloperComponent::CopyNif);
    Input->BindKey(FInputChord(EKeys::R, false, true, true, false), IE_Pressed, this, &UDaocDeveloperComponent::RegenerateZone);
    UE_LOG(LogTemp, Display, TEXT("DAoC developer controls v2 bound: Ctrl+Alt+F flight, Ctrl+Alt+I hammer, Ctrl+Alt+C copy, Ctrl+Alt+R regenerate, Ctrl+Alt+A admin panel. Function keys are standalone alternatives."));
    Input->BindKey(EKeys::F8, IE_Pressed, this, &UDaocDeveloperComponent::ToggleFlight);
    Input->BindKey(EKeys::F9, IE_Pressed, this, &UDaocDeveloperComponent::ToggleInspector);
    Input->BindKey(EKeys::F10, IE_Pressed, this, &UDaocDeveloperComponent::CopyNif);
    Input->BindKey(EKeys::F7, IE_Pressed, this, &UDaocDeveloperComponent::RegenerateZone);
}
void UDaocDeveloperComponent::BeginPlay()
{
    Super::BeginPlay();
    if (auto *Net = UDaocNetworkSubsystem::Get(this))
        Net->OnCombatEvent.AddUniqueDynamic(this, &UDaocDeveloperComponent::HandleAdminEvent);
}
void UDaocDeveloperComponent::AddAdminMessage(const FString &Text)
{
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines);
    for (const auto &Line : Lines)
        AdminMessages.Add(Line.Left(600));
    if (AdminMessages.Num() > 96)
        AdminMessages.RemoveAt(0, AdminMessages.Num() - 96);
}
void UDaocDeveloperComponent::HandleAdminEvent(const FDaocCombatEvent &Event)
{
    if (Authorized() && Event.Kind == EDaocCombatEventKind::Message && !Event.Text.IsEmpty())
        AddAdminMessage(Event.Text);
}
FString UDaocDeveloperComponent::GetAdminLog() const
{
    return FString::Join(AdminMessages, TEXT("\n"));
}
FString UDaocDeveloperComponent::GetAdminContext() const
{
    auto *Net = UDaocNetworkSubsystem::Get(this);
    if (!Net)
        return TEXT("Not connected.");
    const FVector Position = Net->LastSubmittedPosition.IsNearlyZero() ? Net->WorldToServer(GetOwner()->GetActorLocation()) : Net->LastSubmittedPosition;
    FString Target = TEXT("none");
    const int32 TargetId = Net->GetCombatState().TargetObjectId;
    for (const auto &T : Net->GetCombatTargets())
        if (T.ObjectId == TargetId)
        {
            Target = FString::Printf(TEXT("%s (#%d)"), *T.Name, TargetId);
            break;
        }
    return FString::Printf(TEXT("Region %d | Zone %d | Server X %.0f Y %.0f Z %.0f | Target %s\nCtrl+Alt+A opens this panel. Select an NPC before editing it; Cycle target changes the server target."), Net->GetServerRegionId(), Net->ActiveZoneId, Position.X, Position.Y, Position.Z, *Target);
}
void UDaocDeveloperComponent::CycleAdminTarget()
{
    if (!Authorized())
        return;
    auto *Net = UDaocNetworkSubsystem::Get(this);
    if (!Net)
        return;
    auto Targets = Net->GetCombatTargets();
    Targets.RemoveAll([](const FDaocCombatTarget &T) { return !T.bNPC || T.HealthPercent == 0; });
    if (Targets.IsEmpty())
    {
        AddAdminMessage(TEXT("No live nearby NPC targets have been received from DOL."));
        return;
    }
    const FVector Position = GetOwner()->GetActorLocation();
    Targets.Sort([Position](const FDaocCombatTarget &A, const FDaocCombatTarget &B) { return FVector::DistSquared(A.Position, Position) < FVector::DistSquared(B.Position, Position); });
    const int32 Current = Targets.IndexOfByPredicate([Net](const FDaocCombatTarget &T) { return T.ObjectId == Net->GetCombatState().TargetObjectId; });
    Net->SelectCombatTarget(Targets[(Current + 1) % Targets.Num()].ObjectId);
}
void UDaocDeveloperComponent::ToggleAdminPanel()
{
    if (AdminPanel.IsValid())
    {
        CloseAdminPanel();
        return;
    }
    if (bPendingAdmin)
    {
        bPendingAdmin = false;
        return;
    }
    if (Authorized())
    {
        OpenAdminPanel();
        return;
    }
    auto *Net = UDaocNetworkSubsystem::Get(this);
    bPendingAdmin = Net && Net->bWorldReady && Net->RequestDeveloperMode(true);
    PendingTime = 0;
    Information = bPendingAdmin ? TEXT("Requesting DOL admin permission...") : TEXT("Finish logging into a character before opening administration.");
}
void UDaocDeveloperComponent::OpenAdminPanel()
{
    if (!Authorized() || AdminPanel.IsValid())
        return;
    auto *Pawn = Cast<ADaocPlayerCharacter>(GetOwner());
    auto *PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    auto *Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (!PC || !Viewport)
        return;
    Pawn->GetCharacterMovement()->StopMovementImmediately();
    if (auto *HUD = Cast<ADaocCombatHUD>(PC->GetHUD()))
        HUD->CloseChat();
    AdminController = PC;
    bPreviousCursor = PC->bShowMouseCursor;
    AdminPanel = CreateDaocAdminPanel(this);
    Viewport->AddViewportWidgetContent(AdminPanel.ToSharedRef(), 200);
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    PC->bShowMouseCursor = true;
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(AdminPanel);
    PC->SetInputMode(Mode);
    if (AdminMessages.IsEmpty())
        AddAdminMessage(TEXT("Choose a command, edit its arguments, then Send. Server responses appear here."));
}
void UDaocDeveloperComponent::CloseAdminPanel()
{
    if (!AdminPanel.IsValid())
        return;
    if (auto *Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
        Viewport->RemoveViewportWidgetContent(AdminPanel.ToSharedRef());
    AdminPanel.Reset();
    if (auto *PC = AdminController.Get())
    {
        PC->SetIgnoreMoveInput(false);
        PC->SetIgnoreLookInput(false);
        auto *Net = UDaocNetworkSubsystem::Get(this);
        // During logout/travel, the frontend owns the next input mode.
        if ((Net && Net->bWorldReady) || FParse::Param(FCommandLine::Get(), TEXT("DaocOffline")))
        {
            PC->bShowMouseCursor = bPreviousCursor;
            if (bPreviousCursor)
                PC->SetInputMode(FInputModeGameAndUI());
            else
                PC->SetInputMode(FInputModeGameOnly());
            if (FSlateApplication::IsInitialized())
                FSlateApplication::Get().SetAllUserFocusToGameViewport();
        }
    }
    AdminController.Reset();
}
bool UDaocDeveloperComponent::ExecuteAdminCommand(const FString &Command)
{
    if (!Authorized())
    {
        AddAdminMessage(TEXT("DOL admin authorization is required."));
        return false;
    }
    const FString Text = Command.TrimStartAndEnd();
    if (Text.IsEmpty() || Text.Len() > 254 || Text.Contains(TEXT("<")) || Text.Contains(TEXT(">")))
    {
        AddAdminMessage(TEXT("Replace command placeholders first; commands must contain 1-254 printable ASCII characters."));
        return false;
    }
    for (TCHAR C : Text)
        if (C < 32 || C > 126)
        {
            AddAdminMessage(TEXT("Use one command with printable ASCII characters."));
            return false;
        }
    TArray<FString> Args;
    Text.ParseIntoArrayWS(Args);
    if (Args.IsEmpty())
        return false;
    if (Args[0].Equals(TEXT("/client"), ESearchCase::IgnoreCase))
    {
        bool Valid = true;
        if (Args.Num() == 2 && Args[1].Equals(TEXT("regen"), ESearchCase::IgnoreCase))
            RegenerateZone();
        else if (Args.Num() == 2 && Args[1].Equals(TEXT("fly"), ESearchCase::IgnoreCase))
            ToggleFlight();
        else if (Args.Num() == 2 && Args[1].Equals(TEXT("inspect"), ESearchCase::IgnoreCase))
            ToggleInspector();
        else if ((Args.Num() == 3 || Args.Num() == 4) && Args[1].Equals(TEXT("weather"), ESearchCase::IgnoreCase))
        {
            auto *Weather = GetWorld()->GetSubsystem<UDaocWeatherSubsystem>();
            const FString Pattern = Args[2].ToLower();
            float Amount = .55f;
            const bool HasAmount = Args.Num() == 4;
            if (!Weather || (HasAmount && (!LexTryParseString(Amount, *Args[3]) || !FMath::IsFinite(Amount) || Amount < 0 || Amount > 1)))
                Valid = false;
            else if (Pattern == TEXT("auto") && !HasAmount)
                Weather->ClearWeatherOverride();
            else if (Pattern == TEXT("clear") && !HasAmount)
                Weather->SetWeatherOverride(EDaocWeatherType::Sunny, 0);
            else if (Pattern == TEXT("snow"))
                Weather->SetWeatherOverride(EDaocWeatherType::Snow, Amount);
            else if (Pattern == TEXT("rain"))
                Weather->SetWeatherOverride(EDaocWeatherType::Rain, Amount);
            else
                Valid = false;
        }
        else
            Valid = false;
        AddAdminMessage(Valid ? TEXT("Local: ") + Text : TEXT("Local commands: /client fly | inspect | regen | weather rain/snow [0..1] | weather clear | weather auto"));
        return Valid;
    }
    auto *Net = UDaocNetworkSubsystem::Get(this);
    FString Verb = Args[0];
    Verb.RemoveFromStart(TEXT("/"));
    Verb.RemoveFromStart(TEXT("&"));
    if (Net && Verb.Equals(TEXT("mob"), ESearchCase::IgnoreCase) && Args.Num() > 1)
    {
        const FString Action = Args[1].ToLower();
        const TArray<FString> TargetActions = {TEXT("model"), TEXT("name"), TEXT("level"), TEXT("size"), TEXT("movehere"), TEXT("peace")};
        if (TargetActions.Contains(Action))
        {
            const int32 Id = Net->GetCombatState().TargetObjectId;
            const auto Targets = Net->GetCombatTargets();
            if (!Targets.ContainsByPredicate([Id](const FDaocCombatTarget &T) { return T.ObjectId == Id && T.bNPC; }))
            {
                AddAdminMessage(TEXT("Select an NPC with Cycle NPC target before editing it."));
                return false;
            }
        }
    }
    const bool Sent = Net && Net->SendAdminCommand(Text);
    // A queued request is not a server acknowledgement; retain the actual reply separately.
    AddAdminMessage(Sent ? TEXT("Sent: ") + Text : TEXT("Not sent: reconnect and finish zone loading first."));
    return Sent;
}
bool UDaocDeveloperComponent::Authorized() const
{
    const auto *Net = UDaocNetworkSubsystem::Get(this);
    if (Net && Net->IsDeveloperModeAuthorized() && Net->bWorldReady)
        return true;
    // Explicit offline development only; never bypass authorization on a connection.
    return FParse::Param(FCommandLine::Get(), TEXT("DaocOffline")) && FParse::Param(FCommandLine::Get(), TEXT("DaocDeveloper")) && (!Net || Net->ConnectionState == TEXT("Disconnected"));
}
bool UDaocDeveloperComponent::IsFlying() const
{
    return bFlightActive && Authorized();
}
void UDaocDeveloperComponent::ToggleFlight()
{
    if (bFlightActive || bPendingFlight)
    {
        bPendingFlight = false;
        StopFlight();
        return;
    }
    bPendingFlight = true;
    PendingTime = 0;
    if (!Authorized())
    {
        auto *Net = UDaocNetworkSubsystem::Get(this);
        const bool Sent = Net && Net->RequestDeveloperMode(true);
        bPendingFlight = Sent;
        Information = Sent ? TEXT("Requesting server developer permission (account privilege required).") : TEXT("Cannot request flight: connect to a character and finish zone loading first.");
        UE_LOG(LogTemp, Display, TEXT("DAoC flight key received: %s"), *Information);
    }
}
void UDaocDeveloperComponent::ToggleInspector()
{
    if (bInspecting || bPendingInspector)
    {
        bPendingInspector = false;
        bInspecting = false;
        SetHammerVisible(false);
        Information.Empty();
        return;
    }
    bPendingInspector = true;
    PendingTime = 0;
    if (!Authorized())
    {
        auto *Net = UDaocNetworkSubsystem::Get(this);
        const bool Sent = Net && Net->RequestDeveloperMode(true);
        bPendingInspector = Sent;
        Information = Sent ? TEXT("Requesting server developer permission (account privilege required).") : TEXT("Cannot request inspection: connect to a character and finish zone loading first.");
        UE_LOG(LogTemp, Display, TEXT("DAoC hammer key received: %s"), *Information);
    }
}
void UDaocDeveloperComponent::Vertical(float Value)
{
    if (IsFlying())
        if (auto *Pawn = Cast<ADaocPlayerCharacter>(GetOwner()))
            Pawn->AddMovementInput(FVector::UpVector, Value);
}
void UDaocDeveloperComponent::StopFlight()
{
    if (bFlightActive)
        if (auto *Pawn = Cast<ADaocPlayerCharacter>(GetOwner()))
        {
            Pawn->GetCharacterMovement()->StopMovementImmediately();
            Pawn->GetCharacterMovement()->SetMovementMode(UDaocNetworkSubsystem::Get(this) && UDaocNetworkSubsystem::Get(this)->GetCombatState().bDead ? MOVE_None : MOVE_Falling);
        }
    bFlightActive = false;
}
void UDaocDeveloperComponent::SetHammerVisible(bool bVisible)
{
    auto *Pawn = Cast<ADaocPlayerCharacter>(GetOwner());
    if (!Pawn)
        return;
    if (bVisible && !HammerHandle)
    {
        HammerHandle = NewObject<UStaticMeshComponent>(Pawn, NAME_None, RF_Transient);
        HammerHead = NewObject<UStaticMeshComponent>(Pawn, NAME_None, RF_Transient);
        auto *Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        FName Hand = NAME_None;
        for (int32 I = 0; I < Pawn->GetMesh()->GetNumBones(); ++I)
        {
            const FName Bone = Pawn->GetMesh()->GetBoneName(I);
            const FString Name = Bone.ToString().ToLower();
            if (Name.Contains(TEXT("r hand")) || Name.Contains(TEXT("r_hand")) || Name.Contains(TEXT("hand_r")))
            {
                Hand = Bone;
                break;
            }
        }
        HammerHandle->SetStaticMesh(Cube);
        HammerHead->SetStaticMesh(Cube);
        for (auto *Part : {HammerHandle.Get(), HammerHead.Get()})
        {
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetCastShadow(false);
            Pawn->AddInstanceComponent(Part);
        }
        HammerHandle->SetupAttachment(Hand.IsNone() ? Pawn->GetRootComponent() : Pawn->GetMesh(), Hand);
        HammerHead->SetupAttachment(HammerHandle);
        HammerHandle->RegisterComponent();
        HammerHead->RegisterComponent();
        // Root fallback follows capsule size; skeletal attachment inherits imported visual scale.
        const float Unit = Hand.IsNone() ? Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() / 90.f : 1.f;
        HammerHandle->SetRelativeLocation(Hand.IsNone() ? FVector(25, 35, 0) * Unit : FVector::ZeroVector);
        HammerHandle->SetRelativeScale3D(FVector(.04, .04, .4) * Unit);
        HammerHead->SetRelativeLocation(FVector(0, 0, 45));
        HammerHead->SetRelativeScale3D(FVector(5, 2.5, .25));
    }
    if (HammerHandle)
        HammerHandle->SetHiddenInGame(!bVisible, true);
}
void UDaocDeveloperComponent::CopyNif()
{
    if (Authorized() && bInspecting && !HitNif.IsNone())
        FPlatformApplicationMisc::ClipboardCopy(*HitNif.ToString());
}
void UDaocDeveloperComponent::RegenerateZone()
{
    if (!Authorized())
    {
        auto *Net = UDaocNetworkSubsystem::Get(this);
        bPendingRegenerate = Net && Net->RequestDeveloperMode(true);
        PendingTime = 0;
        Information = bPendingRegenerate ? TEXT("Requesting server permission to regenerate the current zone.") : TEXT("Cannot regenerate: connect and finish zone loading first.");
        UE_LOG(LogTemp, Display, TEXT("DAoC regenerate key received: %s"), *Information);
        return;
    }
    for (TActorIterator<ADaocZoneActor> It(GetWorld()); It; ++It)
        It->RebuildZone();
    Information = TEXT("Current zone regenerated. Ctrl+Alt+I: inspect fixtures; Ctrl+Alt+C: copy source NIF.");
}
void UDaocDeveloperComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction *Tick)
{
    Super::TickComponent(Dt, Type, Tick);
    if (bPendingFlight || bPendingInspector || bPendingRegenerate || bPendingAdmin)
    {
        PendingTime += Dt;
        if (Authorized())
        {
            if (bPendingFlight)
                if (auto *Pawn = Cast<ADaocPlayerCharacter>(GetOwner()))
                {
                    Pawn->StopJumping();
                    Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
                    bFlightActive = true;
                }
            if (bPendingInspector)
            {
                bInspecting = true;
                SetHammerVisible(true);
                PickTime = 1;
            }
            const bool Regenerate = bPendingRegenerate;
            const bool OpenAdmin = bPendingAdmin;
            bPendingFlight = bPendingInspector = bPendingRegenerate = bPendingAdmin = false;
            Information = TEXT("Ctrl+Alt+F flight | Space up / Ctrl down | Ctrl+Alt+I hammer | Ctrl+Alt+C copy | Ctrl+Alt+R regenerate");
            UE_LOG(LogTemp, Display, TEXT("DAoC developer permission acknowledged by DOL. Flight=%s Inspector=%s"), bFlightActive ? TEXT("on") : TEXT("off"), bInspecting ? TEXT("on") : TEXT("off"));
            if (Regenerate)
                RegenerateZone();
            if (OpenAdmin)
                OpenAdminPanel();
        }
        else if (PendingTime > 8)
        {
            bPendingFlight = bPendingInspector = bPendingRegenerate = bPendingAdmin = false;
            Information = TEXT("Server did not grant developer mode. Reconnect after privilege changes.");
        }
    }
    if (!Authorized() && AdminPanel.IsValid())
        CloseAdminPanel();
    if (!Authorized() || (UDaocNetworkSubsystem::Get(this) && UDaocNetworkSubsystem::Get(this)->GetCombatState().bDead))
    {
        StopFlight();
        if (bInspecting)
        {
            bInspecting = false;
            SetHammerVisible(false);
        }
        return;
    }
    if (bFlightActive)
        if (auto *Pawn = Cast<ADaocPlayerCharacter>(GetOwner()))
        {
            if (auto *Net = UDaocNetworkSubsystem::Get(this))
                Pawn->GetCharacterMovement()->MaxFlySpeed = FVector::Distance(Net->ServerToWorld(FVector::ZeroVector), Net->ServerToWorld(FVector(FlightSpeed, 0, 0)));
        }
    if (!bInspecting)
        return;
    auto *Pawn = Cast<ADaocPlayerCharacter>(GetOwner());
    auto *PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (!PC)
        return;
    FVector Start;
    FRotator Rotation;
    PC->GetPlayerViewPoint(Start, Rotation);
    float Range = InspectRange;
    if (auto *Net = UDaocNetworkSubsystem::Get(this))
        Range = FVector::Distance(Net->ServerToWorld(FVector::ZeroVector), Net->ServerToWorld(FVector(InspectRange, 0, 0)));
    const FVector End = Start + Rotation.Vector() * Range;
    PickTime += Dt;
    if (PickTime >= .1f)
    {
        PickTime = 0;
        bool Found = false;
        double Distance = DBL_MAX;
        HitNif = NAME_None;
        LaserEnd = End;
        for (TActorIterator<ADaocZoneActor> It(GetWorld()); It; ++It)
        {
            FVector Hit;
            FString Info;
            FName Name;
            if (It->InspectFixture(Start, End, Hit, Info, Name) && FVector::DistSquared(Start, Hit) < Distance)
            {
                Distance = FVector::DistSquared(Start, Hit);
                LaserEnd = Hit;
                Information = Info;
                HitNif = Name;
                Found = true;
            }
        }
        if (!Found)
            Information = TEXT("Debug hammer: aim at a fixture. Picking uses mesh bounds.\nCtrl+Alt+C copies the source NIF for a profile replacement.");
    }
    const FVector BeamStart = HammerHead ? HammerHead->GetComponentLocation() : Start;
    DrawDebugLine(GetWorld(), BeamStart, LaserEnd, FColor::Green, false, 0, 0, .5f);
    DrawDebugPoint(GetWorld(), LaserEnd, 8, FColor::Green, false, 0);
}
void UDaocDeveloperComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseAdminPanel();
    if (auto *Net = UDaocNetworkSubsystem::Get(this))
        Net->OnCombatEvent.RemoveDynamic(this, &UDaocDeveloperComponent::HandleAdminEvent);
    StopFlight();
    if (HammerHead)
        HammerHead->DestroyComponent();
    if (HammerHandle)
        HammerHandle->DestroyComponent();
    Super::EndPlay(Reason);
}
