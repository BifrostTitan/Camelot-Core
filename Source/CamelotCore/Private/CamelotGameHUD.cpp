#include "CamelotGameHUD.h"
#include "CamelotAbilityDrag.h"
#include "CamelotCharacterMenu.h"
#include "CamelotTrainerWindow.h"
#include "CamelotUITheme.h"
#include "CamelotUIWidgets.h"
#include "DaocCombatComponent.h"
#include "DaocCombatHUD.h"
#include "DaocDeveloperComponent.h"
#include "DaocNetworkSubsystem.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace
{
FText Text(const TCHAR *Value)
{
    return FText::FromString(Value);
}
const UCamelotUITheme *ThemeFor(const TWeakObjectPtr<ADaocCombatHUD> &Owner)
{
    return Owner.IsValid() ? Owner->GetUITheme() : GetDefault<UCamelotUITheme>();
}
} // namespace

void SCamelotGameHUD::Construct(const FArguments &Args)
{
    Owner = Args._Owner;
    const auto *Theme = ThemeFor(Owner);
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", Theme->FontSize);
    auto Player = SNew(SVerticalBox);
    for (int32 Kind = 0; Kind < 3; ++Kind)
    {
        const TCHAR *Names[] = {TEXT("Health"), TEXT("Mana"), TEXT("Endurance")};
        const FLinearColor Colors[] = {Theme->Health, Theme->Mana, Theme->Endurance};
        Player->AddSlot().AutoHeight().Padding(0, 2)[SNew(SCamelotResourceBar).Theme(Theme).Label(Text(Names[Kind])).Color(Colors[Kind]).Fraction_Lambda([this, Kind] { return Resource(Kind); })];
    }
    Player->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).Text_Lambda([this] { return StatusText(); })];
    Player->AddSlot().AutoHeight().Padding(0, 4)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Muted).Text_Lambda([this] {
        if (!Owner.IsValid() || !Owner->GetViewState().bProgressKnown)
            return FText();
        const auto &S = Owner->GetViewState();
        return FText::FromString(FString::Printf(TEXT("XP %.1f%% · Skill points %d"), S.ExperiencePercent, S.SkillPoints));
    })];

    auto Target = SNew(SVerticalBox) +
                  SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 6)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", Theme->FontSize)).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Clipping(EWidgetClipping::ClipToBounds).ColorAndOpacity(Theme->Accent).Text_Lambda([this] { return TargetText(); })] +
                                                                         SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)[SNew(SButton)
                                                                                                                                    .Text(Text(TEXT("Interact")))
                                                                                                                                    .Visibility_Lambda([this] {
                                                                                                                                        if (Owner.IsValid())
                                                                                                                                            for (const auto &T : Owner->GetViewTargets())
                                                                                                                                                if (T.ObjectId == Owner->GetViewState().TargetObjectId && T.bNPC)
                                                                                                                                                    return EVisibility::Visible;
                                                                                                                                        return EVisibility::Collapsed;
                                                                                                                                    })
                                                                                                                                    .OnClicked_Lambda([this] {
                                                                                                                                        if (Owner.IsValid())
                                                                                                                                            Owner->InteractWithTarget();
                                                                                                                                        return FReply::Handled();
                                                                                                                                    })]] +
                  SVerticalBox::Slot().AutoHeight()[SNew(SCamelotResourceBar).Theme(Theme).Label(Text(TEXT("Health"))).Color(Theme->Health).Fraction_Lambda([this] { return Resource(3); })];
    Player->AddSlot().AutoHeight()[Target];

    auto Chat = SNew(SVerticalBox) + SVerticalBox::Slot().FillHeight(1)[SAssignNew(ChatList, SListView<TSharedPtr<FDaocChatMessage>>).ListItemsSource(&ChatItems).SelectionMode(ESelectionMode::None).OnGenerateRow_Lambda([Theme, Font](TSharedPtr<FDaocChatMessage> Message, const TSharedRef<STableViewBase> &List) {
        const FLinearColor Color = Message->ChatType == 0 ? Theme->Accent : Message->ChatType == 3 || Message->ChatType == 4 ? Theme->Friendly
                                                                                                                             : Theme->Text;
        return SNew(STableRow<TSharedPtr<FDaocChatMessage>>, List)
            .Padding(FMargin(0, 2))
                // The HUD is authored at a fixed 380-unit panel
                // width then scaled as a whole. Resolve wrapping
                // before the list measures row height; deferred
                // auto-wrap would clip new messages.
                [SNew(STextBlock).WrapTextAt(334.f).ColorAndOpacity(Color).Font(Font).Text(FText::FromString(Message->Text))];
    })] +
                SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SAssignNew(ChatInput, SEditableTextBox)
                                                                    .Font(Font)
                                                                    .HintText(Text(TEXT("Chat")))
                                                                    .SelectAllTextWhenFocused(false)
                                                                    .ClearKeyboardFocusOnCommit(false)
                                                                    .OnTextChanged_Lambda([this](const FText &) {
                                                                        if (Owner.IsValid() && !Owner->IsChatOpen())
                                                                            Owner->OpenChat();
                                                                    })
                                                                    .OnKeyDownHandler_Lambda([this](const FGeometry &, const FKeyEvent &Event) {
                                                                        if (Event.GetKey() == EKeys::Escape)
                                                                        {
                                                                            if (Owner.IsValid())
                                                                                Owner->CloseChat();
                                                                            return FReply::Handled();
                                                                        }
                                                                        return FReply::Unhandled();
                                                                    })
                                                                    .OnTextCommitted_Lambda([this](const FText &Value, ETextCommit::Type Type) {
                                                                        if (!Owner.IsValid())
                                                                            return;
                                                                        if (Type == ETextCommit::OnEnter)
                                                                        {
                                                                            if (Value.IsEmpty())
                                                                            {
                                                                                Owner->CloseChat();
                                                                                return;
                                                                            }
                                                                            if (Owner->SubmitChat(Value.ToString()))
                                                                            {
                                                                                ChatError.Reset();
                                                                                ChatInput->SetText(FText());
                                                                                Owner->CloseChat();
                                                                            }
                                                                            else
                                                                                ChatError = TEXT("Not sent: check connection and use "
                                                                                                 "1–254 printable ASCII characters.");
                                                                        }
                                                                        else if (Type == ETextCommit::OnUserMovedFocus)
                                                                            Owner->CloseChat();
                                                                    })] +
                SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Accent).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(ChatError); })];

    auto Bar = SNew(SVerticalBox);
    for (int32 B = 0; B < 3; ++B)
    {
        auto Row = SNew(SHorizontalBox);
        for (int32 I = 0; I < 8; ++I)
            Row->AddSlot().FillWidth(1).Padding(2, 0)[Slot(B * 8 + I)];
        const TCHAR *Label = B == 0 ? TEXT("1–8") : B == 1 ? TEXT("Shift + 1–8")
                                                           : TEXT("Ctrl + 1–8");
        Bar->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[SNew(SVerticalBox).Visibility_Lambda([this, B] { return B < BarCount() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }) + SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 4)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Accent).Text(Text(Label))] + SVerticalBox::Slot().AutoHeight()[Row]];
    }
    auto Developer = SNew(SVerticalBox) + SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).AutoWrapText(true).Text_Lambda([this] {
                                                                                 const auto *D = Owner.IsValid() ? Owner->GetDeveloper() : nullptr;
                                                                                 return FText::FromString(D ? D->GetInformation() : FString());
                                                                             })]] +
                     SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(STextBlock)
                                                                         .Font(Font)
                                                                         .ColorAndOpacity(Theme->Muted)
                                                                         .AutoWrapText(true)
                                                                         .Text(Text(TEXT("Ctrl+Alt: F flight · I inspect · C copy · "
                                                                                         "R rebuild · A admin")))];

    // One authored layout scales as a unit; projection/nameplate coordinates
    // remain viewport pixels.
    ChildSlot[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SBox).WidthOverride_Lambda([this] { return 1600.f / (Owner.IsValid() ? Owner->GetGameplayUIScale() : .75f); }).HeightOverride_Lambda([this] { return 900.f / (Owner.IsValid() ? Owner->GetGameplayUIScale() : .75f); })[SNew(SConstraintCanvas) + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(0, 0)).Offset(FMargin(24, 24, 300, 250))[SNew(SCamelotPanel).Theme(Theme).Title(Text(TEXT("Status")))[Player]] + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(.5f, 0)).Offset(FMargin(-190, 154, 380, 80))[SNew(SCamelotPanel).Theme(Theme).Title(Text(TEXT("CASTING"))).Visibility_Lambda([this] { return Owner.IsValid() && Owner->GetViewState().CastingRemaining > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).Text_Lambda([this] {
        return Owner.IsValid() ? FText::FromString(FString::Printf(TEXT("%.1f "
                                                                        "seconds "
                                                                        "remaining"),
                                                                   Owner->GetViewState().CastingRemaining))
                               : FText();
    })]] + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(0, 0)).Offset(FMargin(24, 292, 300, 170))[SNew(SCamelotPanel).Theme(Theme).Title(Text(TEXT("EFFECTS"))).Visibility_Lambda([this] {
        return LastEffects.IsEmpty() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
    })[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Effects, SVerticalBox)]]] + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(1, 0)).Offset(FMargin(-440, 76, 416, 246))[SNew(SCamelotPanel).Theme(Theme).Title(Text(TEXT("INSPECTOR"))).Visibility_Lambda([this] {
        const auto *D = Owner.IsValid() ? Owner->GetDeveloper() : nullptr;
        return !bCharacterMenuOpen && D && !D->GetInformation().IsEmpty() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
    })[Developer]] + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(1, 0)).Offset(FMargin(-440, 24, 416, 36))[SNew(SButton).IsFocusable(false).Text(Text(TEXT("Character"))).OnClicked_Lambda([this] {
        if (Owner.IsValid())
            Owner->ToggleCharacterMenu();
        else
            ToggleCharacterMenu();
        return FReply::Handled();
    })] + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(1, 0)).Offset_Lambda([this] {
        const bool Equipment = CharacterMenu.IsValid() && CharacterMenu->GetPage() == 3;
        const float AvailableHeight = 900.f - (32.f + BarCount() * 92.f) - 24.f - 76.f - 16.f;
        return FMargin(Equipment ? -724.f : -440.f, 76, Equipment ? 700.f : 416.f, FMath::Min(Equipment ? 650.f : 590.f, AvailableHeight));
    })[SNew(SCamelotPanel).Theme(Theme).Title(Text(TEXT("Character"))).Visibility_Lambda([this] {
        return bCharacterMenuOpen ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
    })[SAssignNew(CharacterMenu, SCamelotCharacterMenu).Owner(Owner.Get())]] + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(0, 1)).Offset(FMargin(24, -294, 380, 270))[SNew(SCamelotPanel).Theme(Theme).Title(Text(TEXT("CHAT")))[Chat]] + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(.5f, 1)).Offset_Lambda([this] {
        const float Height = 32.f + BarCount() * 92.f;
        return FMargin(-350, -Height - 24, 700, Height);
    })[SNew(SCamelotPanel).Theme(Theme).Title(Text(TEXT("")))[Bar]] + SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector).Anchors(FAnchors(.5f, 1)).Offset_Lambda([this] { return FMargin(-350, -74 - BarCount() * 92, 700, 28); })[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).Justification(ETextJustify::Center).Text_Lambda([this] {
        const auto *C = Owner.IsValid() ? Owner->GetCombat() : nullptr;
        return FText::FromString(C ? C->LastFeedback.Left(120) : FString());
    })] + SConstraintCanvas::Slot().Alignment(FVector2D(.5f, .5f)).Anchors(FAnchors(.5f, .5f)).Offset(FMargin(0, 0, 620, 560))[SAssignNew(TrainerWindow, SCamelotTrainerWindow).Owner(Owner.Get()).Visibility(EVisibility::Collapsed)]]]];
    Refresh();
}

TSharedRef<SWidget> SCamelotGameHUD::Slot(int32 Index)
{
    const auto *Theme = ThemeFor(Owner);
    return SNew(SCamelotHotbarDropZone)
        .Owner(Owner.Get())
        .Slot(Index)[SNew(SBox).HeightOverride(60)[SNew(SButton)
                                                       .IsFocusable(false)
                                                       .ContentPadding(4)
                                                       .OnClicked_Lambda([this, Index] {
                                                           if (Owner.IsValid())
                                                               if (auto *C = Owner->GetCombat())
                                                                   C->ActivateHotbar(Index);
                                                           return FReply::Handled();
                                                       })
                                                       .ToolTipText_Lambda([this, Index] {
                                                           const auto &A = Abilities[Index];
                                                           return FText::FromString(A.Name.IsEmpty() ? TEXT("Empty") : FString::Printf(TEXT("%s\n%s\nLevel %d"), *A.Name, *A.LineName, A.Level));
                                                       })[SNew(SOverlay) + SOverlay::Slot()[SNew(SImage).Image(&Icons[Index]).Visibility_Lambda([this, Index] { return Icons[Index].GetResourceObject() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })] + SOverlay::Slot().VAlign(VAlign_Top)[SNew(STextBlock).ColorAndOpacity(Theme->Muted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 10)).Text(FText::AsNumber(Index % 8 + 1))] + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(STextBlock).ColorAndOpacity(Theme->Text).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).Text_Lambda([this, Index] { return Icons[Index].GetResourceObject() ? FText() : FText::FromString(Abilities[Index].Name.Left(11)); })] + SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center)[SNew(STextBlock).ColorAndOpacity(Theme->Accent).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)).Text_Lambda([this, Index] {
                                                           const float R = Abilities[Index].CooldownRemaining;
                                                           return R > 0 ? FText::FromString(FString::Printf(TEXT("%.1fs"), R)) : FText();
                                                       })]]]];
}
FText SCamelotGameHUD::StatusText() const
{
    if (!Owner.IsValid() || !Owner->GetViewState().bStatusKnown)
        return FText();
    const auto &S = Owner->GetViewState();
    return S.bDead ? Text(TEXT("Defeated")) : S.bAttacking ? Text(TEXT("Attacking"))
                                          : S.bSitting     ? Text(TEXT("Resting"))
                                                           : FText();
}
FText SCamelotGameHUD::TargetText() const
{
    if (Owner.IsValid())
        for (const auto &T : Owner->GetViewTargets())
            if (T.ObjectId == Owner->GetViewState().TargetObjectId)
                return FText::FromString(T.Name.Left(36));
    return Text(TEXT("No target"));
}
TOptional<float> SCamelotGameHUD::Resource(int32 Kind) const
{
    if (!Owner.IsValid())
        return {};
    const auto &S = Owner->GetViewState();
    if (Kind == 3)
    {
        for (const auto &T : Owner->GetViewTargets())
            if (T.ObjectId == S.TargetObjectId && T.HealthPercent >= 0)
                return T.HealthPercent / 100.f;
        return {};
    }
    if (!S.bStatusKnown)
        return {};
    return (Kind == 0 ? S.HealthPercent : Kind == 1 ? S.ManaPercent
                                                    : S.EndurancePercent) /
           100.f;
}
TSharedRef<SWidget> SCamelotGameHUD::GetChatInput() const
{
    return ChatInput.ToSharedRef();
}
int32 SCamelotGameHUD::BarCount() const
{
    const auto *C = Owner.IsValid() ? Owner->GetCombat() : nullptr;
    return C ? FMath::Clamp(C->VisibleHotbarCount, 1, 3) : 1;
}
void SCamelotGameHUD::ToggleCharacterMenu()
{
    bCharacterMenuOpen = !bCharacterMenuOpen;
    CharacterMenu->SetMenuOpen(bCharacterMenuOpen);
}
void SCamelotGameHUD::ShowCharacterPage(int32 Page)
{
    bCharacterMenuOpen = true;
    CharacterMenu->SetMenuOpen(true);
    CharacterMenu->SelectPage(Page);
}
void SCamelotGameHUD::Refresh()
{
    if (TrainerWindow.IsValid())
    {
        auto *Net = Owner.IsValid() ? UDaocNetworkSubsystem::Get(Owner.Get()) : nullptr;
        const auto T = Net ? Net->GetTrainerState() : FDaocTrainerState();
        TrainerWindow->Refresh(T);
        if (Owner.IsValid())
            Owner->SetTrainerOpen(T.bOpen);
    }
    auto *Combat = Owner.IsValid() ? Owner->GetCombat() : nullptr;
    const auto *Theme = ThemeFor(Owner);
    for (int32 I = 0; I < 24; ++I)
    {
        const auto A = Combat && Combat->Hotbar.IsValidIndex(I) ? Combat->Hotbar[I] : FDaocCombatAbility();
        if (A.Icon != Abilities[I].Icon || A.Name != Abilities[I].Name)
        {
            Icons[I].SetResourceObject(nullptr);
            if (!A.Name.IsEmpty())
                if (const auto *Asset = Theme->AbilityIcons.Find(A.Icon))
                    Icons[I].SetResourceObject(Asset->LoadSynchronous());
            if (Owner.IsValid())
                Owner->RetainHotbarIcon(I, Cast<UTexture2D>(Icons[I].GetResourceObject()));
            Icons[I].DrawAs = ESlateBrushDrawType::Image;
            Icons[I].ImageSize = FVector2D(48, 48);
        }
        Abilities[I] = A;
    }
    if (bCharacterMenuOpen && CharacterMenu.IsValid())
        CharacterMenu->Refresh();
    const int64 ChatRevision = Combat ? Combat->GetChatRevision() : -1;
    if (ChatRevision != LastChatRevision)
    {
        // Only visible rows have Slate text layouts. Retain their identities when
        // combat messages append or the bounded history drops its oldest lines.
        // Table views report normalized scrollbar distance, not pixels.
        const bool Follow = ChatItems.IsEmpty() || ChatList->GetScrollDistanceRemaining().Y <= .0001;
        const float OldOffset = ChatList->GetScrollOffset();
        TMap<int64, TSharedPtr<FDaocChatMessage>> Existing;
        for (const auto &Item : ChatItems)
            Existing.Add(Item->SequenceId, Item);
        const auto Messages = Combat ? Combat->GetChatMessages() : TArray<FDaocChatMessage>();
        int32 Removed = 0;
        while (Removed < ChatItems.Num() && (Messages.IsEmpty() || ChatItems[Removed]->SequenceId < Messages[0].SequenceId))
            ++Removed;
        ChatItems.Reset(Messages.Num());
        for (const auto &Message : Messages)
        {
            const auto *Item = Existing.Find(Message.SequenceId);
            ChatItems.Add(Item ? *Item : MakeShared<FDaocChatMessage>(Message));
        }
        LastChatRevision = ChatRevision;
        ChatList->RequestListRefresh();
        if (Follow)
            ChatList->ScrollToBottom();
        else
            ChatList->SetScrollOffset(FMath::Max(0.f, OldOffset - Removed));
    }
    const auto CurrentEffects = Combat ? Combat->GetEffects() : TArray<FDaocCombatEffect>();
    bool EffectsChanged = CurrentEffects.Num() != LastEffects.Num();
    for (int32 I = 0; !EffectsChanged && I < CurrentEffects.Num(); ++I)
        EffectsChanged = CurrentEffects[I].InternalId != LastEffects[I].InternalId || CurrentEffects[I].Name != LastEffects[I].Name || CurrentEffects[I].ReportedSeconds != LastEffects[I].ReportedSeconds || CurrentEffects[I].bCancelable != LastEffects[I].bCancelable;
    if (EffectsChanged)
    {
        LastEffects = CurrentEffects;
        Effects->ClearChildren();
        for (const auto &E : LastEffects)
            Effects->AddSlot().AutoHeight().Padding(0, 2)[SNew(SButton).IsFocusable(false).IsEnabled(E.bCancelable).Text(FText::FromString(E.Name)).ToolTipText(FText::FromString(FString::Printf(TEXT("Last server duration: %ds%s"), E.ReportedSeconds, E.bCancelable ? TEXT("\nClick to request cancellation.") : TEXT("")))).OnClicked_Lambda([this, Id = E.InternalId] {
                if (Owner.IsValid())
                    if (auto *C = Owner->GetCombat())
                        C->CancelEffect(Id);
                return FReply::Handled();
            })];
    }
}
