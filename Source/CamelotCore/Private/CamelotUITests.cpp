#if WITH_DEV_AUTOMATION_TESTS
#include "CamelotUIPolicy.h"
#include "CamelotUITheme.h"
#include "DaocCombatComponent.h"
#include "DaocNetworkSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR
#include "CamelotAbilityDrag.h"
#include "CamelotCharacterMenu.h"
#include "CamelotGameHUD.h"
#include "CamelotTrainerWindow.h"
#include "DaocAdminPanel.h"
#include "DaocCombatHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "TextureCompiler.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Views/SListView.h"

#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamelotAbilityIconTest, "Camelot.UI.AbilityIconNamespaces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCamelotAbilityIconTest::RunTest(const FString &)
{
    auto *Theme = NewObject<UCamelotUITheme>();
    auto *Spell = NewObject<UTexture2D>(Theme, TEXT("Spell"));
    auto *Style = NewObject<UTexture2D>(Theme, TEXT("Style"));
    auto *Skill = NewObject<UTexture2D>(Theme, TEXT("Skill"));
    auto *Missing = NewObject<UTexture2D>(Theme, TEXT("Missing"));
    Theme->AbilityIcons.Add(7, Spell);
    Theme->StyleIcons.Add(7, Style);
    Theme->SkillIcons.Add(7, Skill);
    Theme->MissingAbilityIcon = Missing;
    FDaocCombatAbility Ability;
    Ability.Icon = 7;
    Ability.Kind = EDaocAbilityKind::ListSpell;
    TestTrue(TEXT("Spell namespace resolves server ID"), Theme->LoadAbilityIcon(Ability) == Spell);
    Ability.Kind = EDaocAbilityKind::Style;
    TestTrue(TEXT("Equal style ID resolves distinct artwork"), Theme->LoadAbilityIcon(Ability) == Style);
    Ability.Kind = EDaocAbilityKind::Ability;
    TestTrue(TEXT("Equal skill ID resolves distinct artwork"), Theme->LoadAbilityIcon(Ability) == Skill);
    Ability.Icon = 8;
    TestTrue(TEXT("Unavailable artwork uses the host fallback"), Theme->LoadAbilityIcon(Ability) == Missing);
    Theme->AbilityIcons.Add(8, Spell);
    TestTrue(TEXT("Existing shared icon themes remain supported"), Theme->LoadAbilityIcon(Ability) == Spell);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamelotUIPolicyTest, "Camelot.UI.ChatAndNameplatePolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCamelotUIPolicyTest::RunTest(const FString &)
{
    using CamelotUI::ENameColor;
    auto Con = [](int32 Player, int32 Mob) { return CamelotUI::NameColor(true, 0, 1, Mob, Player); };
    TestTrue(TEXT("Same realm overrides NPC difficulty"), CamelotUI::NameColor(true, 1, 1, 80, 10) == ENameColor::Friendly);
    TestTrue(TEXT("Enemy player red regardless of level"), CamelotUI::NameColor(false, 2, 1, 1, 50) == ENameColor::Enemy);
    TestTrue(TEXT("Unknown realm player is neutral"), CamelotUI::NameColor(false, 0, 1, 50, 50) == ENameColor::Unknown);
    TestTrue(TEXT("Missing level does not imply gray"), Con(50, 0) == ENameColor::Unknown);
    TestTrue(TEXT("Same level gold"), Con(10, 10) == ENameColor::Yellow);
    TestTrue(TEXT("Two below level ten blue"), Con(10, 8) == ENameColor::Blue);
    TestTrue(TEXT("Server level ten gray boundary"), Con(10, 6) == ENameColor::Gray);
    TestTrue(TEXT("Above level ten gray boundary blue"), Con(10, 7) == ENameColor::Blue);
    TestTrue(TEXT("Above level twenty gray boundary green"), Con(20, 14) == ENameColor::Green);
    TestTrue(TEXT("Classic level forty yellow range"), Con(40, 36) == ENameColor::Yellow);
    TestTrue(TEXT("Classic level forty blue range"), Con(40, 35) == ENameColor::Blue);
    TestTrue(TEXT("Classic level forty green range"), Con(40, 30) == ENameColor::Green);
    TestTrue(TEXT("XP table level forty cutoff"), Con(40, 25) == ENameColor::Gray);
    TestTrue(TEXT("XP table level fifty cutoff"), Con(50, 35) == ENameColor::Gray);
    TestTrue(TEXT("Level fifty above cutoff remains eligible color"), Con(50, 36) == ENameColor::Green);
    TestTrue(TEXT("Higher starts orange"), Con(50, 51) == ENameColor::Orange);
    TestTrue(TEXT("Orange upper boundary"), Con(50, 56) == ENameColor::Orange);
    TestTrue(TEXT("Red upper difficulty"), Con(50, 57) == ENameColor::Red);
    TestTrue(TEXT("Purple upper difficulty"), Con(50, 63) == ENameColor::Purple);
    TestTrue(TEXT("Far above dark purple"), Con(50, 69) == ENameColor::DarkPurple);
    FString Wire;
    TestTrue(TEXT("Ordinary say text accepted"), CamelotUI::NormalizeChat(TEXT(" hello world "), Wire));
    TestEqual(TEXT("Say text remains ordinary text"), Wire, FString(TEXT("hello world")));
    TestTrue(TEXT("Server slash command accepted"), CamelotUI::NormalizeChat(TEXT("/group ready"), Wire));
    TestEqual(TEXT("Slash command uses existing command prefix"), Wire, FString(TEXT("&group ready")));
    TestTrue(TEXT("Maximum classic wire length accepted"), CamelotUI::NormalizeChat(FString::ChrN(254, TEXT('a')), Wire));
    TestFalse(TEXT("Overlong classic message rejected"), CamelotUI::NormalizeChat(FString::ChrN(255, TEXT('a')), Wire));
    TestFalse(TEXT("Empty command rejected"), CamelotUI::NormalizeChat(TEXT("/"), Wire));
    TestFalse(TEXT("Blank message rejected"), CamelotUI::NormalizeChat(TEXT("   "), Wire));
    TestFalse(TEXT("Embedded newline rejected"), CamelotUI::NormalizeChat(TEXT("hello\n&command"), Wire));
    TestFalse(TEXT("Unsupported encoding rejected without truncation"), CamelotUI::NormalizeChat(TEXT("caf\u00e9"), Wire));
    auto *Instance = NewObject<UGameInstance>();
    auto *Network = NewObject<UDaocNetworkSubsystem>(Instance);
    TestFalse(TEXT("Chat never sends without a ready connection"), Network->SendChat(TEXT("hello")));

    TArray<FDaocCombatTarget> Crowd;
    for (int32 I = 1; I <= 1000; ++I)
    {
        FDaocCombatTarget T;
        T.ObjectId = I;
        T.Name = FString::Printf(TEXT("Entity %d"), I);
        T.Position = FVector(I * 40, 0, 0);
        Crowd.Add(T);
    }
    Crowd[0].bLocal = true;
    Crowd[1].Name.Reset();
    const auto Chosen = CamelotUI::SelectNameplates(Crowd, FVector::ZeroVector, 4000, 48, 90);
    TestEqual(TEXT("Thousand-player crowd stays within label budget"), Chosen.Num(), 48);
    TestEqual(TEXT("Selected entity has first overlap priority"), Crowd[Chosen[0]].ObjectId, 90);
    TestEqual(TEXT("Nearest eligible entity follows target"), Crowd[Chosen[1]].ObjectId, 3);
    for (int32 Index : Chosen)
        TestTrue(TEXT("Nameplates obey distance/local/name filtering"), !Crowd[Index].bLocal && !Crowd[Index].Name.IsEmpty() && Crowd[Index].Position.Size() <= 4000);
    const auto Distant = CamelotUI::SelectNameplates(Crowd, FVector::ZeroVector, 4000, 48, 999);
    TestFalse(TEXT("Selected distant entity does not bypass distance budget"), Distant.Contains(998));
    TestEqual(TEXT("Zero label budget disables labels"), CamelotUI::SelectNameplates(Crowd, FVector::ZeroVector, 4000, 0, 0).Num(), 0);
    TestEqual(TEXT("Zero display range disables labels"), CamelotUI::SelectNameplates(Crowd, FVector::ZeroVector, 0, 48, 0).Num(), 0);

    auto *Combat = NewObject<UDaocCombatComponent>();
    TestEqual(TEXT("Three hotbars have independent storage"), Combat->Hotbar.Num(), 24);
    TestEqual(TEXT("One bar shown initially"), Combat->VisibleHotbarCount, 1);
    Combat->SetVisibleHotbarCount(5);
    TestEqual(TEXT("Visible hotbars capped at three"), Combat->VisibleHotbarCount, 3);
    Combat->SetVisibleHotbarCount(0);
    TestEqual(TEXT("Default hotbar cannot be hidden"), Combat->VisibleHotbarCount, 1);
    Combat->SetNetworkClient(Network);
    auto *Function = Network->FindFunction(TEXT("RecordChatEvent"));
    if (!TestNotNull(TEXT("Combat message handler exists"), Function))
        return false;
    for (int32 I = 0; I < 105; ++I)
    {
        FDaocCombatEvent Event;
        Event.Kind = EDaocCombatEventKind::Message;
        Event.ChatType = I % 5;
        Event.Text = FString::FromInt(I);
        Network->ProcessEvent(Function, &Event);
    }
    TestEqual(TEXT("Chat history remains bounded"), Combat->GetChatMessages().Num(), 100);
    TestEqual(TEXT("Oldest retained line is correct"), Combat->GetChatMessages()[0].Text, FString(TEXT("5")));
    TestEqual(TEXT("Server chat category retained"), Combat->GetChatMessages().Last().ChatType, 4);
    const auto BeforeRepeat = Combat->GetChatMessages().Last().SequenceId;
    const auto BeforeChatRevision = Combat->GetChatRevision();
    FDaocCombatEvent Repeat;
    Repeat.Kind = EDaocCombatEventKind::Message;
    Repeat.Text = TEXT("104");
    Repeat.ChatType = 4;
    Network->ProcessEvent(Function, &Repeat);
    TestTrue(TEXT("Identical consecutive messages retain distinct UI identities"), Combat->GetChatMessages().Last().SequenceId > BeforeRepeat);
    TestTrue(TEXT("Appended message advances the cheap UI revision"), Combat->GetChatRevision() > BeforeChatRevision);
    Combat->SetNetworkClient(nullptr);
    Combat->SetNetworkClient(Network);
    TestEqual(TEXT("Pawn reattachment retains session chat across travel"), Combat->GetChatMessages().Num(), 100);
    Network->Disconnect();
    TestEqual(TEXT("Disconnect clears previous session chat"), Combat->GetChatMessages().Num(), 0);
    TestTrue(TEXT("Clearing history invalidates its UI revision"), Combat->GetChatRevision() > BeforeChatRevision + 1);
    return true;
}
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamelotHUDZoneTravelTest, "Camelot.UI.NameplatesAcrossZoneTravel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCamelotHUDZoneTravelTest::RunTest(const FString &)
{
    auto *Instance = NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone();
    auto *World = Instance->GetWorld();
    auto *Network = Instance->GetSubsystem<UDaocNetworkSubsystem>();
    if (!TestNotNull(TEXT("Travel fixture world exists"), World) || !TestNotNull(TEXT("Travel fixture session exists"), Network))
        return false;
    auto *PC = World->SpawnActor<APlayerController>();
    auto *Pawn = World->SpawnActor<APawn>();
    PC->Possess(Pawn);
    FActorSpawnParameters Spawn;
    Spawn.Owner = PC;
    auto *HUD = World->SpawnActor<ADaocCombatHUD>(Spawn);
    HUD->PostInitializeComponents();
    if (!HUD->DefaultUITheme.IsNull())
    {
        HUD->UITheme = HUD->DefaultUITheme.LoadSynchronous();
        TestNotNull(TEXT("Configured host icon theme loads"), HUD->UITheme.Get());
    }
    HUD->Canvas = NewObject<UCanvas>();
    HUD->Canvas->Init(1280, 720, nullptr, nullptr);
    FDaocCombatTarget Target;
    Target.ObjectId = 123;
    Target.Name = TEXT("Previous zone NPC");
    HUD->ViewTargets.Add(Target);
    HUD->NameplateCandidates.Add(0);
    HUD->PlateVisibility.FindOrAdd(123).bVisible = true;
    HUD->ViewState.TargetObjectId = 123;
    HUD->NameplateTraceCursor = 43;
    HUD->RefreshElapsed = 0;
    HUD->bShowHUD = false;
    HUD->Tick(.01f);
    TestEqual(TEXT("Hidden gameplay clears target snapshot"), HUD->ViewTargets.Num(), 0);
    TestEqual(TEXT("Hidden gameplay also clears indices into that snapshot"), HUD->NameplateCandidates.Num(), 0);
    TestEqual(TEXT("Previous-zone occlusion results cleared"), HUD->PlateVisibility.Num(), 0);
    TestEqual(TEXT("Trace cursor reset for new zone"), HUD->NameplateTraceCursor, 0);
    TestEqual(TEXT("Previous-zone selected target cleared"), HUD->ViewState.TargetObjectId, 0);
    TestTrue(TEXT("First visible frame is due for immediate refresh"), HUD->RefreshElapsed >= .1f);

    Network->bUseFrontend = false;
    Network->bWorldReady = true;
    Network->ConnectionState = TEXT("Playing");
    HUD->bShowHUD = true;
    TestTrue(TEXT("Draw fixture follows visible gameplay path"), HUD->IsGameUIVisible());
    // Reproduce the exact empty-target crash and both invalid-index drawing
    // paths.
    HUD->NameplateCandidates = {0, 4, -1};
    HUD->DrawHUD();
    HUD->ViewTargets.Add(Target);
    HUD->DrawHUD();
    TestTrue(TEXT("Both drawing passes survive empty and shrinking snapshots"), true);
    HUD->Tick(.01f);
    TestEqual(TEXT("New zone without targets replaces stale candidates immediately"), HUD->NameplateCandidates.Num(), 0);
    HUD->DrawHUD();
    Instance->Shutdown();
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamelotUIRenderTest, "Camelot.UI.RenderPanels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCamelotUIRenderTest::RunTest(const FString &)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
    {
        AddWarning(TEXT("RenderPanels requires a real RHI; policy tests can run "
                        "with NullRHI."));
        return true;
    }
    FString Output = FPaths::ProjectSavedDir() / TEXT("UIReview");
    FParse::Value(FCommandLine::Get(), TEXT("CamelotUIReport="), Output);
    IFileManager::Get().MakeDirectory(*Output, true);
    FWidgetRenderer Renderer(false);
    auto Render = [&](const TSharedRef<SWidget> &UI, FVector2D Size, FLinearColor Background, const FString &File) {
        auto Root = SNew(SOverlay) + SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Background)] + SOverlay::Slot()[UI];
        auto *Target = Renderer.DrawWidget(Root, Size);
        TArray<FColor> Pixels;
        if (!Target)
        {
            AddError(TEXT("UI render target was not created"));
            return Pixels;
        }
        FlushRenderingCommands();
        FReadSurfaceDataFlags ReadFlags;
        ReadFlags.SetLinearToGamma(false);
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, ReadFlags);
        // BGRA8 readback does not gamma-encode a linear render target.
        for (auto &Pixel : Pixels)
            Pixel = Pixel.ReinterpretAsLinear().ToFColorSRGB();
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(int32(Size.X), int32(Size.Y), Pixels, PNG);
        TestTrue(TEXT("UI preview saved"), FFileHelper::SaveArrayToFile(PNG, *(Output / File)));
        return Pixels;
    };
    const auto First = Render(CreateDaocAdminPanel(nullptr), FVector2D(1920, 1080), FLinearColor::Red, TEXT("admin.png"));
    const auto Second = Render(CreateDaocAdminPanel(nullptr), FVector2D(1920, 1080), FLinearColor::Green, TEXT("admin-contrast.png"));
    if (First.Num() == 1920 * 1080 && Second.Num() == First.Num())
    {
        const int32 Sample = 540 * 1920 + 960;
        TestTrue(TEXT("Admin content is opaque over contrasting backgrounds"), First[Sample] == Second[Sample]);
    }
    else
        AddError(TEXT("Admin preview did not return the expected image size"));
    Render(SNew(SCamelotGameHUD), FVector2D(1920, 1080), FLinearColor(.11f, .15f, .18f, 1), TEXT("hud-1080.png"));
    const auto Small = Render(SNew(SCamelotGameHUD), FVector2D(1280, 720), FLinearColor(.11f, .15f, .18f, 1), TEXT("hud-720.png"));
    if (Small.Num() == 1280 * 720)
    {
        TestTrue(TEXT("Player panel remains inside the viewport at 720p"), Small[21 * 1280 + 21] != Small[0]);
        TestTrue(TEXT("Chat panel remains inside the viewport at 720p"), Small[600 * 1280 + 21] != Small[0]);
    }
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto *PC = World->SpawnActor<APlayerController>();
    auto *Pawn = World->SpawnActor<APawn>();
    PC->Possess(Pawn);
    auto *Combat = NewObject<UDaocCombatComponent>(Pawn);
    Pawn->AddInstanceComponent(Combat);
    Combat->RegisterComponent();
    Combat->SetVisibleHotbarCount(3);
    FActorSpawnParameters Spawn;
    Spawn.Owner = PC;
    auto *HUD = World->SpawnActor<ADaocCombatHUD>(Spawn);
    HUD->PostInitializeComponents();
    TestTrue(TEXT("Expanded UI resolves the test pawn's combat component"), HUD->GetCombat() == Combat);
    if (!HUD->DefaultUITheme.IsNull())
    {
        HUD->UITheme = HUD->DefaultUITheme.LoadSynchronous();
        TestNotNull(TEXT("Gameplay theme is available to rendered panels"), HUD->UITheme.Get());
        // Artwork is supplied by hosts; generic UI tests must allow themes without icons.
        if (HUD->UITheme && FParse::Param(FCommandLine::Get(), TEXT("CamelotIconReview")))
        {
            FDaocCombatAbility SpellIcon, StyleIcon, SkillIcon;
            SpellIcon.Kind = EDaocAbilityKind::ListSpell;
            SpellIcon.Icon = 5311;
            StyleIcon.Kind = EDaocAbilityKind::Style;
            StyleIcon.Icon = 239;
            SkillIcon.Kind = EDaocAbilityKind::Ability;
            SkillIcon.Icon = 7;
            TArray<UTexture *> Textures;
            for (const auto &Ability : {SpellIcon, StyleIcon, SkillIcon})
                if (auto *Texture = HUD->UITheme->LoadAbilityIcon(Ability))
                {
                    Textures.Add(Texture);
                    TestFalse(TEXT("Slate artwork is not a virtual texture"), Texture->VirtualTextureStreaming != 0);
                }
                else
                    AddError(TEXT("Host catalog artwork did not resolve"));
            FTextureCompilingManager::Get().FinishCompilation(Textures);
            FlushRenderingCommands();
            for (auto *Texture : Textures)
                TestNotNull(TEXT("Icon texture has a rendering resource"), Texture->GetResource());
            auto Icons = SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[SNew(SCamelotAbilityIcon).Ability(SpellIcon).Theme(HUD->UITheme)] + SHorizontalBox::Slot().AutoWidth()[SNew(SCamelotAbilityIcon).Ability(StyleIcon).Theme(HUD->UITheme)] + SHorizontalBox::Slot().AutoWidth()[SNew(SCamelotAbilityIcon).Ability(SkillIcon).Theme(HUD->UITheme)];
            Icons->SlatePrepass(1.f);
            TestTrue(TEXT("Icon row has its expected layout extent"), Icons->GetDesiredSize().Equals(FVector2D(108, 36)));
            const auto IconPixels = Render(Icons, FVector2D(108, 36), FLinearColor(.11f, .15f, .18f, 1), TEXT("ability-icons.png"));
            TSet<uint32> Colors;
            for (const auto &Pixel : IconPixels)
                Colors.Add(Pixel.ToPackedARGB());
            TestTrue(TEXT("Imported ability widgets render artwork rather than solid placeholders"), Colors.Num() > 16);
        }
    }
    TestEqual(TEXT("Expanded UI fixture has three visible bars"), HUD->GetCombat()->VisibleHotbarCount, 3);
    auto Pages = SNew(SCamelotCharacterMenu);
    Pages->SelectPage(1);
    TestEqual(TEXT("Switcher selects Skills"), Pages->GetPage(), 1);
    Pages->SelectPage(2);
    TestEqual(TEXT("Switcher selects Effects"), Pages->GetPage(), 2);
    Pages->SelectPage(-1);
    TestEqual(TEXT("Switcher clamps invalid page"), Pages->GetPage(), 0);
    auto Expanded = SNew(SCamelotGameHUD).Owner(HUD);
    const TCHAR *Files[] = {TEXT("character-720.png"), TEXT("skills-720.png"), TEXT("effects-720.png"), TEXT("equipment-720.png")};
    for (int32 Page = 0; Page < 4; ++Page)
    {
        Expanded->ShowCharacterPage(Page);
        Render(Expanded, FVector2D(1280, 720), FLinearColor(.11f, .15f, .18f, 1), Files[Page]);
    }
    auto *Instance = NewObject<UGameInstance>();
    auto *Network = NewObject<UDaocNetworkSubsystem>(Instance);
    Combat->SetNetworkClient(Network);
    auto *Handler = Network->FindFunction(TEXT("RecordChatEvent"));
    for (int32 I = 0; I < 100; ++I)
    {
        FDaocCombatEvent Event;
        Event.Kind = EDaocCombatEventKind::Message;
        Event.Text = FString::Printf(TEXT("Combat message %d: the attacker strikes the defender for damage."), I);
        Network->ProcessEvent(Handler, &Event);
    }
    Expanded->Refresh();
    Render(Expanded, FVector2D(1280, 720), FLinearColor(.11f, .15f, .18f, 1), TEXT("chat-crowd-720.png"));
    TSharedPtr<SListView<TSharedPtr<FDaocChatMessage>>> ChatView;
    TFunction<void(TSharedRef<SWidget>)> FindList;
    FindList = [&](TSharedRef<SWidget> Widget) {
        if (Widget->GetTypeAsString().Contains(TEXT("SListView")))
        {
            ChatView = StaticCastSharedRef<SListView<TSharedPtr<FDaocChatMessage>>>(Widget);
            return;
        }
        auto *Children = Widget->GetChildren();
        for (int32 I = 0; Children && I < Children->Num(); ++I)
            FindList(Children->GetChildAt(I));
    };
    FindList(Expanded);
    if (TestTrue(TEXT("Chat uses a virtualized list"), ChatView.IsValid()))
    {
        TestEqual(TEXT("Full chat history remains available"), ChatView->GetNumItemsBeingObserved(), 100);
        TFunction<int32(TSharedRef<SWidget>)> CountRows;
        CountRows = [&](TSharedRef<SWidget> Widget) {
            int32 Count = Widget->GetTypeAsString().Contains(TEXT("STableRow")) ? 1 : 0;
            auto *Children = Widget->GetChildren();
            for (int32 I = 0; Children && I < Children->Num(); ++I)
                Count += CountRows(Children->GetChildAt(I));
            return Count;
        };
        const int32 VisibleRows = CountRows(ChatView.ToSharedRef());
        TestTrue(TEXT("Only visible chat lines create text layouts"), VisibleRows > 0 && VisibleRows < 20);
        ChatView->ScrollToTop();
        Render(Expanded, FVector2D(1280, 720), FLinearColor(.11f, .15f, .18f, 1), TEXT("chat-history-720.png"));
        FDaocCombatEvent Event;
        Event.Kind = EDaocCombatEventKind::Message;
        Event.Text = TEXT("Newest combat message");
        Network->ProcessEvent(Handler, &Event);
        Expanded->Refresh();
        Render(Expanded, FVector2D(1280, 720), FLinearColor(.11f, .15f, .18f, 1), TEXT("chat-reader-720.png"));
        TestTrue(TEXT("An append preserves a reader's position in older history"), ChatView->GetScrollOffset() < 1);
        Network->Disconnect();
        Expanded->Refresh();
        TestEqual(TEXT("Disconnect removes prior session rows"), ChatView->GetNumItemsBeingObserved(), 0);
    }
    auto Trainer = SNew(SCamelotTrainerWindow);
    FDaocTrainerState Training;
    Training.bOpen = true;
    Training.bSpecsKnown = true;
    Training.bRealmKnown = true;
    Training.ObjectId = 9;
    Training.Revision = 1;
    Training.Name = TEXT("Master trainer");
    Training.SpecializationPoints = 1000;
    Training.RealmPoints = 10;
    for (int32 I = 0; I < 24; ++I)
    {
        FDaocTrainerLine L;
        L.Index = I;
        L.Name = FString::Printf(TEXT("Specialization %d"), I + 1);
        L.Rank = I == 0 ? 10 : 1;
        L.NextCost = L.Rank + 1;
        L.MaximumRank = 40;
        L.bAvailable = true;
        Training.Specializations.Add(L);
        L.Name = FString::Printf(TEXT("Realm ability %d"), I + 1);
        L.Rank = 1;
        L.NextCost = 3;
        L.MaximumRank = 2;
        if (I == 2)
        {
            L.bAvailable = false;
            L.MaximumRank = 1;
            L.Name = TEXT("[Locked realm ability]");
        }
        Training.RealmAbilities.Add(L);
    }
    Trainer->Refresh(Training);
    Trainer->DesiredRanks.Add(0, 20);
    Render(Trainer, FVector2D(620, 560), FLinearColor(.11f, .15f, .18f, 1), TEXT("trainer-specializations.png"));
    TestTrue(TEXT("Trainer opens from the server snapshot"), Trainer->IsOpen());
    TestEqual(TEXT("Trainer retains all scrollable specialization rows"), Trainer->SpecRows->GetChildren()->Num(), 24);
    Trainer->Pages->SetActiveWidgetIndex(1);
    Trainer->DesiredRanks.Add(100, 2);
    Render(Trainer, FVector2D(620, 560), FLinearColor(.11f, .15f, .18f, 1), TEXT("trainer-realm-abilities.png"));
    Training.bOpen = false;
    ++Training.Revision;
    Trainer->Refresh(Training);
    TestFalse(TEXT("Trainer closes with session state"), Trainer->IsOpen());
    TestTrue(TEXT("Closing clears draft allocations"), Trainer->DesiredRanks.IsEmpty());
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
#endif
