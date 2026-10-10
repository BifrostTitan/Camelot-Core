#include "DaocPlayerCharacter.h"
#include "DaocCharacterSizing.h"
#include "DaocDeveloperComponent.h"
#include "DaocEquipmentComponent.h"
#include "DaocCombatComponent.h"
#include "DaocCombatAnimationComponent.h"
#include "DaocZoneActor.h"
#include "DaocNetworkSubsystem.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "GameFramework/PlayerInput.h"

ADaocPlayerCharacter::ADaocPlayerCharacter()
{
    UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(TEXT("DaocForward"), EKeys::W, 1));
    UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(TEXT("DaocForward"), EKeys::S, -1));
    UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(TEXT("DaocRight"), EKeys::D, 1));
    UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(TEXT("DaocRight"), EKeys::A, -1));
    PrimaryActorTick.bCanEverTick = true;
    CombatAnimations = CreateDefaultSubobject<UDaocCombatAnimationComponent>(TEXT("CombatAnimations"));
    Developer = CreateDefaultSubobject<UDaocDeveloperComponent>(TEXT("DeveloperTools"));
    Combat = CreateDefaultSubobject<UDaocCombatComponent>(TEXT("Combat"));
    EquipmentRenderer = CreateDefaultSubobject<UDaocEquipmentComponent>(TEXT("EquipmentRenderer"));
    GetCapsuleComponent()->InitCapsuleSize(34, 90);
    GetMesh()->SetRelativeLocation(FVector(0, 0, -90));
    GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bUseControllerRotationYaw = false;
    auto *Movement = GetCharacterMovement();
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0, 540, 0);
    Movement->MaxWalkSpeed = 300;
    Movement->JumpZVelocity = 420;
    Movement->AirControl = .25f;
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength = 400;
    CameraBoom->SocketOffset = FVector(0, 0, 55);
    CameraBoom->bUsePawnControlRotation = true;
    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
}
void ADaocPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    Network = UDaocNetworkSubsystem::Get(this);
    if (Network)
        Network->LocalPlayerPawn = this;
    Combat->SetNetworkClient(Network);
    Combat->OnCombatEvent.AddDynamic(CombatAnimations, &UDaocCombatAnimationComponent::HandleEvent);
    ZoomCamera(0);
    SetModel(OfflineModelId);
}
void ADaocPlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Network && Network->LocalPlayerPawn.Get() == this)
        Network->LocalPlayerPawn = nullptr;
    Super::EndPlay(Reason);
}
FVector ADaocPlayerCharacter::ResolveWorldFeet(FVector Feet) const
{
    for (TActorIterator<ADaocZoneActor> It(GetWorld()); It; ++It)
    {
        FVector Surface;
        if (It->FindTerrainSurface(Feet, Surface))
            Feet.Z = FMath::Max(Feet.Z, Surface.Z);
    }
    return Feet;
}
void ADaocPlayerCharacter::SetModel(int32 Id)
{
    if (Id <= 0 || Id == CurrentModel || MissingModels.Contains(Id))
        return;
    USkeletalMesh *CharacterAsset = Network ? Network->ResolveCharacterMesh(Id) : nullptr;
    if (!CharacterAsset)
    {
        const FString Path = FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Id, Id);
        CharacterAsset = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn);
    }
    if (!CharacterAsset)
    {
        MissingModels.Add(Id);
        UE_LOG(LogTemp, Warning, TEXT("DAoC player model %d is not imported; retaining visible model %d"), Id, CurrentModel);
        return;
    }
    CurrentModel = Id;
    Clips.Reset();
    EquipmentRenderer->ClearEquipment();
    GetMesh()->Stop();
    GetMesh()->EmptyOverrideMaterials();
    GetMesh()->SetSkeletalMesh(CharacterAsset);

    if (Network)
    {
        const FVector Scale = Network->GetCharacterScale(CharacterAsset) * Network->GetRenderScale();
        GetMesh()->SetRelativeScale3D(Scale * DaocCharacterSizing::ModelScale(Id));
        GetMesh()->SetRelativeRotation(Network->CharacterMeshRotationOffset);
        WorldCharacterScale = FMath::Max(.001f, FMath::Abs(Scale.Z));
        const float VisualSize = WorldCharacterScale * DaocCharacterSizing::ModelScale(Id);
        GetCapsuleComponent()->SetCapsuleSize(34 * VisualSize, 90 * VisualSize);
        GetMesh()->SetRelativeLocation(FVector(0, 0, -90 * VisualSize - 2));
        auto *Movement = GetCharacterMovement();
        Movement->GravityScale = WorldCharacterScale;
        Movement->MaxStepHeight = 45 * WorldCharacterScale;
        Movement->MaxAcceleration = 2048 * WorldCharacterScale;
        Movement->BrakingDecelerationWalking = 2048 * WorldCharacterScale;
        Movement->PerchRadiusThreshold = 0;
        CameraBoom->TargetArmLength = CameraDistance * WorldCharacterScale;
        CameraBoom->SocketOffset = FVector(0, 0, 55 * VisualSize);
        CameraBoom->ProbeSize = 12 * WorldCharacterScale;
    }
}
bool ADaocPlayerCharacter::SetCharacterModel(int32 ModelId)
{
    SetModel(ModelId);
    return ModelId > 0 && CurrentModel == ModelId;
}
void ADaocPlayerCharacter::SetupPlayerInputComponent(UInputComponent *Input)
{
    Super::SetupPlayerInputComponent(Input);
    Combat->BindDefaultInput(Input);
    Developer->BindInput(Input);
    Input->BindAxis(TEXT("DaocForward"), this, &ADaocPlayerCharacter::Forward);
    Input->BindAxis(TEXT("DaocRight"), this, &ADaocPlayerCharacter::Right);
    Input->BindAxisKey(EKeys::MouseX, this, &ADaocPlayerCharacter::LookYaw);
    Input->BindAxisKey(EKeys::MouseY, this, &ADaocPlayerCharacter::LookPitch);
    Input->BindAxisKey(EKeys::MouseWheelAxis, this, &ADaocPlayerCharacter::ZoomCamera);
    Input->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ACharacter::Jump);
    Input->BindKey(EKeys::SpaceBar, IE_Released, this, &ACharacter::StopJumping);
}
void ADaocPlayerCharacter::Forward(float V)
{
    if (Controller)
        AddMovementInput(FRotationMatrix(Developer->IsFlying() ? Controller->GetControlRotation() : FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X), V);
}
void ADaocPlayerCharacter::Right(float V)
{
    if (Controller)
        AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), V);
}
void ADaocPlayerCharacter::LookYaw(float V)
{
    AddControllerYawInput(V);
}
void ADaocPlayerCharacter::LookPitch(float V)
{
    AddControllerPitchInput(-V);
}
void ADaocPlayerCharacter::ZoomCamera(float WheelDelta)
{
    if (!FMath::IsFinite(WheelDelta))
        return;
    const float Minimum = FMath::Max(1.f, MinimumCameraDistance);
    const float Maximum = FMath::Max(Minimum, MaximumCameraDistance);
    CameraDistance = FMath::Clamp(CameraDistance - WheelDelta * FMath::Max(1.f, CameraZoomStep), Minimum, Maximum);
}
void ADaocPlayerCharacter::UpdateAnimation()
{
    auto *CharacterAsset = GetMesh()->GetSkeletalMeshAsset();
    if (!CharacterAsset)
        return;
    const TCHAR *Action = GetCharacterMovement()->IsFalling() ? TEXT("Jump") : GetVelocity().SizeSquared2D() > 25 ? TEXT("Run")
                                                                                                                  : TEXT("Idle");
    const FString Key = Action;
    if (!Clips.Contains(Key))
    {
        const FString Path = FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_%s.A_daoc_%d_%s"), CurrentModel, Action, CurrentModel, Action);
        Clips.Add(Key, LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn));
    }
    CombatAnimations->SetCharacter(GetMesh(), CurrentModel, Network ? Network->PlayerObjectId : 0);
    int32 Weapon = 0;
    if (Network)
        if (const auto *Appearance = Network->PlayerAppearances.Find(Network->PlayerObjectId))
            for (const auto &Item : Appearance->Equipment)
                if (Item.Slot == 10 + Appearance->RightWeapon)
                    Weapon = Item.Model;
    CombatAnimations->UpdateLocomotion(Clips.FindRef(Key).Get(), Key != TEXT("Jump"), Combat->GetState().bAttacking, Weapon);
}
void ADaocPlayerCharacter::Tick(float Dt)
{
    const bool Waiting = !FParse::Param(FCommandLine::Get(), TEXT("DaocOffline")) && Network && (Network->bUseFrontend || Network->ConnectionState != TEXT("Disconnected")) && (Network->ConnectionState != TEXT("Playing") || !Network->bWorldReady);
    if (Waiting)
    {
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        SetActorHiddenInGame(true);
        SetActorEnableCollision(false);
        JoinedObject = 0;
        return;
    }
    if (IsHidden())
    {
        SetActorHiddenInGame(false);
        SetActorEnableCollision(true);
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
    Super::Tick(Dt);
    CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, CameraDistance * WorldCharacterScale, Dt, CameraZoomSpeed);
    if (Network && Network->ConnectionState == TEXT("Playing") && Network->PlayerObjectId && Network->ServerPlayerPosition.Z > 0 && Network->PlayerAppearances.Contains(Network->PlayerObjectId))
    {
        if (const auto *Appearance = Network->PlayerAppearances.Find(Network->PlayerObjectId))
        {
            SetModel(Appearance->Model);
            if (auto *Body = GetMesh()->GetSkeletalMeshAsset())
            {
                const FVector VisualScale = Network->GetAppearanceScale(Body, *Appearance) * Network->GetRenderScale();
                if (!GetMesh()->GetRelativeScale3D().Equals(VisualScale, .0001f))
                {
                    const float PreviousHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
                    const float Height = 90.f * FMath::Abs(VisualScale.Z);
                    GetMesh()->SetRelativeScale3D(VisualScale);
                    GetCapsuleComponent()->SetCapsuleSize(34.f * FMath::Abs(VisualScale.Z), Height);
                    GetMesh()->SetRelativeLocation(FVector(0, 0, -Height - 2));
                    SetActorLocation(GetActorLocation() + FVector(0, 0, Height - PreviousHeight));
                    CameraBoom->SocketOffset = FVector(0, 0, 55.f * FMath::Abs(VisualScale.Z));
                }
            }
            EquipmentRenderer->ApplyAppearance(GetMesh(), Network->EquipmentCatalog, *Appearance);
        }
        const FVector Server = Network->ServerPlayerPosition;
        const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (JoinedObject != Network->PlayerObjectId || JoinedWorldRevision != Network->WorldRevision)
        {
            JoinedObject = Network->PlayerObjectId;
            JoinedWorldRevision = Network->WorldRevision;
            LastSent = LastObserved = Server;
            GetCharacterMovement()->MaxWalkSpeed = FVector::Distance(Network->ServerToWorld(Server), Network->ServerToWorld(Server + FVector(ServerRunSpeed, 0, 0)));
            // Scale the jump with the body, and cap it below DOL's jump tolerance.
            const float Height = FMath::Min(90.f * WorldCharacterScale, FVector::Distance(Network->ServerToWorld(Server), Network->ServerToWorld(Server + FVector(0, 0, 170))));
            GetCharacterMovement()->JumpZVelocity = FMath::Sqrt(2 * FMath::Abs(GetCharacterMovement()->GetGravityZ()) * Height);
            const FVector SpawnFeet = ResolveWorldFeet(Network->ServerToWorld(Server));
            SetActorLocation(SpawnFeet + FVector(0, 0, HalfHeight + 2), false, nullptr, ETeleportType::TeleportPhysics);
            UE_LOG(LogTemp, Display, TEXT("DAoC player grounded model %d: server feet %s, rendered feet %s, capsule half height %.3f"), CurrentModel, *Server.ToString(), *SpawnFeet.ToString(), HalfHeight);
            GetCharacterMovement()->StopMovementImmediately();
        }
        else if (!Server.Equals(LastObserved, 1) && !Server.Equals(Network->LastSubmittedPosition, 2))
        {
            // A server correction differs from the position echoed by Client.Move.
            SetActorLocation((Developer->IsFlying() ? Network->ServerToWorld(Server) : ResolveWorldFeet(Network->ServerToWorld(Server))) + FVector(0, 0, HalfHeight + 2), false, nullptr, ETeleportType::TeleportPhysics);
            GetCharacterMovement()->StopMovementImmediately();
        }
        LastObserved = Server;
        if (const auto *Appearance = Network->PlayerAppearances.Find(JoinedObject))
            SetModel(Appearance->Model);
        SendElapsed += Dt;
        if (SendElapsed >= .5f)
        {
            SendElapsed = 0;
            const FVector Feet = GetActorLocation() - FVector(0, 0, HalfHeight + 2);
            const FVector Position = Network->WorldToServer(Feet);
            const FVector ServerVelocity = Network->WorldToServer(Feet + GetVelocity()) - Position;
            // DAoC zero faces +Y; positive headings rotate toward -X.
            const FVector Facing = Network->WorldToServer(Feet + GetActorForwardVector()) - Position;
            const int32 Heading = FMath::RoundToInt(FMath::Atan2(-Facing.X, Facing.Y) * 4096 / (2 * PI)) & 4095;
            const int32 Speed = FMath::Clamp(FMath::RoundToInt(ServerVelocity.Size2D()), 0, 511);
            if (Network->SendPlayerPosition(Position, Heading, Speed, Developer->IsFlying()))
                LastSent = FVector(FMath::Floor(Position.X), FMath::Floor(Position.Y), FMath::Floor(Position.Z));
        }
    }
    else
    {
        JoinedObject = 0;
        GetCharacterMovement()->MaxWalkSpeed = Network ? FVector::Distance(Network->ServerToWorld(FVector::ZeroVector), Network->ServerToWorld(FVector(ServerRunSpeed, 0, 0))) : 300;
        GetCharacterMovement()->JumpZVelocity = 420 * WorldCharacterScale;
    }
    const auto CombatState = Combat->GetState();
    if (CombatState.bStatusKnown && CombatState.bDead)
    {
        GetCharacterMovement()->DisableMovement();
        bCombatMovementDisabled = true;
    }
    else if (bCombatMovementDisabled)
    {
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        bCombatMovementDisabled = false;
    }
    UpdateAnimation();
}
