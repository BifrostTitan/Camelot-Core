#include "DaocNetworkSubsystem.h"
#include "Engine/GameInstance.h"
#include "DaocCombatComponent.h"
#include "DaocCombatAnimationComponent.h"
#include "CamelotLiveCrowd.h"
#include "CamelotCrowdComponent.h"
#include "EngineUtils.h"
#include "DaocPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "DaocEquipmentCatalog.h"
#include "DaocEquipmentComponent.h"
#include "DaocZoneActor.h"
#include "DaocCharacterSizing.h"
#include "CamelotAbilityDrag.h"
#include "CamelotInventoryUI.h"
#include "CamelotPlayerPreview.h"
#include "DaocCombatHUD.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "CamelotUIWidgets.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"
#include "Widgets/Layout/SBox.h"
#endif

// Network bodies own separate animation scheduling groups and attach to the zone host.
template <typename T>
static void GatherNetworkComponents(AActor *Host, TArray<T *> &Out)
{
    Out.Reset();
    Host->GetComponents(Out);
    TArray<AActor *> Attached;
    Host->GetAttachedActors(Attached);
    for (auto *Actor : Attached)
    {
        TArray<T *> Parts;
        Actor->GetComponents(Parts);
        Out.Append(Parts);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocNetworkTest, "DAoC.Network.SocketWorldEntry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDaocNetworkTest::RunTest(const FString &Parameters)
{
    // Isolated hosts can resolve imported fixtures directly without scanning the game project.
    FString FixtureContent;
    const bool Mounted = FParse::Value(FCommandLine::Get(), TEXT("CamelotTestContent="), FixtureContent);
    if (Mounted)
    {
        FixtureContent = FPaths::ConvertRelativePathToFull(FixtureContent);
        FPaths::NormalizeDirectoryName(FixtureContent);
        FixtureContent += TEXT("/");
        FPackageName::RegisterMountPoint(TEXT("/Game/"), FixtureContent);
    }
    ON_SCOPE_EXIT
    {
        if (Mounted)
            FPackageName::UnRegisterMountPoint(TEXT("/Game/"), FixtureContent);
    };
    auto *Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Address = Subsystem->CreateInternetAddr();
    bool Valid = false;
    Address->SetIp(TEXT("127.0.0.1"), Valid);
    Address->SetPort(0);
    FSocket *Listener = Subsystem->CreateSocket(NAME_Stream, TEXT("Camelot mock DOL"), Address->GetProtocolType());
    if (!Listener || !Listener->Bind(*Address) || !Listener->Listen(1))
    {
        if (Listener)
            Subsystem->DestroySocket(Listener);
        AddError(TEXT("Unable to create mock listener"));
        return false;
    }
    Listener->SetNonBlocking(true);
    Listener->GetAddress(*Address);
    auto WorldSettings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &WorldSettings);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto *Actor = World->SpawnActor<ADaocZoneActor>();
    Actor->SetActorLocation(FVector(100, 200, 300));
    Actor->SetActorRotation(FRotator(0, 30, 0));
    auto *SessionInstance = NewObject<UGameInstance>();
    auto *Component = NewObject<UDaocNetworkSubsystem>(SessionInstance);
    Component->SetRenderingContext(World, Actor->GetActorTransform());
    Component->bManageZoneTravel = false;
    const bool RequireLiveCrowd = FParse::Param(FCommandLine::Get(), TEXT("CamelotRequireLiveCrowd"));
#if WITH_EDITOR
    if (RequireLiveCrowd)
    {
        auto *Prepared = LoadObject<UCamelotLiveCrowdCatalog>(nullptr, TEXT("/Game/DAoC/Crowd/DA_LiveCrowdCatalog.DA_LiveCrowdCatalog"));
        if (Prepared)
            for (int32 Model : {9, 14, 34, 35, 47, 479})
                if (const auto *Entry = Prepared->Models.Find(Model))
                    for (const auto &Part : Entry->MeshProviders)
                        Part.Value.LoadSynchronous();
        FAssetCompilingManager::Get().FinishAllCompilation();
    }
#endif
    Component->bUseLiveCrowd = RequireLiveCrowd;
    Component->ClientVersion = 68; // This mock deliberately covers the legacy layout.
    // Cold editor asset/provider loading blocks this synchronous mock's sender;
    // it is not a real server heartbeat failure. Keep production timing unchanged.
    Component->RequestTimeoutSeconds = 120;
    Component->ServerPort = Address->GetPort();
    Component->ServerZoneOrigin = FVector2D(548864, 483328);
    Component->SetCredentials(TEXT("account"), TEXT("secret"), TEXT("tester"));
    Component->StartForZone(0, 100.f);
    Component->Connect();
    FSocket *Peer = nullptr;
    TArray<uint8> Input, Reply;
    int32 ReplyOffset = 0;
    int32 Stage = 0;
    const uint8 Expected[] = {0xf4, 0xa7, 0xfc, 0x10, 0xbf, 0xd4, 0xe8};
    const auto Queue = [&](uint8 Op, const TArray<uint8> &Body) {
        Reply.Add(uint8(Body.Num() >> 8));
        Reply.Add(uint8(Body.Num()));
        Reply.Add(Op);
        Reply.Append(Body);
    };
    const double Deadline = FPlatformTime::Seconds() + 5;
    while (FPlatformTime::Seconds() < Deadline && Component->ConnectionState != TEXT("Failed"))
    {
        Component->Tick(0.001f);
        if (!Peer)
        {
            bool Pending = false;
            if (Listener->HasPendingConnection(Pending) && Pending)
            {
                Peer = Listener->Accept(TEXT("Camelot mock peer"));
                Peer->SetNonBlocking(true);
            }
        }
        if (Peer)
        {
            uint8 Buffer[8192];
            int32 Count = 0;
            while (Peer->Recv(Buffer, sizeof(Buffer), Count) && Count > 0)
                Input.Append(Buffer, Count);
            while (Input.Num() >= 12)
            {
                const int32 Length = ((Input[0] << 8) | Input[1]) + 12;
                if (Input.Num() < Length)
                    break;
                if (Stage >= 7 || Input[9] != Expected[Stage])
                {
                    AddError(TEXT("Unexpected client handshake opcode"));
                    break;
                }
                switch (Stage++)
                {
                case 0:
                    Queue(0x22, {1, 0x32, 1, 68, 0});
                    break;
                case 1:
                    Queue(0x2a, {1, 1, 68, 0, 7, 'a', 'c', 'c', 'o', 'u', 'n', 't', 4, 'T', 'e', 's', 't', 12, 0, 0});
                    break;
                case 2: {
                    TArray<uint8> Characters;
                    Characters.SetNumZeroed(24 + 8 * 184 + 104);
                    const uint8 Name[] = {'T', 'e', 's', 't', 'e', 'r'};
                    FMemory::Memcpy(Characters.GetData() + 24, Name, sizeof(Name));
                    Queue(0xfd, Characters);
                }
                break;
                case 3:
                    Queue(0x28, {42, 0});
                    break;
                case 4:
                    Queue(0x2d, {0});
                    break;
                case 5:
                    Queue(0x20, {0, 7, 1, 244, 0, 8, 0x64, 0x70, 0, 7, 0x7a, 0x10, 4, 0, 0, 0});
                    break; // (550000,490000,500)
                case 6:
                    Queue(0x2b, {0});
                    break;
                }
                Input.RemoveAt(0, Length, EAllowShrinking::No);
            }
            if (ReplyOffset < Reply.Num())
            {
                int32 Sent = 0;
                Peer->Send(Reply.GetData() + ReplyOffset, FMath::Min(7, Reply.Num() - ReplyOffset), Sent);
                ReplyOffset += Sent;
                if (ReplyOffset == Reply.Num())
                {
                    Reply.Reset();
                    ReplyOffset = 0;
                }
            }
        }
        if (Component->ConnectionState == TEXT("Playing"))
            break;
        FPlatformProcess::Sleep(0.001f);
    }
    TestEqual(TEXT("Handshake reached Playing"), Component->ConnectionState, FString(TEXT("Playing")));
    TestEqual(TEXT("Server assigned object ID"), Component->PlayerObjectId, 7);
    TestEqual(TEXT("One dummy player"), Component->ReplicatedPlayerCount, 1);
    if (Peer && Component->ConnectionState == TEXT("Playing"))
    {
        // Catacombs sends ten bytes for spell effects; malformed neighboring payloads
        // must not disconnect or discard the following authoritative resource update.
        const TArray<uint8> CombatFrames = {0, 10, 0x1b, 0, 7, 0, 0, 0, 7, 0, 0, 1, 1, 0, 1, 0x1b, 0, 0, 8, 0xad, 90, 70, 0, 0, 0, 60, 80, 0};
        int32 CombatSent = 0;
        TestTrue(TEXT("Spell effect and malformed-payload fixture sent"), Peer->Send(CombatFrames.GetData(), CombatFrames.Num(), CombatSent) && CombatSent == CombatFrames.Num());
        const double CombatDeadline = FPlatformTime::Seconds() + 2;
        do
        {
            Component->Tick(.001f);
            FPlatformProcess::Sleep(.001f);
        } while (Component->GetCombatState().HealthPercent != 90 && Component->ConnectionState == TEXT("Playing") && FPlatformTime::Seconds() < CombatDeadline);
        TestEqual(TEXT("Recoverable spell errors preserve the active session"), Component->ConnectionState, FString(TEXT("Playing")));
        TestEqual(TEXT("Resource update after malformed spell is consumed"), Component->GetCombatState().HealthPercent, 90);
        TestTrue(TEXT("Recoverable diagnostics do not set the login failure text"), Component->LastNetworkError.IsEmpty());
        TestEqual(TEXT("Spell errors preserve the replicated player"), Component->ReplicatedPlayerCount, 1);
    }
    TestTrue(TEXT("Absolute server coordinates"), Component->ServerPlayerPosition.Equals(FVector(550000, 490000, 500), 0.01));
    const FVector ExpectedLocal(1136 * 255.f / 65536.f * 100, 6672 * 255.f / 65536.f * 100, 500 * 255.f / 65536.f * 100);
    const FVector WorldPosition = Component->ServerToWorld(FVector(550000, 490000, 500));
    TestTrue(TEXT("Matches terrain scale and transformed zone origin"), WorldPosition.Equals(Actor->GetActorTransform().TransformPosition(ExpectedLocal), 0.01));
    TestTrue(TEXT("Coordinate conversion round trip"), Component->WorldToServer(WorldPosition).Equals(FVector(550000, 490000, 500), 0.01));
    TArray<UStaticMeshComponent *> DummyMeshes;
    Component->GetRenderHost()->GetComponents<UStaticMeshComponent>(DummyMeshes);
    TestEqual(TEXT("Cylinder component is registered"), DummyMeshes.Num(), 1);
    if (DummyMeshes.Num() == 1)
    {
        TestTrue(TEXT("Cylinder uses server feet position and center offset"), DummyMeshes[0]->GetRelativeLocation().Equals(ExpectedLocal + FVector(0, 0, 90), 0.01));
        TestTrue(TEXT("Cylinder follows server heading"), FMath::IsNearlyEqual(DummyMeshes[0]->GetRelativeRotation().Yaw, 180., 0.01));
    }
    TestTrue(TEXT("Outbound test movement accepted"), Component->SendPlayerPosition(FVector(550010, 490020, 501), 2048, 0));
    TestFalse(TEXT("Movement outside loaded zone rejected"), Component->SendPlayerPosition(FVector(1, 2, 501), 0, 0));
    // Live model changes must replace the marker with a real imported skeletal component.
    if (Peer)
    {
        const uint8 Change[] = {0, 8, 0xdb, 0, 7, 0, 14, 0, 0, 0, 0};
        int32 Sent = 0;
        TestTrue(TEXT("Model-change packet sent"), Peer->Send(Change, sizeof(Change), Sent) && Sent == sizeof(Change));
        TArray<USkeletalMeshComponent *> Characters;
        const double SkinDeadline = FPlatformTime::Seconds() + 2;
        do
        {
            Component->Tick(0.001f);
            Characters.Reset();
            GatherNetworkComponents(Component->GetRenderHost(), Characters);
            FPlatformProcess::Sleep(0.001f);
        } while (Characters.IsEmpty() && FPlatformTime::Seconds() < SkinDeadline);
        TestEqual(TEXT("Server model resolves imported skeletal mesh"), Characters.Num(), 1);
        if (Characters.Num() == 1)
        {
            TestEqual(TEXT("Correct model asset"), Characters[0]->GetSkeletalMeshAsset()->GetName(), FString(TEXT("SK_daoc_14")));
            const FVector MovedLocal = Actor->GetActorTransform().InverseTransformPosition(Component->ServerToWorld(FVector(550010, 490020, 501)));
            TestTrue(TEXT("Skeletal character is placed at feet, with no cylinder center offset"), Characters[0]->GetRelativeLocation().Equals(MovedLocal, 0.01));
            TestEqual(TEXT("Live model appearance exposed to Blueprint"), Component->PlayerAppearances.FindChecked(7).Model, 14);
            Component->LocalPlayerPawn = Actor;
            Component->Tick(0.001f);
            TestTrue(TEXT("Local pawn hides duplicate network body"), Characters[0]->bHiddenInGame);
            Component->LocalPlayerPawn = nullptr;
            Component->Tick(0.001f);
            TestFalse(TEXT("Unpossessed local body remains visible"), Characters[0]->bHiddenInGame);
            FVector NameplateAnchor;
            TestTrue(TEXT("Visible rendered body exposes nameplate anchor"), Component->GetNameplateAnchor(7, NameplateAnchor));
            TestTrue(TEXT("Nameplate anchor is above the rendered body"), NameplateAnchor.Z > Characters[0]->Bounds.Origin.Z + Characters[0]->Bounds.BoxExtent.Z);
            Characters[0]->SetHiddenInGame(true);
            TestFalse(TEXT("Hidden rendered body cannot expose a nameplate"), Component->GetNameplateAnchor(7, NameplateAnchor));
            Characters[0]->SetHiddenInGame(false);
            auto *Instance = Characters[0]->GetSingleNodeInstance();
            TestNotNull(TEXT("Imported idle animation playing"), Instance);
            if (Instance && Instance->GetCurrentAsset())
                TestEqual(TEXT("Idle clip selected"), Instance->GetCurrentAsset()->GetName(), FString(TEXT("A_daoc_14_Idle")));
            for (const auto Speed : {60, 200, 0})
            {
                Component->SendPlayerPosition(FVector(550010, 490020, 501), 2048, Speed);
                Component->Tick(0.001f);
                Instance = Characters[0]->GetSingleNodeInstance();
                const FString ExpectedClip = Speed == 60 ? TEXT("A_daoc_14_Walk") : Speed == 200 ? TEXT("A_daoc_14_Run")
                                                                                                 : TEXT("A_daoc_14_Idle");
                if (Instance && Instance->GetCurrentAsset())
                    TestEqual(TEXT("Movement selects correct clip"), Instance->GetCurrentAsset()->GetName(), ExpectedClip);
                else
                    AddError(TEXT("Missing locomotion animation instance"));
            }
        }
        DummyMeshes.Reset();
        Component->GetRenderHost()->GetComponents(DummyMeshes);
        TestEqual(TEXT("Old marker removed"), DummyMeshes.Num(), 0);
        const uint8 PackedModel[] = {0, 8, 0xdb, 0, 7, 0xb1, 0xdf, 0, 0, 0, 0};
        int32 PackedSent = 0;
        TestTrue(TEXT("Packed Saracen model packet sent"), Peer->Send(PackedModel, sizeof(PackedModel), PackedSent) && PackedSent == sizeof(PackedModel));
        const double PackedDeadline = FPlatformTime::Seconds() + 2;
        do
        {
            Component->Tick(.001f);
            FPlatformProcess::Sleep(.001f);
        } while (Component->PlayerAppearances.FindChecked(7).Model != 479 && FPlatformTime::Seconds() < PackedDeadline);
        TestEqual(TEXT("Server flags do not become mesh ID"), Component->PlayerAppearances.FindChecked(7).Model, 479);
        TestEqual(TEXT("Original packed model retained"), Component->PlayerAppearances.FindChecked(7).RawModelCode, 45535);
        Characters.Reset();
        GatherNetworkComponents(Component->GetRenderHost(), Characters);
        TestTrue(TEXT("Login model resolves visible Saracen mesh"), Characters.Num() == 1 && Characters[0]->GetSkeletalMeshAsset()->GetName() == TEXT("SK_daoc_479"));
        if (Characters.Num() == 1)
            TestTrue(TEXT("Network bodies preserve world scale and model/height sizing"), Characters[0]->GetRelativeScale3D().Equals(FVector(100.f * 255.f / 65536.f / 2.54f * DaocCharacterSizing::AppearanceScale(479, 45535)), .0001));
        const TArray<uint8> NPC = {0, 8, 0, 0, 0, 0, 1, 244, 0, 8, 0x64, 0x70, 0, 7, 0x7a, 0x10, 0, 0, 0, 47, 50, 5, 0, 32, 4, 'W', 'o', 'l', 'f', 3, 'P', 'a', 'c', 0};
        TArray<uint8> Frame = {0, uint8(NPC.Num()), 0xda};
        Frame.Append(NPC);
        TestTrue(TEXT("NPC-create packet sent"), Peer->Send(Frame.GetData(), Frame.Num(), Sent) && Sent == Frame.Num());
        const double NPCDeadline = FPlatformTime::Seconds() + 2;
        do
        {
            Component->Tick(0.001f);
            FPlatformProcess::Sleep(0.001f);
        } while (Component->ReplicatedNPCCount == 0 && FPlatformTime::Seconds() < NPCDeadline);
        TestEqual(TEXT("NPC replicated separately from players"), Component->ReplicatedNPCCount, 1);
        TestEqual(TEXT("Size 100 NPC is twice normal size"), DaocCharacterSizing::PacketSizeScale(100, true), 2.f);
        TestEqual(TEXT("Size 25 NPC is half normal size"), DaocCharacterSizing::PacketSizeScale(25, true), .5f);
        TestEqual(TEXT("Unspecified NPC size uses normal scale"), DaocCharacterSizing::PacketSizeScale(0, true), 1.f);
        TestEqual(TEXT("Player size baseline remains 100"), DaocCharacterSizing::PacketSizeScale(100, false), 1.f);
        if (RequireLiveCrowd)
            TestTrue(TEXT("Prepared creature NPC uses live GPU presentation"), Component->IsCrowdRendered(8));
        Characters.Reset();
        GatherNetworkComponents(Component->GetRenderHost(), Characters);
        TestEqual(TEXT("Player and NPC both have skeletal components"), Characters.Num(), 2);
        if (Characters.Num() == 2)
        {
            TestTrue(TEXT("Live entities have independent actor animation schedules"), Characters[0]->GetOwner() != Characters[1]->GetOwner());
            TestTrue(TEXT("Live entities have independent URO state"), Characters[0]->AnimUpdateRateParams != Characters[1]->AnimUpdateRateParams);
            TestTrue(TEXT("URO remains enabled for both live presenters"), Characters[0]->bEnableUpdateRateOptimizations && Characters[1]->bEnableUpdateRateOptimizations);
        }
        bool WolfFound = false;
        for (auto *Character : Characters)
            if (Character->GetSkeletalMeshAsset()->GetName() == TEXT("SK_daoc_47"))
            {
                WolfFound = true;
                TestTrue(TEXT("Size 50 NPC retains normal model scale"), Character->GetRelativeScale3D().Equals(FVector(100.f * 255.f / 65536.f / 2.54f * DaocCharacterSizing::AppearanceScale(47, 0)), .0001));
                TestTrue(TEXT("NPC is placed at server feet"), Character->GetRelativeLocation().Equals(ExpectedLocal, 0.01));
                auto *Instance = Character->GetSingleNodeInstance();
                if (Instance && Instance->GetCurrentAsset())
                    TestEqual(TEXT("NPC uses its own idle clip"), Instance->GetCurrentAsset()->GetName(), FString(TEXT("A_daoc_47_Idle")));
                else
                    AddError(TEXT("NPC animation missing"));
            }
        TestTrue(TEXT("Server model 47 resolves wolf"), WolfFound);
        for (const auto &Target : Component->GetCombatTargets())
            if (Target.ObjectId == 8)
                TestEqual(TEXT("Nameplates receive NPC guild labels"), Target.GuildName, FString(TEXT("Pac")));
        USkeletalMeshComponent *Wolf = nullptr;
        for (auto *Character : Characters)
            if (Character->GetSkeletalMeshAsset()->GetName() == TEXT("SK_daoc_47"))
                Wolf = Character;
        if (Wolf)
        {
            TArray<float> Heights;
            Heights.Init(4.f, 32 * 32);
            Actor->SetTerrainData(32, 32, Heights);
            Component->Tick(.001f);
            TestEqual(TEXT("Buried NPC feet follow rendered terrain"), Wolf->GetRelativeLocation().Z, 400.);
            Heights.Init(1.f, 32 * 32);
            Actor->SetTerrainData(32, 32, Heights);
            Component->Tick(.001f);
            TestEqual(TEXT("Floating NPC feet settle onto terrain"), Wolf->GetRelativeLocation().Z, 100.);
            if (RequireLiveCrowd)
                TestTrue(TEXT("Grounding preserves live GPU presentation"), Component->IsCrowdRendered(8));
            const auto SendMob = [&](TArray<uint8> Mob) {
                const int32 Revision = Component->PlayerAppearances.FindChecked(8).Revision;
                TArray<uint8> Packet = {0, uint8(Mob.Num()), 0xda};
                Packet.Append(Mob);
                Peer->Send(Packet.GetData(), Packet.Num(), Sent);
                const double Until = FPlatformTime::Seconds() + 2;
                do
                {
                    Component->Tick(.001f);
                    FPlatformProcess::Sleep(.001f);
                } while (Component->PlayerAppearances.FindChecked(8).Revision == Revision && FPlatformTime::Seconds() < Until);
            };
            for (int32 Y = 0; Y < 32; ++Y)
                for (int32 X = 0; X < 32; ++X)
                    Heights[Y * 32 + X] = 2.f + X * .05f + Y * .02f;
            Actor->SetTerrainData(32, 32, Heights);
            const TArray<uint8> Move = {0, 24, 0xa1, 0, 200, 0, 0, 4, 112, 6, 100, 26, 16, 26, 16, 1, 244, 1, 244, 0, 8, 0, 0, 100, 0, 0, 0};
            Peer->Send(Move.GetData(), Move.Num(), Sent);
            const double MoveUntil = FPlatformTime::Seconds() + 2;
            do
            {
                Component->Tick(.001f);
                FPlatformProcess::Sleep(.001f);
            } while (Wolf->GetRelativeLocation().X <= ExpectedLocal.X + 2 && FPlatformTime::Seconds() < MoveUntil);
            FVector Slope;
            TestTrue(TEXT("NPC interpolates the incoming movement waypoint"), Wolf->GetRelativeLocation().X > ExpectedLocal.X + 2);
            TestTrue(TEXT("Moving NPC feet stay on the rendered slope"), Actor->FindTerrainSurface(Wolf->GetComponentLocation(), Slope) && Wolf->GetComponentLocation().Equals(Slope, .01));
            Heights.Init(1.f, 32 * 32);
            Actor->SetTerrainData(32, 32, Heights);
            auto Airborne = NPC;
            Airborne[22] |= 0x20;
            SendMob(Airborne);
            TestTrue(TEXT("Flying NPC preserves server altitude"), Wolf->GetRelativeLocation().Equals(ExpectedLocal, .01));
            FDaocZoneWaterSurface Water;
            Water.Height = 1000;
            Water.LeftBank = {FVector2D(0, 0), FVector2D(0, 31)};
            Water.RightBank = {FVector2D(31, 0), FVector2D(31, 31)};
            auto *WaterProperty = FindFProperty<FArrayProperty>(ADaocZoneActor::StaticClass(), TEXT("WaterSurfaces"));
            auto &WaterSurfaces = *WaterProperty->ContainerPtrToValuePtr<TArray<FDaocZoneWaterSurface>>(Actor);
            WaterSurfaces.Add(Water);
            SendMob(NPC);
            TestTrue(TEXT("Aquatic NPC keeps its server depth instead of dropping to the river bed"), Wolf->GetRelativeLocation().Equals(ExpectedLocal, .01));
            WaterSurfaces.Reset();
            auto *Bridge = World->SpawnActor<AActor>();
            auto *Floor = NewObject<UBoxComponent>(Bridge);
            Bridge->SetRootComponent(Floor);
            Floor->SetMobility(EComponentMobility::Static);
            Floor->SetBoxExtent(FVector(100, 100, 5));
            Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Floor->SetCollisionObjectType(ECC_WorldStatic);
            Floor->SetCollisionResponseToAllChannels(ECR_Block);
            const FVector BridgeFeet = Actor->GetActorTransform().TransformPosition(FVector(ExpectedLocal.X, ExpectedLocal.Y, 750 * 100.f * 255.f / 65536.f));
            Floor->SetWorldLocation(BridgeFeet - FVector(0, 0, 5));
            Floor->RegisterComponent();
            auto Raised = NPC;
            Raised[6] = uint8(750 >> 8);
            Raised[7] = uint8(750 & 255);
            SendMob(Raised);
            TestTrue(TEXT("NPC keeps an elevated walkable floor instead of sinking to terrain"), Wolf->GetComponentLocation().Equals(BridgeFeet, .1));
            Bridge->Destroy();
            Actor->SetTerrainData(0, 0, {});
        }
        const uint8 Delete[] = {0, 2, 0xa2, 0, 8};
        Peer->Send(Delete, sizeof(Delete), Sent);
        const double DeleteDeadline = FPlatformTime::Seconds() + 2;
        do
        {
            Component->Tick(0.001f);
            FPlatformProcess::Sleep(0.001f);
        } while (Component->ReplicatedNPCCount && FPlatformTime::Seconds() < DeleteDeadline);
        TestEqual(TEXT("NPC deletion removes render component"), Component->ReplicatedNPCCount, 0);
        // Exercise a mass-create burst using the command's two model families.
        TArray<uint8> Burst;
        for (int32 Id = 1000; Id < 1200; ++Id)
        {
            auto Mob = NPC;
            Mob[0] = uint8(Id >> 8);
            Mob[1] = uint8(Id);
            Mob[19] = uint8(34 + Id % 2);
            Mob[20] = 50;
            Burst.Append({0, uint8(Mob.Num()), 0xda});
            Burst.Append(Mob);
        }
        TestTrue(TEXT("200-mob create burst sent"), Peer->Send(Burst.GetData(), Burst.Num(), Sent) && Sent == Burst.Num());
        const double BurstDeadline = FPlatformTime::Seconds() + 10;
        do
        {
            Component->Tick(.001f);
            FPlatformProcess::Sleep(.001f);
        } while (Component->ReplicatedNPCCount != 200 && FPlatformTime::Seconds() < BurstDeadline);
        TestEqual(TEXT("All 200 mobs are replicated"), Component->ReplicatedNPCCount, 200);
        GatherNetworkComponents(Component->GetRenderHost(), Characters);
        TestEqual(TEXT("200 mobs plus player have skeletal presenters"), Characters.Num(), 201);
        TSet<const FAnimUpdateRateParameters *> Schedules;
        for (auto *Character : Characters)
            Schedules.Add(Character->AnimUpdateRateParams);
        TestEqual(TEXT("Mass-create does not synchronize URO groups"), Schedules.Num(), 201);
        if (RequireLiveCrowd)
        {
            TestEqual(TEXT("Live network routes all 200 supported mobs into GPU batches"), Component->LiveCrowdCharacterCount, 200);
            int32 ActiveBatches = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
                if (auto *Batch = It->FindComponentByClass<UCamelotCrowdComponent>())
                    if (Batch->GetCharacterCount() > 0)
                        ++ActiveBatches;
            // The deleted creature's empty batch may remain warm for two seconds.
            TestEqual(TEXT("Two mob appearances share two active GPU batches"), ActiveBatches, 2);
            TestTrue(TEXT("Shared mob pose count is bounded by cohorts"), Component->LiveCrowdPoseCount <= 8);
            TestFalse(TEXT("Local player is never crowd rendered"), Component->IsCrowdRendered(7));
            FVector Anchor;
            TestTrue(TEXT("Crowd-rendered mob keeps its nameplate anchor"), Component->GetNameplateAnchor(1000, Anchor));
            TestTrue(TEXT("Selected crowd mob can be targeted"), Component->SelectCombatTarget(1000));
            Component->Tick(.001f);
            TestFalse(TEXT("Selected mob promotes to full presentation"), Component->IsCrowdRendered(1000));
            TestEqual(TEXT("Target promotion leaves the rest of the crowd instanced"), Component->LiveCrowdCharacterCount, 199);
            const auto AdvanceFullBlends = [&]() {
                for (int32 FrameIndex = 0; FrameIndex < 16; ++FrameIndex)
                {
                    World->TimeSeconds += .015;
                    ++GFrameCounter;
                    for (TActorIterator<AActor> It(World); It; ++It)
                        if (auto *Body = Cast<USkeletalMeshComponent>(It->GetRootComponent()))
                            if (!Body->bHiddenInGame)
                            {
                                Body->TickAnimation(.015f, false);
                                Body->RefreshBoneTransforms();
                            }
                    Component->Tick(.015f);
                }
            };
            TestTrue(TEXT("Clearing target succeeds"), Component->SelectCombatTarget(0));
            Component->Tick(.001f);
            AdvanceFullBlends();
            TestTrue(TEXT("Unselected mob returns to the shared renderer"), Component->IsCrowdRendered(1000));
            Component->MobRenderDistance = 1;
            Component->Tick(.001f);
            TestEqual(TEXT("Distance culling removes GPU mobs"), Component->LiveCrowdCharacterCount, 0);
            TestFalse(TEXT("Culled GPU mob has no visible nameplate"), Component->GetNameplateAnchor(1000, Anchor));
            Component->MobRenderDistance = 10000;
            Component->Tick(.001f);
            AdvanceFullBlends();
            TestEqual(TEXT("Re-entering range restores crowd rendering"), Component->LiveCrowdCharacterCount, 200);
            Component->bUseLiveCrowd = false;
            Component->Tick(.001f);
            TestEqual(TEXT("Disabling live crowd restores ordinary presenters"), Component->LiveCrowdCharacterCount, 0);
            Component->bUseLiveCrowd = true;
            Component->Tick(.001f);
            AdvanceFullBlends();
            TestEqual(TEXT("Re-enabling live crowd batches mobs again"), Component->LiveCrowdCharacterCount, 200);
            Peer->SetNoDelay(true);
            const auto SendLive = [&](uint8 Op, const TArray<uint8> &Data) {
                TArray<uint8> Packet{uint8(Data.Num() >> 8), uint8(Data.Num()), Op};
                Packet.Append(Data);
                TestTrue(TEXT("Live fixture packet sent"), Peer->Send(Packet.GetData(), Packet.Num(), Sent) && Sent == Packet.Num());
                // Allow the nonblocking socket pump to receive this frame.
                for (int32 K = 0; K < 20; ++K)
                {
                    World->TimeSeconds += .015;
                    ++GFrameCounter;
                    Component->Tick(.015f);
                    for (TActorIterator<AActor> It(World); It; ++It)
                    {
                        if (auto *Animation = It->FindComponentByClass<UDaocCombatAnimationComponent>())
                            Animation->TickComponent(.015f, LEVELTICK_All, nullptr);
                        if (auto *Batch = It->FindComponentByClass<UCamelotCrowdComponent>())
                            Batch->TickComponent(.015f, LEVELTICK_All, nullptr);
                        if (auto *Body = Cast<USkeletalMeshComponent>(It->GetRootComponent()))
                            if (!Body->bHiddenInGame)
                            {
                                Body->TickAnimation(.015f, false);
                                Body->RefreshBoneTransforms();
                            }
                    }
                    FPlatformProcess::Sleep(.001f);
                }
            };
            const auto RemoteBatch = [&]() -> UCamelotCrowdComponent * {
                for (TActorIterator<AActor> It(World); It; ++It)
                    if (auto *Batch = It->FindComponentByClass<UCamelotCrowdComponent>())
                        if (Batch->GetCharacterTrack(15000) != INDEX_NONE)
                            return Batch;
                return nullptr;
            };
            TArray<uint8> Remote;
            Remote.SetNumZeroed(29);
            Remote[1] = 43;
            Remote[2] = 15000 >> 8;
            Remote[3] = uint8(15000 & 255);
            Remote[4] = 4;
            Remote[5] = 112;
            Remote[6] = 26;
            Remote[7] = 16;
            Remote[10] = 1;
            Remote[11] = 244;
            Remote[15] = 14;
            Remote[18] = 1;
            Remote[19] = 50;
            Remote[22] = 6;
            FMemory::Memcpy(Remote.GetData() + 23, "Remote", 6);
            SendLive(0xd4, Remote);
            TestTrue(TEXT("Remote player is routed into GPU rendering"), Component->IsCrowdRendered(15000));
            SendLive(0xa9, {0, 43, 0, 200, 1, 244, 4, 176, 26, 16, 0, 0, 4, 0});
            if (auto *Batch = RemoteBatch())
            {
                UAnimSequence *Clip = nullptr;
                float Position = 0, Rate = 1;
                bool Loop = false;
                TestTrue(TEXT("Remote movement blends into the shared run animation"), Batch->GetCharacterPlayback(15000, Clip, Position, Loop, Rate) && Clip && Clip->GetName().EndsWith(TEXT("_Run")) && Loop);
            }
            else
                AddError(TEXT("Remote player GPU batch missing"));
            SendLive(0xa9, {0, 43, 0, 0, 1, 244, 4, 176, 26, 16, 0, 0, 4, 0, 0, 0, 0, 228});
            if (auto *ReadyBatch = RemoteBatch())
            {
                UAnimSequence *ReadyClip = nullptr;
                float Position = 0, Rate = 1;
                bool Loop = false;
                ReadyBatch->GetCharacterPlayback(15000, ReadyClip, Position, Loop, Rate);
                auto *Catalog = LoadObject<UDaocCombatAnimationCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_CombatAnimations.DA_CombatAnimations"));
                auto *ExpectedReady = Catalog ? Catalog->Models.FindChecked(14).Actions.FindChecked(TEXT("C_4")).LoadSynchronous() : nullptr;
                TestTrue(TEXT("Remote player attack bit selects shared combat idle"), ReadyBatch->GetCharacterPlayback(15000, ReadyClip, Position, Loop, Rate) && ExpectedReady && ReadyClip == ExpectedReady && Loop);
                TestTrue(TEXT("Remote combat idle uses its reserved cohort"), ReadyBatch->GetCharacterTrack(15000) >= 12 && ReadyBatch->GetCharacterTrack(15000) < 16);
            }
            SendLive(0xba, {0, 43, 4, 0, 0, 128, 0, 0, 100, 0, 100, 100});
            if (auto *IdleBatch = RemoteBatch())
            {
                UAnimSequence *IdleClip = nullptr;
                float Position = 0, Rate = 1;
                bool Loop = false;
                TestTrue(TEXT("Remote heading attack stop returns to ordinary idle despite torch flag"), IdleBatch->GetCharacterPlayback(15000, IdleClip, Position, Loop, Rate) && IdleClip && IdleClip->GetName().EndsWith(TEXT("_Idle")) && Loop);
            }
            Component->EquipmentCatalog = LoadObject<UDaocEquipmentCatalog>(nullptr, TEXT("/Game/DAoC/Equipment/DA_EquipmentCatalog.DA_EquipmentCatalog"));
            SendLive(0x15, {uint8(15000 >> 8), uint8(15000 & 255), 0, 0xf0, 3, 25, 0, 58, 26, 0, 96, 10, 0, 4});
            auto *Batch = RemoteBatch();
            TestNotNull(TEXT("Equipped remote player remains GPU rendered"), Batch);
            if (Batch)
            {
                TestTrue(TEXT("Live GPU appearance includes modular armor"), Batch->GetSkinnedInstanceCount() >= 5);
                TestEqual(TEXT("Live GPU appearance includes the held weapon"), Batch->GetWeaponInstanceCount(), 1);
            }
            TestTrue(TEXT("Selecting remote player succeeds"), Component->SelectCombatTarget(15000));
            Component->Tick(.001f);
            TestFalse(TEXT("Selected remote player is promoted"), Component->IsCrowdRendered(15000));
            TestTrue(TEXT("Clearing remote target succeeds"), Component->SelectCombatTarget(0));
            Component->Tick(.001f);
            TestTrue(TEXT("Remote player demotes with equipment restored in the batch"), Component->IsCrowdRendered(15000));
            SendLive(0xbc, {uint8(15000 >> 8), uint8(15000 & 255), 3, 232, 0, 4, 0, 0, 0, 0, 10, 100});
            Batch = RemoteBatch();
            TestNotNull(TEXT("Confirmed remote attack stays in the GPU pool"), Batch);
            if (Batch)
                TestTrue(TEXT("Remote attack uses an independent action track"), Batch->GetCharacterTrack(15000) >= 16);
            SendLive(0x72, {uint8(15000 >> 8), uint8(15000 & 255), 0xff, 0xff, 0, 30, 0, 0});
            Batch = RemoteBatch();
            if (Batch)
            {
                UAnimSequence *Clip = nullptr;
                float Position = 0, Rate = 1;
                bool Loop = false;
                TestTrue(TEXT("Confirmed cast has GPU playback"), Batch->GetCharacterPlayback(15000, Clip, Position, Loop, Rate));
                auto *Catalog = LoadObject<UDaocCombatAnimationCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_CombatAnimations.DA_CombatAnimations"));
                TestTrue(TEXT("GPU cast uses the confirmed casting-start clip"), Catalog && Clip == Catalog->Models.FindChecked(14).Actions.FindChecked(TEXT("G_16")).LoadSynchronous());
            }
            else
                AddError(TEXT("Remote cast GPU batch missing"));
            SendLive(0x73, {uint8(15000 >> 8), uint8(15000 & 255), 0, 1});
            SendLive(0xae, {uint8(15000 >> 8), uint8(15000 & 255), 0, 7, 0, 0, 0, 0});
            Batch = RemoteBatch();
            TestNotNull(TEXT("Confirmed death remains GPU rendered"), Batch);
            if (Batch)
            {
                UAnimSequence *Clip = nullptr;
                float Position = 0, Rate = 1;
                bool Loop = false;
                Batch->GetCharacterPlayback(15000, Clip, Position, Loop, Rate);
                auto *Catalog = LoadObject<UDaocCombatAnimationCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_CombatAnimations.DA_CombatAnimations"));
                TestTrue(TEXT("GPU death uses a clamped death sequence"), Catalog && Clip == Catalog->Models.FindChecked(14).Actions.FindChecked(TEXT("G_9")).LoadSynchronous() && !Loop);
            }
            SendLive(0x89, {uint8(15000 >> 8), uint8(15000 & 255), 0, 0});
            Batch = RemoteBatch();
            TestTrue(TEXT("Revive returns to a locomotion cohort"), Batch && Batch->GetCharacterTrack(15000) < 16);
            SendLive(0xa2, {uint8(15000 >> 8), uint8(15000 & 255)});
            TestFalse(TEXT("Remote player removal releases its GPU identity"), Component->IsCrowdRendered(15000));
            Component->EquipmentCatalog = nullptr;
            Component->Tick(.001f);
        }
        Burst.Reset();
        for (int32 Id = 1000; Id < 1200; ++Id)
            Burst.Append({0, 2, 0xa2, uint8(Id >> 8), uint8(Id)});
        TestTrue(TEXT("200-mob removal burst sent"), Peer->Send(Burst.GetData(), Burst.Num(), Sent) && Sent == Burst.Num());
        const double RemovalDeadline = FPlatformTime::Seconds() + 5;
        do
        {
            Component->Tick(.001f);
            FPlatformProcess::Sleep(.001f);
        } while (Component->ReplicatedNPCCount && FPlatformTime::Seconds() < RemovalDeadline);
        GatherNetworkComponents(Component->GetRenderHost(), Characters);
        TestEqual(TEXT("Mass removal leaves only player presentation"), Characters.Num(), 1);
        TArray<AActor *> RemainingHosts;
        Component->GetRenderHost()->GetAttachedActors(RemainingHosts);
        TestEqual(TEXT("Mass removal releases mob render actors"), RemainingHosts.Num(), 1);
        TestEqual(TEXT("Mass removal clears live GPU instances"), Component->LiveCrowdCharacterCount, 0);
        Component->EquipmentCatalog = LoadObject<UDaocEquipmentCatalog>(nullptr, TEXT("/Game/DAoC/Equipment/DA_EquipmentCatalog.DA_EquipmentCatalog"));
        TestNotNull(TEXT("Equipment catalog available to network renderer"), Component->EquipmentCatalog.Get());
        const uint8 Gear[] = {0, 14, 0x15, 0, 7, 0, 0xf0, 3, 25, 0, 58, 26, 0, 96, 10, 0, 4};
        TestTrue(TEXT("Live equipment packet sent"), Peer->Send(Gear, sizeof(Gear), Sent) && Sent == sizeof(Gear));
        const double GearDeadline = FPlatformTime::Seconds() + 2;
        do
        {
            Component->Tick(.001f);
            FPlatformProcess::Sleep(.001f);
        } while (Component->PlayerAppearances.FindChecked(7).Equipment.Num() != 3 && FPlatformTime::Seconds() < GearDeadline);
        TArray<UDaocEquipmentComponent *> Renderers;
        GatherNetworkComponents(Component->GetRenderHost(), Renderers);
        auto *Equipped = Renderers.FindByPredicate([](auto *R) { return R->WeaponCount == 1; });
        TestTrue(TEXT("Replicated character receives armor and held weapon"), Equipped && (*Equipped)->ArmorPartCount >= 4);
        Component->LocalPlayerPawn = Actor;
        Component->Tick(.001f);
        TestTrue(TEXT("Possessed pawn does not keep duplicate network armor"), Equipped && (*Equipped)->ArmorPartCount == 0 && (*Equipped)->WeaponCount == 0);
        Component->LocalPlayerPawn = nullptr;
        Component->Tick(.001f);
        TestTrue(TEXT("Unpossessed network character restores equipment"), Equipped && (*Equipped)->WeaponCount == 1);
        const uint8 EmptyGear[] = {0, 5, 0x15, 0, 7, 0, 0xff, 0};
        Peer->Send(EmptyGear, sizeof(EmptyGear), Sent);
        const double ClearDeadline = FPlatformTime::Seconds() + 2;
        do
        {
            Component->Tick(.001f);
            FPlatformProcess::Sleep(.001f);
        } while (!Component->PlayerAppearances.FindChecked(7).Equipment.IsEmpty() && FPlatformTime::Seconds() < ClearDeadline);
        TestTrue(TEXT("Live unequip removes rendered gear"), Equipped && (*Equipped)->WeaponCount == 0 && (*Equipped)->ArmorPartCount == 0);
    }
    if (Peer)
    {
        const auto SendCombat = [&](uint8 Op, const TArray<uint8> &Body) {
            TArray<uint8> F = {uint8(Body.Num() >> 8), uint8(Body.Num()), Op};
            F.Append(Body);
            int32 Sent = 0;
            TestTrue(TEXT("Combat snapshot socket write"), Peer->Send(F.GetData(), F.Num(), Sent) && Sent == F.Num());
        };
        SendCombat(0xad, {90, 70, 0, 0, 0, 60, 80, 0});
        SendCombat(0x91, {0, 0, 0, 50, 1, 244, 0, 12, 0, 0, 0, 5, 0, 2, 0, 0});
        SendCombat(0x16, {1, 2, 3, 0, 1, 0, 0, 0, 0, 0, 80, 5, 'S', 'w', 'o', 'r', 'd', 0, 1, 0, 0, 0, 0, 81, 6, 'S', 'p', 'r', 'i', 'n', 't'});
        SendCombat(0x16, {2, 2, 2, 0, 0, 0, 0, 4, 'F', 'i', 'r', 'e', 5, 0, 200, 5, 'F', 'l', 'a', 'm', 'e'});
        const auto AddItem = [](TArray<uint8> &Body, uint8 Slot, int32 Model, const ANSICHAR *Name, uint8 Type = 1, uint8 Count = 1) {
            Body.Add(Slot);
            const int32 Start = Body.Num();
            Body.AddZeroed(18);
            Body[Start] = 50;
            Body[Start + 1] = Count;
            Body[Start + 4] = Type;
            Body[Start + 6] = 25;
            Body[Start + 7] = 95;
            Body[Start + 8] = 99;
            Body[Start + 9] = 100;
            Body[Start + 10] = 5;
            Body[Start + 11] = uint8(Model >> 8);
            Body[Start + 12] = uint8(Model);
            Body[Start + 17] = uint8(FCStringAnsi::Strlen(Name));
            Body.Append(reinterpret_cast<const uint8 *>(Name), FCStringAnsi::Strlen(Name));
        };
        TArray<uint8> InventoryGear = {2, 0, 0xf0, 1};
        AddItem(InventoryGear, 25, 58, "Robe");
        AddItem(InventoryGear, 10, 4, "Sword");
        SendCombat(2, InventoryGear);
        TArray<uint8> InventoryBag = {2, 0, 0xf0, 2};
        AddItem(InventoryBag, 40, 4, "Sword");
        AddItem(InventoryBag, 41, 100, "20 Arrows", 43, 20);
        SendCombat(2, InventoryBag);
        const double CombatDeadline = FPlatformTime::Seconds() + 2;
        do
        {
            Component->Tick(.001f);
            FPlatformProcess::Sleep(.001f);
        } while ((Component->GetCombatAbilities().Num() != 3 || Component->GetInventoryState().Items.Num() != 4) && FPlatformTime::Seconds() < CombatDeadline);
        const auto Inventory = Component->GetInventoryState();
        TestTrue(TEXT("Socket inventory exposes equipment and all backpack metadata"), Inventory.bEquipmentKnown && Inventory.bBackpackKnown && Inventory.Items.Num() == 4);
        TestTrue(TEXT("Server vitals exposed"), Component->GetCombatState().bStatusKnown && Component->GetCombatState().HealthPercent == 90);
        TestTrue(TEXT("Server progress exposed"), Component->GetCombatState().bProgressKnown && Component->GetCombatState().ExperiencePercent == 50.f && Component->GetCombatState().SkillPoints == 12 && Component->GetCombatState().RealmPoints == 50);
        TestEqual(TEXT("Server hybrid and list abilities exposed"), Component->GetCombatAbilities().Num(), 3);
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto *Pawn = World->SpawnActor<ADaocPlayerCharacter>(Component->ServerToWorld(FVector(550010, 490020, 501)) + FVector(0, 0, 92), FRotator::ZeroRotator, Spawn);
        if (TestNotNull(TEXT("Combat attaches to playable pawn"), Pawn))
        {
            Component->LocalPlayerPawn = Pawn;
            auto *Combat = Pawn->Combat.Get();
            Combat->SetNetworkClient(Component);
            Combat->bAutoFillHotbar = true; // Explicit test convenience; players assign their own bindings.
            Combat->TickComponent(.001f, LEVELTICK_All, nullptr);
            TestEqual(TEXT("Specialization is excluded from hotbar"), Combat->Hotbar[0].Name, FString(TEXT("Sprint")));
            TestEqual(TEXT("List spell is available on same hotbar"), Combat->Hotbar[1].Name, FString(TEXT("Flame")));
            Combat->SetVisibleHotbarCount(3);
            TestTrue(TEXT("Third bar accepts a learned spell"), Combat->AssignHotbar(23, Combat->Hotbar[1]));
            TestTrue(TEXT("Third bar independently activates its binding"), Combat->ActivateHotbar(23));
            Combat->SetVisibleHotbarCount(1);
            TestFalse(TEXT("Hidden bar cannot activate by shortcut"), Combat->ActivateHotbar(23));
            TestEqual(TEXT("Hiding a bar preserves its assignment"), Combat->Hotbar[23].Name, FString(TEXT("Flame")));
            TestTrue(TEXT("Clearing one binding succeeds"), Combat->ClearHotbar(23));
            TestTrue(TEXT("Cleared binding is empty"), Combat->Hotbar[23].Name.IsEmpty());
            TestEqual(TEXT("Clearing extra bar leaves the default binding intact"), Combat->Hotbar[1].Name, FString(TEXT("Flame")));
            TestFalse(TEXT("Invalid clear slot is rejected"), Combat->ClearHotbar(24));
            auto *PC = World->SpawnActor<APlayerController>();
            PC->Possess(Pawn);
            FActorSpawnParameters HUDSpawn;
            HUDSpawn.Owner = PC;
            auto *HUD = World->SpawnActor<ADaocCombatHUD>(HUDSpawn);
            HUD->PostInitializeComponents();
            auto View = MakeShared<FCamelotInventoryView>();
            View->Session = Component;
            View->State = Inventory;
            auto ItemSlot = SNew(SCamelotInventorySlot).View(View).Slot(42);
            const auto ItemDrag = FCamelotItemDrag::New(Component, 41, Inventory.Revision, TEXT("20 Arrows"));
            TestTrue(TEXT("Item drop sends a classic move request"), ItemSlot->OnDrop(FGeometry(), FDragDropEvent(FPointerEvent(), ItemDrag)).IsEventHandled());
            TestEqual(TEXT("Item drop preserves confirmed source until server replies"), Component->GetInventoryState().Items.Num(), 4);
            const auto StaleItem = FCamelotItemDrag::New(Component, 41, Inventory.Revision - 1, TEXT("20 Arrows"));
            ItemSlot->OnDrop(FGeometry(), FDragDropEvent(FPointerEvent(), StaleItem));
            TestFalse(TEXT("Stale item drops provide feedback"), View->Feedback.IsEmpty());
            FCamelotInventoryView FullBag;
            FullBag.Session = Component;
            FullBag.State = Inventory;
            for (int32 Slot = 42; Slot <= 79; ++Slot)
            {
                FDaocInventoryItem Filler;
                Filler.Slot = Slot;
                Filler.Name = TEXT("Item");
                FullBag.State.Items.Add(Filler);
            }
            TestFalse(TEXT("Full backpack blocks convenience unequip"), FullBag.EquipOrRemove(25));
            FullBag.State.bBackpackKnown = false;
            TestFalse(TEXT("Unknown backpack blocks convenience unequip"), FullBag.EquipOrRemove(25));
            TestTrue(TEXT("Backpack item can request server autoequip"), View->EquipOrRemove(40));
            TestTrue(TEXT("Inventory requests charged item use"), Component->RequestUseItem(40, 1, Inventory.Revision));
            TestTrue(TEXT("Inventory requests full server item details"), Component->RequestInspectItem(40, Inventory.Revision));
            const auto ForeignItem = FCamelotItemDrag::New(NewObject<UDaocNetworkSubsystem>(NewObject<UGameInstance>()), 41, Inventory.Revision, TEXT("20 Arrows"));
            TestFalse(TEXT("Inventory drop rejects another session"), ItemSlot->OnDrop(FGeometry(), FDragDropEvent(FPointerEvent(), ForeignItem)).IsEventHandled());
            auto Preview = SNew(SCamelotPlayerPreview).Session(Component);
            TestNull(TEXT("Closed inventory has no preview render target"), Preview->GetRenderTarget());
            Preview->SetActive(true);
            if (TestNotNull(TEXT("Equipment preview resolves the actual character mesh"), Preview->GetRenderTarget()))
                TestEqual(TEXT("Equipment preview uses a bounded render target"), Preview->GetRenderTarget()->SizeX, 512);
            Preview->SetActive(false);
            TestNull(TEXT("Closing inventory releases preview rendering"), Preview->GetRenderTarget());
#if WITH_EDITOR
            if (!FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
            {
                FString Output = FPaths::ProjectSavedDir() / TEXT("UIReview");
                FParse::Value(FCommandLine::Get(), TEXT("CamelotUIReport="), Output);
                IFileManager::Get().MakeDirectory(*Output, true);
                auto InventoryPanel = SNew(SCamelotInventoryPanel).Session(Component);
                InventoryPanel->SetActive(true);
                FWidgetRenderer Renderer(false);
                auto *Target = Renderer.DrawWidget(SNew(SBox).WidthOverride(700).HeightOverride(650)[SNew(SCamelotPanel).Title(FText::FromString(TEXT("Equipment")))[InventoryPanel]], FVector2D(700, 650));
                FlushRenderingCommands();
                TArray<FColor> Pixels;
                FReadSurfaceDataFlags Flags;
                Flags.SetLinearToGamma(false);
                if (TestNotNull(TEXT("Inventory fixture rendered"), Target))
                {
                    Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags);
                    for (auto &Pixel : Pixels)
                        Pixel = Pixel.ReinterpretAsLinear().ToFColorSRGB();
                    TArray64<uint8> PNG;
                    FImageUtils::PNGCompressImageArray(700, 650, Pixels, PNG);
                    TestTrue(TEXT("Inventory fixture capture saved"), FFileHelper::SaveArrayToFile(PNG, *(Output / TEXT("inventory-populated.png"))));
                }
                InventoryPanel->SetActive(false);
            }
#endif
            Combat->SetVisibleHotbarCount(3);
            auto Zone = SNew(SCamelotHotbarDropZone).Owner(HUD).Slot(17);
            const FGeometry Geometry;
            const FPointerEvent Mouse;
            const auto Drag = FCamelotAbilityDrag::New(Combat->Hotbar[1], Combat, nullptr);
            TestTrue(TEXT("Slate drop snaps the learned spell into the destination slot"), Zone->OnDrop(Geometry, FDragDropEvent(Mouse, Drag)).IsEventHandled());
            TestEqual(TEXT("Dropped icon retains its learned ability identity"), Combat->Hotbar[17].Name, FString(TEXT("Flame")));
            const auto Foreign = FCamelotAbilityDrag::New(Combat->Hotbar[1], NewObject<UDaocCombatComponent>(), nullptr);
            TestFalse(TEXT("Drag from another component is rejected"), Zone->OnDrop(Geometry, FDragDropEvent(Mouse, Foreign)).IsEventHandled());
            Combat->SetVisibleHotbarCount(1);
            TestFalse(TEXT("A hidden hotbar cannot receive a drop"), Zone->OnDrop(Geometry, FDragDropEvent(Mouse, Drag)).IsEventHandled());
            HUD->SetHoveredHotbarSlot(17);
            PC->bShowMouseCursor = false;
            HUD->ClearHoveredBinding();
            TestFalse(TEXT("Clear shortcut cannot remove a binding during cursor-free gameplay"), Combat->Hotbar[17].Name.IsEmpty());
            PC->bShowMouseCursor = true;
            HUD->ClearHoveredBinding();
            TestFalse(TEXT("Clear shortcut ignores a hidden bar"), Combat->Hotbar[17].Name.IsEmpty());
            Combat->SetVisibleHotbarCount(3);
            HUD->ClearHoveredBinding();
            TestTrue(TEXT("Clear shortcut removes the hovered visible binding"), Combat->Hotbar[17].Name.IsEmpty());
            Combat->SetVisibleHotbarCount(1);
            Combat->TargetSelf();
            TestEqual(TEXT("Component targeting updates session"), Combat->GetState().TargetObjectId, 7);
            TestTrue(TEXT("Component skill queues on existing connection"), Combat->ActivateHotbar(0));
            TestTrue(TEXT("Component list spell queues on existing connection"), Combat->ActivateHotbar(1));
            TestTrue(TEXT("Client action pose records feet"), Component->LastSubmittedPosition.Equals(FVector(550010, 490020, 501), 1));
            TestTrue(TEXT("Attack request accepted"), Combat->SetAttack(true));
            TestFalse(TEXT("Attack awaits confirmation"), Combat->GetState().bAttacking);
            TestTrue(TEXT("Ordinary player chat queues on the existing connection"), Component->SendChat(TEXT("/group ready")));
            TestFalse(TEXT("Malformed chat cannot queue an extra command"), Component->SendChat(TEXT("hello\n&debug on")));
            Component->Tick(.001f);
            TArray<uint8> Wire;
            const double WireDeadline = FPlatformTime::Seconds() + 1;
            do
            {
                uint8 Buffer[8192];
                int32 Count = 0;
                while (Peer->Recv(Buffer, sizeof(Buffer), Count) && Count > 0)
                    Wire.Append(Buffer, Count);
                FPlatformProcess::Sleep(.001f);
            } while (Wire.IsEmpty() && FPlatformTime::Seconds() < WireDeadline);
            bool Skill = false, Spell = false, Target = false, Attack = false;
            bool Chat = false, ItemMove = false, ItemUse = false, ItemInspect = false;
            for (int32 Offset = 0; Offset + 12 <= Wire.Num();)
            {
                const int32 Length = ((Wire[Offset] << 8) | Wire[Offset + 1]) + 12;
                if (Offset + Length > Wire.Num())
                    break;
                const uint8 Op = Wire[Offset + 9];
                Skill |= Op == 0xbb;
                Spell |= Op == 0x7d;
                Target |= Op == 0xb0;
                Attack |= Op == 0x74;
                ItemMove |= Op == 0xdd && Length == 20;
                ItemUse |= Op == 0x71 && Length == 16;
                ItemInspect |= Op == 0xd8 && Length == 20;
                if (Op == 0xaf && Length >= 275)
                    Chat |= FMemory::Memcmp(Wire.GetData() + Offset + 18, "&group ready", 12) == 0;
                Offset += Length;
            }
            TestTrue(TEXT("Combat requests reach socket, not a separate simulation"), Skill && Spell && Target && Attack);
            TestTrue(TEXT("Chat uses the classic chat packet layout"), Chat);
            TestTrue(TEXT("Inventory move, use and inspect requests reach the socket"), ItemMove && ItemUse && ItemInspect);
            SendCombat(0xc4, {5, 'S', 'w', 'o', 'r', 'd', 1, 5, 'B', 'o', 'n', 'u', 's', 0});
            const double DetailDeadline = FPlatformTime::Seconds() + 2;
            do
            {
                Component->Tick(.001f);
                FPlatformProcess::Sleep(.001f);
            } while (Component->GetInventoryState().DetailText.IsEmpty() && FPlatformTime::Seconds() < DetailDeadline);
            TestEqual(TEXT("Server item details exposed without invented bonuses"), Component->GetInventoryState().DetailText, FString(TEXT("Bonus\n")));
            SendCombat(0xaf, {0, 0, 0, 0, 4, 0, 0, 0, 'G', 'r', 'o', 'u', 'p', ' ', 'r', 'e', 'a', 'd', 'y', '.', 0});
            const double ChatDeadline = FPlatformTime::Seconds() + 2;
            do
            {
                Component->Tick(.001f);
                FPlatformProcess::Sleep(.001f);
            } while (Component->GetChatMessages().IsEmpty() && FPlatformTime::Seconds() < ChatDeadline);
            const auto Messages = Combat->GetChatMessages();
            TestTrue(TEXT("Server chat reaches the session and gameplay component with its category"), Messages.ContainsByPredicate([](const FDaocChatMessage &M) { return M.Text == TEXT("Group ready.") && M.ChatType == 4; }));
            SendCombat(0x74, {1, 0, 0, 0});
            SendCombat(0xd6, {0, 0, 1, 1, 0, 0, 0, 10});
            const double ConfirmDeadline = FPlatformTime::Seconds() + 2;
            do
            {
                Component->Tick(.001f);
                FPlatformProcess::Sleep(.001f);
            } while (!Combat->GetState().bAttacking && FPlatformTime::Seconds() < ConfirmDeadline);
            Combat->TickComponent(.001f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("Confirmed server attack state"), Combat->GetState().bAttacking);
            TestTrue(TEXT("Server cooldown reaches hotbar"), Combat->Hotbar[0].CooldownRemaining > 0);
            TestFalse(TEXT("Cooldown blocks repeat request"), Combat->ActivateHotbar(0));
            Combat->OnCombatEvent.AddUniqueDynamic(Pawn->CombatAnimations.Get(), &UDaocCombatAnimationComponent::HandleEvent);
            SendCombat(0x72, {0, 7, 0xff, 0xff, 0, 30, 0, 0});
            const double CastDeadline = FPlatformTime::Seconds() + 2;
            do
            {
                Component->Tick(.001f);
                FPlatformProcess::Sleep(.001f);
            } while (Pawn->CombatAnimations->ActiveAction != TEXT("G_16") && FPlatformTime::Seconds() < CastDeadline);
            TestEqual(TEXT("First cast packet initializes and animates the local pawn before its tick"), Pawn->CombatAnimations->ActiveAction, FString(TEXT("G_16")));
            SendCombat(0x73, {0, 7, 0, 1});
            const double InterruptDeadline = FPlatformTime::Seconds() + 2;
            do
            {
                Component->Tick(.001f);
                FPlatformProcess::Sleep(.001f);
            } while (!Pawn->CombatAnimations->ActiveAction.IsEmpty() && FPlatformTime::Seconds() < InterruptDeadline);
            TestTrue(TEXT("Server interrupt releases local cast presentation"), Pawn->CombatAnimations->ActiveAction.IsEmpty());
            // Visibility replies must retain collision owned by the zone actor.
            SendCombat(0xda, {0, 8, 0, 0, 0, 0, 1, 244, 0, 8, 0x64, 0xd4, 0, 7, 0x7a, 0x74, 0, 0, 0, 47, 100, 5, 0, 32, 4, 'W', 'o', 'l', 'f', 0, 0});
            const double TargetDeadline = FPlatformTime::Seconds() + 2;
            do
            {
                Component->Tick(.001f);
                FPlatformProcess::Sleep(.001f);
            } while (Component->ReplicatedNPCCount == 0 && FPlatformTime::Seconds() < TargetDeadline);
            const auto Targets = Component->GetCombatTargets();
            const auto *SourceTarget = Targets.FindByPredicate([](const FDaocCombatTarget &T) { return T.ObjectId == 7; });
            const auto *Destination = Targets.FindByPredicate([](const FDaocCombatTarget &T) { return T.ObjectId == 8; });
            if (SourceTarget && Destination)
            {
                auto *Wall = NewObject<UBoxComponent>(Actor);
                Actor->AddInstanceComponent(Wall);
                Wall->SetBoxExtent(FVector(12));
                Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
                Wall->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
                Wall->RegisterComponent();
                Wall->SetWorldLocation((SourceTarget->Position + Destination->Position) * .5);
                const auto CheckVisibility = [&](uint16 Source, uint16 ExpectedResponse) {
                    SendCombat(0xd0, {uint8(Source >> 8), uint8(Source), 0, 8, 0, 0, 0, 0});
                    TArray<uint8> Incoming;
                    bool Found = false;
                    const double LOSDeadline = FPlatformTime::Seconds() + 2;
                    while (!Found && FPlatformTime::Seconds() < LOSDeadline)
                    {
                        Component->Tick(.001f);
                        uint8 Buffer[8192];
                        int32 Count = 0;
                        while (Peer->Recv(Buffer, sizeof(Buffer), Count) && Count > 0)
                            Incoming.Append(Buffer, Count);
                        while (Incoming.Num() >= 12)
                        {
                            const int32 Length = ((Incoming[0] << 8) | Incoming[1]) + 12;
                            if (Incoming.Num() < Length)
                                break;
                            if (Incoming[9] == 0xd0 && Length >= 20)
                            {
                                TestEqual(TEXT("Visibility request uses collision and known entities"), uint16((Incoming[14] << 8) | Incoming[15]), ExpectedResponse);
                                Found = true;
                            }
                            Incoming.RemoveAt(0, Length, EAllowShrinking::No);
                        }
                        FPlatformProcess::Sleep(.001f);
                    }
                    TestTrue(TEXT("Server receives LOS response"), Found);
                };
                CheckVisibility(7, 0);
                Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                CheckVisibility(7, 0x100);
                CheckVisibility(999, 0);
                Wall->DestroyComponent();
            }
            else
                AddError(TEXT("Visibility test entities were not replicated"));
            Actor->Destroy();
            Component->Tick(.001f);
            TestEqual(TEXT("Destroying zone renderer preserves session"), Component->ConnectionState, FString(TEXT("Playing")));
            TestNotNull(TEXT("Entity host survives renderer removal"), Component->GetRenderHost());
            Component->Disconnect();
            Combat->TickComponent(.001f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("Disconnected pawn discards learned hotbar and vitals"), Combat->Hotbar[0].Name.IsEmpty() && !Combat->GetState().bStatusKnown);
            TestFalse(TEXT("Disconnected pawn cannot cast"), Combat->ActivateHotbar(1));
        }
    }
    Component->Disconnect();
    TestEqual(TEXT("Disconnect clears dummy components"), Component->ReplicatedPlayerCount, 0);
    if (Peer)
    {
        Peer->Close();
        Subsystem->DestroySocket(Peer);
    }
    Listener->Close();
    Subsystem->DestroySocket(Listener);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
