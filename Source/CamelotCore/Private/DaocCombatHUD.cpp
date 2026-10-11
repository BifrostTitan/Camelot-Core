#include "DaocCombatHUD.h"
#include "CamelotGameHUD.h"
#include "CamelotUIPolicy.h"
#include "CamelotUITheme.h"
#include "DaocCombatComponent.h"
#include "DaocDeveloperComponent.h"
#include "DaocNetworkSubsystem.h"
#include "Components/InputComponent.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"

ADaocCombatHUD::ADaocCombatHUD()
{
    PrimaryActorTick.bCanEverTick = true;
}
const UCamelotUITheme *ADaocCombatHUD::GetUITheme() const
{
    return UITheme ? UITheme.Get() : GetDefault<UCamelotUITheme>();
}
UDaocCombatComponent *ADaocCombatHUD::GetCombat() const
{
    return PlayerOwner && PlayerOwner->GetPawn() ? PlayerOwner->GetPawn()->FindComponentByClass<UDaocCombatComponent>() : nullptr;
}
UDaocDeveloperComponent *ADaocCombatHUD::GetDeveloper() const
{
    return PlayerOwner && PlayerOwner->GetPawn() ? PlayerOwner->GetPawn()->FindComponentByClass<UDaocDeveloperComponent>() : nullptr;
}
bool ADaocCombatHUD::IsGameUIVisible() const
{
    if (!bShowHUD || !PlayerOwner || !PlayerOwner->GetPawn())
        return false;
    const auto *Net = UDaocNetworkSubsystem::Get(this);
    return FParse::Param(FCommandLine::Get(), TEXT("DaocOffline")) || (Net && (!Net->bUseFrontend || (Net->bWorldReady && Net->ConnectionState == TEXT("Playing"))));
}
void ADaocCombatHUD::BeginPlay()
{
    Super::BeginPlay();
    if (!UITheme && !DefaultUITheme.IsNull())
    {
        UITheme = DefaultUITheme.LoadSynchronous();
        if (!UITheme)
            UE_LOG(LogTemp, Warning, TEXT("Camelot: could not load configured gameplay UI theme"));
    }
    if (PlayerOwner)
    {
        EnableInput(PlayerOwner);
        InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ADaocCombatHUD::OpenChat);
        InputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &ADaocCombatHUD::ToggleUICursor);
        InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ADaocCombatHUD::ToggleCharacterMenu);
        InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ADaocCombatHUD::InteractWithTarget);
        InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ADaocCombatHUD::CloseTrainer);
        InputComponent->BindKey(FInputChord(EKeys::O, false, true, false, false), IE_Pressed, this, &ADaocCombatHUD::ClearHoveredBinding);
    }
}
void ADaocCombatHUD::Tick(float Delta)
{
    Super::Tick(Delta);
    auto *Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (!GameUI.IsValid() && Viewport && PlayerOwner)
    {
        GameUI = SNew(SCamelotGameHUD).Owner(this);
        Viewport->AddViewportWidgetContent(GameUI.ToSharedRef(), 20);
    }
    const bool Show = IsGameUIVisible();
    if (GameUI.IsValid())
        GameUI->SetVisibility(Show ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
    if (!Show)
    {
        CloseChat();
        if (bTrainerOpen)
            CloseTrainer();
        if (GameUI.IsValid() && GameUI->IsCharacterMenuOpen())
            GameUI->ToggleCharacterMenu();
        // Candidate indices belong to this target snapshot, including across zone travel.
        ViewState = {};
        ViewTargets.Reset();
        NameplateCandidates.Reset();
        PlateVisibility.Reset();
        NameplateTraceCursor = 0;
        RefreshElapsed = 1; // Refresh immediately on the first visible frame.
        return;
    }
    RefreshElapsed += Delta;
    if (RefreshElapsed >= .1f)
    {
        RefreshElapsed = 0;
        if (auto *Combat = GetCombat())
        {
            ViewState = Combat->GetState();
            ViewTargets = Combat->GetTargets();
        }
        else
        {
            ViewState = {};
            ViewTargets.Reset();
        }
        FVector Camera;
        FRotator Rotation;
        PlayerOwner->GetPlayerViewPoint(Camera, Rotation);
        NameplateCandidates = CamelotUI::SelectNameplates(ViewTargets, Camera, NameplateDistance, FMath::Clamp(MaximumNameplates, 0, 128), ViewState.TargetObjectId);
        TSet<int32> IDs;
        for (const auto &Target : ViewTargets)
            IDs.Add(Target.ObjectId);
        for (auto It = PlateVisibility.CreateIterator(); It; ++It)
            if (!IDs.Contains(It.Key()))
                It.RemoveCurrent();
        if (GameUI.IsValid())
            GameUI->Refresh();
    }
}
void ADaocCombatHUD::OpenChat()
{
    if (bChatOpen || !IsGameUIVisible() || !GameUI.IsValid() || (GetDeveloper() && GetDeveloper()->IsAdminPanelOpen()))
        return;
    bChatOpen = true;
    bCursorBeforeChat = PlayerOwner->bShowMouseCursor;
    if (auto *Movement = PlayerOwner->GetPawn()->GetMovementComponent())
        Movement->StopMovementImmediately();
    PlayerOwner->SetIgnoreMoveInput(true);
    PlayerOwner->SetIgnoreLookInput(true);
    PlayerOwner->bShowMouseCursor = true;
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(GameUI->GetChatInput());
    PlayerOwner->SetInputMode(Mode);
    FSlateApplication::Get().SetKeyboardFocus(GameUI->GetChatInput());
}
void ADaocCombatHUD::CloseChat()
{
    if (!bChatOpen)
        return;
    bChatOpen = false;
    if (PlayerOwner)
    {
        PlayerOwner->SetIgnoreMoveInput(false);
        PlayerOwner->SetIgnoreLookInput(false);
        const auto *Net = UDaocNetworkSubsystem::Get(this);
        const bool Gameplay = IsGameUIVisible() || (Net && Net->bWorldReady) || FParse::Param(FCommandLine::Get(), TEXT("DaocOffline"));
        if (Gameplay && !(GetDeveloper() && GetDeveloper()->IsAdminPanelOpen()))
        {
            PlayerOwner->bShowMouseCursor = bCursorBeforeChat || bTrainerOpen || (GameUI.IsValid() && GameUI->IsCharacterMenuOpen());
            if (PlayerOwner->bShowMouseCursor)
                PlayerOwner->SetInputMode(FInputModeGameAndUI());
            else
                PlayerOwner->SetInputMode(FInputModeGameOnly());
            if (FSlateApplication::IsInitialized())
                FSlateApplication::Get().SetAllUserFocusToGameViewport();
        }
    }
}
bool ADaocCombatHUD::SubmitChat(const FString &Text)
{
    auto *Net = UDaocNetworkSubsystem::Get(this);
    return Net && Net->SendChat(Text);
}
void ADaocCombatHUD::ToggleUICursor()
{
    if (!IsGameUIVisible() || bChatOpen || (GetDeveloper() && GetDeveloper()->IsAdminPanelOpen()))
        return;
    PlayerOwner->bShowMouseCursor = !PlayerOwner->bShowMouseCursor;
    if (PlayerOwner->bShowMouseCursor)
        PlayerOwner->SetInputMode(FInputModeGameAndUI());
    else
        PlayerOwner->SetInputMode(FInputModeGameOnly());
}
void ADaocCombatHUD::RetainHotbarIcon(int32 Slot, UTexture2D *Texture)
{
    HotbarTextures.SetNum(24);
    if (HotbarTextures.IsValidIndex(Slot))
        HotbarTextures[Slot] = Texture;
}
void ADaocCombatHUD::ToggleCharacterMenu()
{
    if (!IsGameUIVisible() || bChatOpen || !GameUI.IsValid() || (GetDeveloper() && GetDeveloper()->IsAdminPanelOpen()))
        return;
    if (!GameUI->IsCharacterMenuOpen())
        bCursorBeforeMenu = PlayerOwner->bShowMouseCursor;
    GameUI->ToggleCharacterMenu();
    PlayerOwner->bShowMouseCursor = GameUI->IsCharacterMenuOpen() || bTrainerOpen || bCursorBeforeMenu;
    if (PlayerOwner->bShowMouseCursor)
        PlayerOwner->SetInputMode(FInputModeGameAndUI());
    else
        PlayerOwner->SetInputMode(FInputModeGameOnly());
}
void ADaocCombatHUD::RefreshGameUI()
{
    if (GameUI.IsValid())
        GameUI->Refresh();
}
void ADaocCombatHUD::InteractWithTarget()
{
    if (!IsGameUIVisible() || bChatOpen || (GetDeveloper() && GetDeveloper()->IsAdminPanelOpen()))
        return;
    if (auto *Net = UDaocNetworkSubsystem::Get(this))
        Net->RequestInteract(Net->GetCombatState().TargetObjectId);
}
void ADaocCombatHUD::SetTrainerOpen(bool Open)
{
    if (bTrainerOpen == Open || !PlayerOwner)
        return;
    if (Open)
    {
        CloseChat();
        bCursorBeforeTrainer = PlayerOwner->bShowMouseCursor;
        if (auto *Movement = PlayerOwner->GetPawn() ? PlayerOwner->GetPawn()->GetMovementComponent() : nullptr)
            Movement->StopMovementImmediately();
    }
    bTrainerOpen = Open;
    PlayerOwner->bShowMouseCursor = Open || bChatOpen || (GameUI.IsValid() && GameUI->IsCharacterMenuOpen()) || bCursorBeforeTrainer;
    if (IsGameUIVisible() && !bChatOpen && !(GetDeveloper() && GetDeveloper()->IsAdminPanelOpen()))
    {
        if (PlayerOwner->bShowMouseCursor)
            PlayerOwner->SetInputMode(FInputModeGameAndUI());
        else
            PlayerOwner->SetInputMode(FInputModeGameOnly());
    }
}
void ADaocCombatHUD::CloseTrainer()
{
    if (!bTrainerOpen)
        return;
    if (auto *Net = UDaocNetworkSubsystem::Get(this))
        Net->CloseTrainer();
    SetTrainerOpen(false);
    RefreshGameUI();
}
void ADaocCombatHUD::ClearHoveredBinding()
{
    if (!IsGameUIVisible() || bChatOpen || !PlayerOwner->bShowMouseCursor || (GetDeveloper() && GetDeveloper()->IsAdminPanelOpen()))
        return;
    if (auto *C = GetCombat(); C && HoveredHotbarSlot >= 0 && HoveredHotbarSlot < C->VisibleHotbarCount * 8 && C->ClearHotbar(HoveredHotbarSlot))
        RefreshGameUI();
}
void ADaocCombatHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseChat();
    if (GameUI.IsValid() && GetWorld() && GetWorld()->GetGameViewport())
        GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(GameUI.ToSharedRef());
    GameUI.Reset();
    if (PlayerOwner)
        DisableInput(PlayerOwner);
    Super::EndPlay(Reason);
}
void ADaocCombatHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !IsGameUIVisible())
        return;
    if (const auto *Dev = GetDeveloper(); Dev && Dev->IsInspectorActive())
    {
        DrawLine(Canvas->SizeX * .5f - 6, Canvas->SizeY * .5f, Canvas->SizeX * .5f + 6, Canvas->SizeY * .5f, FLinearColor::Green);
        DrawLine(Canvas->SizeX * .5f, Canvas->SizeY * .5f - 6, Canvas->SizeX * .5f, Canvas->SizeY * .5f + 6, FLinearColor::Green);
    }
    auto *Net = UDaocNetworkSubsystem::Get(this);
    if (!bShowNameplates || !Net || !GetWorld())
        return;
    FVector Camera;
    FRotator Rotation;
    PlayerOwner->GetPlayerViewPoint(Camera, Rotation);
    const auto &Chosen = NameplateCandidates;
    TArray<FBox2D> Occupied;
    const auto *Theme = GetUITheme();
    const auto *LocalAppearance = Net->PlayerAppearances.Find(Net->PlayerObjectId);
    const auto NameFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
    const auto FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    int32 Budget = FMath::Clamp(NameplateTraceBudget, 1, 16);
    const double Now = GetWorld()->GetTimeSeconds();
    // Rotate through candidates so low frame rates cannot starve distant labels.
    for (int32 Checked = 0; Checked < Chosen.Num() && Budget > 0; ++Checked)
    {
        NameplateTraceCursor = (NameplateTraceCursor + 1) % Chosen.Num();
        const int32 Index = Chosen[NameplateTraceCursor];
        if (!ViewTargets.IsValidIndex(Index))
            continue;
        const auto &T = ViewTargets[Index];
        FVector Anchor;
        FVector2D Screen;
        if (!Net->GetNameplateAnchor(T.ObjectId, Anchor) || !PlayerOwner->ProjectWorldLocationToScreen(Anchor, Screen, true) || Screen.X < 0 || Screen.Y < 0 || Screen.X > Canvas->SizeX || Screen.Y > Canvas->SizeY)
            continue;
        auto &Sight = PlateVisibility.FindOrAdd(T.ObjectId);
        if (Sight.CheckedAt >= 0 && Now - Sight.CheckedAt <= .2)
            continue;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(CamelotNameplates), true);
        Params.AddIgnoredActor(PlayerOwner->GetPawn());
        Sight.bVisible = !GetWorld()->LineTraceTestByChannel(Camera, Anchor, ECC_Visibility, Params);
        Sight.CheckedAt = Now;
        --Budget;
    }
    for (int32 Index : Chosen)
    {
        if (!ViewTargets.IsValidIndex(Index))
            continue;
        const auto &T = ViewTargets[Index];
        FVector Anchor;
        if (!Net->GetNameplateAnchor(T.ObjectId, Anchor))
            continue;
        FVector2D Screen;
        if (!PlayerOwner->ProjectWorldLocationToScreen(Anchor, Screen, true))
            continue;
        const FString Label = T.Name.Left(40);
        const FString Guild = T.GuildName.IsEmpty() ? FString() : FString::Printf(TEXT("<%s>"), *T.GuildName.Left(47));
        const float Scale = GetGameplayUIScale();
        const FVector2D NameSize = FontMeasure->Measure(Label, NameFont) * Scale;
        const float LabelWidth = NameSize.X, LineHeight = NameSize.Y;
        float GuildWidth = 0, GuildHeight = 0;
        if (!Guild.IsEmpty())
            GetTextSize(Guild, GuildWidth, GuildHeight, nullptr, Scale);
        const float W = FMath::Max(FMath::Max(LabelWidth, GuildWidth) + 16 * Scale, 40.f * Scale);
        const bool ShowHealth = bShowNameplateDetails && T.HealthPercent >= 0;
        const float H = LineHeight + GuildHeight + (ShowHealth ? 14.f : 6.f) * Scale;
        const FBox2D Rect(Screen - FVector2D(W * .5f, H), Screen + FVector2D(W * .5f, 0));
        if (Rect.Min.X < 8 || Rect.Max.X > Canvas->SizeX - 8 || Rect.Min.Y < 8 || Rect.Max.Y > Canvas->SizeY - 8)
            continue;
        if (Occupied.ContainsByPredicate([&](const FBox2D &Other) { return Other.Intersect(Rect); }))
            continue;
        auto &Sight = PlateVisibility.FindOrAdd(T.ObjectId);
        if (!Sight.bVisible || Sight.CheckedAt < 0 || Now - Sight.CheckedAt > .5)
            continue;
        const auto *Appearance = Net->PlayerAppearances.Find(T.ObjectId);
        const auto Kind = CamelotUI::NameColor(T.bNPC, Appearance ? Appearance->Realm : 0, LocalAppearance ? LocalAppearance->Realm : 0, Appearance ? Appearance->Level : 0, LocalAppearance ? LocalAppearance->Level : 0);
        FLinearColor Color = Theme->Text;
        using CamelotUI::ENameColor;
        switch (Kind)
        {
        case ENameColor::Friendly:
            Color = Theme->NameAlly;
            break;
        case ENameColor::Enemy:
            Color = Theme->NameEnemy;
            break;
        case ENameColor::Gray:
            Color = Theme->ConGray;
            break;
        case ENameColor::Green:
            Color = Theme->ConGreen;
            break;
        case ENameColor::Blue:
            Color = Theme->ConBlue;
            break;
        case ENameColor::Yellow:
            Color = Theme->ConYellow;
            break;
        case ENameColor::Orange:
            Color = Theme->ConOrange;
            break;
        case ENameColor::Red:
            Color = Theme->ConRed;
            break;
        case ENameColor::Purple:
            Color = Theme->ConPurple;
            break;
        case ENameColor::DarkPurple:
            Color = Theme->ConDarkPurple;
            break;
        default:
            break;
        }
        const bool Selected = T.ObjectId == ViewState.TargetObjectId;
        if (bShowNameplateDetails)
        {
            DrawRect(Selected ? Theme->Accent : Theme->Border, Rect.Min.X, Rect.Min.Y, W, H);
            DrawRect(Theme->Panel, Rect.Min.X + 1, Rect.Min.Y + 1, W - 2, H - 2);
        }
        FCanvasTextItem NameItem(FVector2D(Screen.X - LabelWidth * .5f, Rect.Min.Y + 3 * Scale), FText::FromString(Label), NameFont, Color);
        NameItem.Scale = FVector2D(Scale, Scale);
        NameItem.EnableShadow(FLinearColor::Black, FVector2D(Scale, Scale));
        Canvas->DrawItem(NameItem);
        if (!Guild.IsEmpty())
            DrawText(Guild, Theme->Muted, Screen.X - GuildWidth * .5f, Rect.Min.Y + 3 * Scale + LineHeight, nullptr, Scale);
        if (ShowHealth)
        {
            DrawRect(Theme->Recess, Rect.Min.X + 6 * Scale, Rect.Max.Y - 8 * Scale, W - 12 * Scale, 4 * Scale);
            DrawRect(Theme->Health, Rect.Min.X + 6 * Scale, Rect.Max.Y - 8 * Scale, (W - 12 * Scale) * FMath::Clamp(T.HealthPercent / 100.f, 0.f, 1.f), 4 * Scale);
        }
        AddHitBox(Rect.Min, FVector2D(W, H), FName(*FString::Printf(TEXT("Entity_%d"), T.ObjectId)), true, Selected ? 20 : 10);
        Occupied.Add(Rect);
    }
}
void ADaocCombatHUD::NotifyHitBoxClick(FName BoxName)
{
    const FString Name = BoxName.ToString();
    if (Name.StartsWith(TEXT("Entity_")) && PlayerOwner && PlayerOwner->bShowMouseCursor)
        if (auto *Combat = GetCombat())
            Combat->SelectTarget(FCString::Atoi(*Name.Mid(7)));
    Super::NotifyHitBoxClick(BoxName);
}
