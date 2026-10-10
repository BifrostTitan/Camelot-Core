#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "DaocPlayerCharacter.h"
#include "DaocGameMode.h"
#include "DaocZoneActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "DaocNetworkSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/SkeletalMesh.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocPlayableTest, "DAoC.Player.MovementAndJump", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocPlayableTest::RunTest(const FString &)
{
    auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto *SessionInstance = NewObject<UGameInstance>();
    World->SetGameInstance(SessionInstance);
    SessionInstance->Init();
    SessionInstance->GetSubsystem<UDaocNetworkSubsystem>()->SetRenderingContext(World, FTransform::Identity);
    SessionInstance->GetSubsystem<UDaocNetworkSubsystem>()->bAutoConnect = false;
    SessionInstance->GetSubsystem<UDaocNetworkSubsystem>()->bUseFrontend = false;
    SessionInstance->GetSubsystem<UDaocNetworkSubsystem>()->StartForZone(0, 2.54f * 65536.f / 255.f);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto *Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -25), FRotator::ZeroRotator);
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->SetActorScale3D(FVector(30, 30, .5));
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    auto *Player = World->SpawnActor<ADaocPlayerCharacter>(FVector(0, 0, 95), FRotator::ZeroRotator);
    auto *Controller = World->SpawnActor<APlayerController>();
    // A manually spawned controller has no local player in an automation world.
    // CharacterMovement otherwise treats this as a remote pawn and skips physics.
    Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));
    Controller->Possess(Player);
    TestTrue(TEXT("Movement fixture has a locally controlled pawn"), Player->IsLocallyControlled());
    Player->Restart();
    Controller->SetControlRotation(FRotator::ZeroRotator);
    auto *Input = NewObject<UInputComponent>(Player);
    Player->SetupPlayerInputComponent(Input);
    for (const auto &Pair : TArray<TPair<FKey, FVector>>{{EKeys::W, FVector(1, 0, 0)}, {EKeys::S, FVector(-1, 0, 0)}, {EKeys::D, FVector(0, 1, 0)}, {EKeys::A, FVector(0, -1, 0)}})
    {
        Player->ConsumeMovementInputVector();
        bool Found = false;
        for (const auto &Mapping : UPlayerInput::GetEngineDefinedAxisMappings())
            if (Mapping.Key == Pair.Key)
                for (auto &Binding : Input->AxisBindings)
                    if (Binding.AxisName == Mapping.AxisName)
                    {
                        Binding.AxisDelegate.Execute(Mapping.Scale);
                        Found = true;
                    }
        TestTrue(TEXT("Movement key is bound"), Found);
        TestTrue(TEXT("Movement key uses camera horizontal axes"), Player->ConsumeMovementInputVector().Equals(Pair.Value, .001));
    }
    TestTrue(TEXT("Space press/release bound"), Input->KeyBindings.ContainsByPredicate([](const FInputKeyBinding &B) { return B.Chord.Key == EKeys::SpaceBar && B.KeyEvent == IE_Pressed; }) && Input->KeyBindings.ContainsByPredicate([](const FInputKeyBinding &B) { return B.Chord.Key == EKeys::SpaceBar && B.KeyEvent == IE_Released; }));
    for (const auto &Key : TArray<FKey>{EKeys::Tab, EKeys::T, EKeys::F, EKeys::BackSpace, EKeys::One, EKeys::Eight})
        TestTrue(TEXT("Default combat input bound"), Input->KeyBindings.ContainsByPredicate([&](const FInputKeyBinding &B) { return B.Chord.Key == Key && B.KeyEvent == IE_Pressed; }));
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();
    World->GetWorldSettings()->NotifyMatchStarted();
    const auto Tick = [&](int Frames) {
        for (int I = 0; I < Frames; ++I)
        {
            ++GFrameCounter;
            World->Tick(LEVELTICK_All, 1.f / 60);
        }
    };
    Tick(30);
    TestNotNull(TEXT("Offline character mesh loads"), Player->GetMesh()->GetSkeletalMeshAsset());
    TestTrue(TEXT("Server Saracen model is available"), Player->SetCharacterModel(479));
    auto *Saracen = Player->GetMesh()->GetSkeletalMeshAsset();
    AddExpectedMessage(TEXT("DAoC player model 99999 is not imported"), ELogVerbosity::Warning);
    TestFalse(TEXT("Missing server model reports failure"), Player->SetCharacterModel(99999));
    TestTrue(TEXT("Missing model retains visible character"), Player->GetMesh()->GetSkeletalMeshAsset() == Saracen);
    TestTrue(TEXT("Character is supported by collision floor"), Player->GetCharacterMovement()->IsMovingOnGround());
    auto *Wheel = Input->AxisKeyBindings.FindByPredicate([](const FInputAxisKeyBinding &B) { return B.AxisKey == EKeys::MouseWheelAxis; });
    if (TestNotNull(TEXT("Mouse wheel is bound to camera zoom"), Wheel))
    {
        Wheel->AxisDelegate.Execute(-1);
        TestEqual(TEXT("Scroll down increases camera distance"), Player->CameraDistance, 500.f);
        Tick(60);
        TestTrue(TEXT("Camera boom approaches zoom target"), FMath::IsNearlyEqual(Player->CameraBoom->TargetArmLength, 500.f, .1f));
        Player->SetCharacterModel(14);
        Player->SetCharacterModel(479);
        TestTrue(TEXT("Changing character preserves zoom distance"), FMath::IsNearlyEqual(Player->CameraBoom->TargetArmLength, 500.f, .1f));
        Wheel->AxisDelegate.Execute(1);
        TestEqual(TEXT("Scroll up brings camera closer"), Player->CameraDistance, 400.f);
        Player->ZoomCamera(-1000);
        TestEqual(TEXT("Camera cannot zoom beyond configured maximum"), Player->CameraDistance, Player->MaximumCameraDistance);
        Player->ZoomCamera(1000);
        TestEqual(TEXT("Camera cannot zoom inside configured minimum"), Player->CameraDistance, Player->MinimumCameraDistance);
        TestTrue(TEXT("Spring arm retains obstruction collision"), Player->CameraBoom->bDoCollisionTest);
        Player->ZoomCamera(-2.5f);
        Tick(60);
    }
    AddInfo(FString::Printf(TEXT("Grounded test: position %s mode %d tick %d local %d"), *Player->GetActorLocation().ToString(), int32(Player->GetCharacterMovement()->MovementMode), Player->GetCharacterMovement()->IsComponentTickEnabled(), Player->IsLocallyControlled()));
    const FVector Start = Player->GetActorLocation();
    for (int I = 0; I < 60; ++I)
    {
        Player->AddMovementInput(FVector::ForwardVector, 1);
        Tick(1);
    }
    TestTrue(TEXT("Player actually moves with collision enabled"), Player->GetActorLocation().X > Start.X + 100);
    Player->Jump();
    Tick(10);
    Player->StopJumping();
    TestTrue(TEXT("Jump rises above ground"), Player->GetActorLocation().Z > Start.Z + 20);
    Tick(120);
    TestTrue(TEXT("Jump lands on collision floor"), Player->GetCharacterMovement()->IsMovingOnGround());
    TestTrue(TEXT("Feet return to ground"), FMath::Abs(Player->GetActorLocation().Z - Start.Z) < 5);
    TestEqual(TEXT("DAoC mode spawns character"), GetDefault<ADaocGameMode>()->DefaultPawnClass.Get(), ADaocPlayerCharacter::StaticClass());
    auto *Zone = World->SpawnActor<ADaocZoneActor>(FVector(5000, 5000, 50), FRotator::ZeroRotator);
    Zone->SetTerrainData(4, 4, {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3});
    FVector Surface;
    TestTrue(TEXT("Spawn surface is available without a physics trace"), Zone->FindTerrainSurface(FVector(5050, 5050, -1000), Surface));
    TestTrue(TEXT("Spawn surface includes zone transform"), Surface.Equals(FVector(5050, 5050, 350), .01));
    TestFalse(TEXT("Out-of-zone start is not silently clamped"), Zone->FindTerrainSurface(FVector(-100, -100, 0), Surface));
    TestTrue(TEXT("Below-terrain server spawn is lifted to the rendered surface"), Player->ResolveWorldFeet(FVector(5050, 5050, 100)).Equals(FVector(5050, 5050, 350), .01));
    TestTrue(TEXT("Bridge or airborne server position stays above terrain"), Player->ResolveWorldFeet(FVector(5050, 5050, 700)).Equals(FVector(5050, 5050, 700), .01));
    Player->SetActorLocation(FVector(5150, 5150, 450), false, nullptr, ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->StopMovementImmediately();
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    Tick(90);
    TestTrue(TEXT("Character lands on DAoC procedural terrain"), Player->GetCharacterMovement()->IsMovingOnGround());
    TestTrue(TEXT("Procedural terrain supports feet at correct height"), FMath::Abs(Player->GetActorLocation().Z - 442.15f) < 5);
    SessionInstance->GetSubsystem<UDaocNetworkSubsystem>()->StartForZone(0, 100.f);
    auto *ScaledPlayer = World->SpawnActor<ADaocPlayerCharacter>(FVector(5100, 5100, 450), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Scaled test character spawns clear of terrain"), ScaledPlayer))
    {
        SessionInstance->Shutdown();
        World->BeginTearingDown();
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    SessionInstance->GetSubsystem<UDaocNetworkSubsystem>()->Tick(.001f);
    Controller->Possess(ScaledPlayer);
    ScaledPlayer->Restart();
    TestTrue(TEXT("Zone player can load Saracen"), ScaledPlayer->SetCharacterModel(479));
    const float ExpectedScale = 100.f * 255.f / 65536.f / 2.54f;
    // Classic model 479 has a 1.01 race size multiplier; camera and movement
    // use zone units while the body and collision include that visual size.
    const float ExpectedVisualScale = ExpectedScale * 1.01f;
    TestTrue(TEXT("Character shares the fixture NIF unit conversion"), ScaledPlayer->GetMesh()->GetRelativeScale3D().Equals(FVector(ExpectedVisualScale), .0001));
    TestTrue(TEXT("Collision capsule follows imported character scale"), FMath::IsNearlyEqual(ScaledPlayer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 90 * ExpectedVisualScale, .001f));
    TestTrue(TEXT("Camera distance follows character scale"), FMath::IsNearlyEqual(ScaledPlayer->CameraBoom->TargetArmLength, 400 * ExpectedScale, .001f));
    ScaledPlayer->ZoomCamera(-1);
    Tick(60);
    TestTrue(TEXT("Wheel zoom follows imported zone scale"), FMath::IsNearlyEqual(ScaledPlayer->CameraBoom->TargetArmLength, 500 * ExpectedScale, .01f));
    TestEqual(TEXT("Saracen hair is included"), ScaledPlayer->GetMesh()->GetSkeletalMeshAsset()->GetMaterials().Num(), 8);
    Tick(90);
    TestTrue(TEXT("Scaled player lands on terrain"), ScaledPlayer->GetCharacterMovement()->IsMovingOnGround());
    TestTrue(TEXT("Run speed uses server units in the zone"), FMath::IsNearlyEqual(ScaledPlayer->GetCharacterMovement()->MaxWalkSpeed, 191.f * 100.f * 255.f / 65536.f, .001f));
    const FVector ScaledStart = ScaledPlayer->GetActorLocation();
    for (int I = 0; I < 60; ++I)
    {
        ScaledPlayer->AddMovementInput(FVector::ForwardVector, 1);
        Tick(1);
    }
    TestTrue(TEXT("Scaled player covers normal server run distance"), ScaledPlayer->GetActorLocation().X > ScaledStart.X + 60);
    const float GroundZ = ScaledPlayer->GetActorLocation().Z;
    ScaledPlayer->Jump();
    Tick(20);
    ScaledPlayer->StopJumping();
    TestTrue(TEXT("Scaled jump clears ground without oversized displacement"), ScaledPlayer->GetActorLocation().Z > GroundZ + 5 && ScaledPlayer->GetActorLocation().Z < GroundZ + 25);
    Tick(90);
    TestTrue(TEXT("Scaled jump lands"), ScaledPlayer->GetCharacterMovement()->IsMovingOnGround());
    Zone->SetTerrainData(2, 2, {0, 0, 0, 1});
    TestTrue(TEXT("Spawn uses collision triangle height rather than bilinear saddle height"), Zone->FindTerrainSurface(FVector(5025, 5025, 0), Surface) && Surface.Equals(FVector(5025, 5025, 75), .01));
    SessionInstance->Shutdown();
    World->BeginTearingDown();
    World->EndPlay(EEndPlayReason::Quit);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
