#include "CamelotTrainerWindow.h"
#include "Camelot/CamelotTrainer.h"
#include "CamelotUITheme.h"
#include "CamelotUIWidgets.h"
#include "DaocCombatHUD.h"
#include "DaocNetworkSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SCamelotTrainerWindow::Construct(const FArguments &Args)
{
    Owner = Args._Owner;
    const auto *Theme = Owner.IsValid() ? Owner->GetUITheme() : GetDefault<UCamelotUITheme>();
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", Theme->FontSize);
    ChildSlot[SNew(SCamelotPanel)
                  .Theme(Theme)
                  .Title(FText::FromString(TEXT("Training")))[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", Theme->FontSize + 2)).ColorAndOpacity(Theme->Accent).Text_Lambda([this] { return FText::FromString(State.Name); })] + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Close"))).OnClicked_Lambda([this] {
                      if (Owner.IsValid())
                          Owner->CloseTrainer();
                      return FReply::Handled();
                  })]] + SVerticalBox::Slot().AutoHeight().Padding(0, 12)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).Text_Lambda([this] {
                      return FText::FromString(FString::Printf(TEXT("Specialization points: %d     Realm "
                                                                    "points: %d"),
                                                               State.SpecializationPoints, State.RealmPoints));
                  })] + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)[SNew(SButton).Text(FText::FromString(TEXT("Specializations"))).OnClicked_Lambda([this] {
                      Pages->SetActiveWidgetIndex(0);
                      return FReply::Handled();
                  })] + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)[SNew(SButton).Text(FText::FromString(TEXT("Realm abilities"))).OnClicked_Lambda([this] {
                      Pages->SetActiveWidgetIndex(1);
                      return FReply::Handled();
                  })]] + SVerticalBox::Slot().FillHeight(1)[SAssignNew(Pages, SWidgetSwitcher) + SWidgetSwitcher::Slot()[SNew(SScrollBox).ScrollBarAlwaysVisible(true) + SScrollBox::Slot()[SAssignNew(SpecRows, SVerticalBox)]] + SWidgetSwitcher::Slot()[SNew(SScrollBox).ScrollBarAlwaysVisible(true) + SScrollBox::Slot()[SAssignNew(RealmRows, SVerticalBox)]]] +
                                                              SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Accent).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(State.Feedback); })]]];
}
int32 SCamelotTrainerWindow::Desired(const FDaocTrainerLine &Line, bool Realm) const
{
    const auto *Value = DesiredRanks.Find(Line.Index + (Realm ? 100 : 0));
    return FMath::Clamp(Value ? *Value : Line.Rank, Line.Rank, FMath::Max(Line.Rank, Line.MaximumRank));
}
void SCamelotTrainerWindow::Refresh(const FDaocTrainerState &NewState)
{
    const bool Changed = State.Revision != NewState.Revision || State.SpecializationPoints != NewState.SpecializationPoints || State.RealmPoints != NewState.RealmPoints;
    if (State.ObjectId != NewState.ObjectId || !NewState.bOpen)
        DesiredRanks.Reset();
    State = NewState;
    SetVisibility(State.bOpen ? EVisibility::Visible : EVisibility::Collapsed);
    if (!State.bOpen)
    {
        SpecRows->ClearChildren();
        RealmRows->ClearChildren();
        return;
    }
    if (Changed)
    {
        RebuildRows(false);
        RebuildRows(true);
    }
}
void SCamelotTrainerWindow::RebuildRows(bool Realm)
{
    const auto *Theme = Owner.IsValid() ? Owner->GetUITheme() : GetDefault<UCamelotUITheme>();
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", Theme->FontSize);
    auto Rows = Realm ? RealmRows : SpecRows;
    Rows->ClearChildren();
    const auto &Lines = Realm ? State.RealmAbilities : State.Specializations;
    if (Lines.IsEmpty())
    {
        const bool Known = Realm ? State.bRealmKnown : State.bSpecsKnown;
        Rows->AddSlot().AutoHeight().Padding(0, 16)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Muted).Text(FText::FromString(Known ? TEXT("No trainable lines available.") : TEXT("Waiting for training options...")))];
    }
    for (const auto &Line : Lines)
    {
        const int32 Key = Line.Index + (Realm ? 100 : 0);
        DesiredRanks.FindOrAdd(Key) = Desired(Line, Realm);
        FString Label = Line.Name;
        if (!Line.bAvailable && Label.StartsWith(TEXT("[")) && Label.EndsWith(TEXT("]")))
            Label = Label.Mid(1, Label.Len() - 2);
        Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 14)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", Theme->FontSize)).ColorAndOpacity(Line.bAvailable ? Theme->Text : Theme->Muted).Text(FText::FromString(Label))] + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Muted).Text_Lambda([this, Line, Realm] {
            if (!Line.bAvailable)
                return FText::FromString(TEXT("Requirements not met"));
            const int32 Rank = Desired(Line, Realm);
            const int32 Cost = camelot::TrainingCost(Line.Rank, Rank, Line.NextCost, Realm);
            return FText::FromString(FString::Printf(TEXT("Rank %d  →  %d     %d points"), Line.Rank, Rank, Cost));
        })] + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[SNew(SButton).Text(FText::FromString(TEXT("−"))).IsEnabled_Lambda([this, Line, Realm] { return !State.bPending && Desired(Line, Realm) > Line.Rank; }).OnClicked_Lambda([this, Line, Realm, Key] {
            DesiredRanks.Add(Key, Desired(Line, Realm) - 1);
            return FReply::Handled();
        })] + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SSlider).SliderBarColor(Theme->Muted).SliderHandleColor(Theme->Accent).StepSize(1.f / FMath::Max(1, Line.MaximumRank - Line.Rank)).MouseUsesStep(true).IsEnabled_Lambda([this, Line] { return !State.bPending && Line.MaximumRank > Line.Rank; }).Value_Lambda([this, Line, Realm] { return float(Desired(Line, Realm) - Line.Rank) / FMath::Max(1, Line.MaximumRank - Line.Rank); }).OnValueChanged_Lambda([this, Line, Key](float Value) {
            DesiredRanks.Add(Key, Line.Rank + FMath::RoundToInt(Value * (Line.MaximumRank - Line.Rank)));
        })] + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SNew(SButton).Text(FText::FromString(TEXT("+"))).IsEnabled_Lambda([this, Line, Realm] { return !State.bPending && Desired(Line, Realm) < Line.MaximumRank; }).OnClicked_Lambda([this, Line, Realm, Key] {
            DesiredRanks.Add(Key, Desired(Line, Realm) + 1);
            return FReply::Handled();
        })] + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Train"))).IsEnabled_Lambda([this, Line, Realm] { return !State.bPending && Desired(Line, Realm) > Line.Rank; }).OnClicked_Lambda([this, Line, Realm] { return Train(Line, Realm); })]]];
    }
}
FReply SCamelotTrainerWindow::Train(const FDaocTrainerLine &Line, bool Realm)
{
    auto *Net = Owner.IsValid() ? UDaocNetworkSubsystem::Get(Owner.Get()) : nullptr;
    if (Net && Net->RequestTraining(Realm, Line.Index, Desired(Line, Realm), State.Revision))
        Refresh(Net->GetTrainerState());
    else
        State.Feedback = TEXT("Training options changed. Please review your selection.");
    return FReply::Handled();
}
FReply SCamelotTrainerWindow::OnKeyDown(const FGeometry &Geometry, const FKeyEvent &Event)
{
    if (Event.GetKey() == EKeys::Escape && Owner.IsValid())
    {
        Owner->CloseTrainer();
        return FReply::Handled();
    }
    return SCompoundWidget::OnKeyDown(Geometry, Event);
}
