#include "CamelotUIWidgets.h"
#include "Brushes/SlateColorBrush.h"
#include "CamelotUITheme.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SCamelotPanel::Construct(const FArguments &Args)
{
    const auto *Theme = Args._Theme ? Args._Theme : GetDefault<UCamelotUITheme>();
    FLinearColor Background = Theme->Panel;
    Background.A = 1; // Panels must stay readable over any world/background.
    const auto *Solid = FCoreStyle::Get().GetBrush("WhiteBrush");
    ChildSlot[SNew(SBorder).BorderImage(Solid).BorderBackgroundColor(Theme->Border).Padding(1)[SNew(SBorder).BorderImage(Solid).BorderBackgroundColor(Background).ForegroundColor(Theme->Text).Padding(12)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SNew(STextBlock).Text(Args._Title).ColorAndOpacity(Theme->Accent).Visibility_Lambda([Title = Args._Title] { return Title.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }).Font(FCoreStyle::GetDefaultFontStyle("Bold", Theme->FontSize))] + SVerticalBox::Slot().FillHeight(1)[Args._Content.Widget]]]];
}

void SCamelotResourceBar::Construct(const FArguments &Args)
{
    const auto *Theme = Args._Theme ? Args._Theme : GetDefault<UCamelotUITheme>();
    Style.SetBackgroundImage(FSlateColorBrush(Theme->Recess));
    Style.SetFillImage(FSlateColorBrush(FLinearColor::White));
    Style.SetMarqueeImage(FSlateColorBrush(FLinearColor::Transparent));
    const auto Value = Args._Fraction;
    ChildSlot[SNew(SBox).HeightOverride(22)[SNew(SOverlay) + SOverlay::Slot()[SNew(SProgressBar).Style(&Style).Percent_Lambda([Value] { return TOptional<float>(Value.Get().IsSet() ? FMath::Clamp(Value.Get().GetValue(), 0.f, 1.f) : 0.f); }).FillColorAndOpacity(Args._Color).BorderPadding(FVector2D(0, 0))] + SOverlay::Slot().Padding(7, 2)[SNew(STextBlock).Text(Args._Label).ColorAndOpacity(Theme->Text).Font(FCoreStyle::GetDefaultFontStyle("Regular", Theme->FontSize))] +
                                            SOverlay::Slot()
                                                .HAlign(HAlign_Right)
                                                .Padding(7, 2)[SNew(STextBlock)
                                                                   .Text_Lambda([Value] {
                                                                       const auto Fraction = Value.Get();
                                                                       return Fraction.IsSet() ? FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(FMath::Clamp(Fraction.GetValue(), 0.f, 1.f) * 100))) : FText::FromString(TEXT("—"));
                                                                   })
                                                                   .ColorAndOpacity(Theme->Text)
                                                                   .Font(FCoreStyle::GetDefaultFontStyle("Bold", Theme->FontSize))]]];
}
