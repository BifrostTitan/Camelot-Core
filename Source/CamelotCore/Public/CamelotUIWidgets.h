#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"
class UCamelotUITheme;

/// Reusable themed panel with a title and caller-supplied content.
class CAMELOTCORE_API SCamelotPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotPanel) : _Theme(nullptr)
    {
    }
    SLATE_ARGUMENT(const UCamelotUITheme *, Theme)
    SLATE_ATTRIBUTE(FText, Title)
    SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
};

/// Themed resource bar displaying a reported resource fraction.
class CAMELOTCORE_API SCamelotResourceBar : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotResourceBar) : _Theme(nullptr), _Color(FLinearColor::White)
    {
    }
    SLATE_ARGUMENT(const UCamelotUITheme *, Theme)
    SLATE_ATTRIBUTE(FText, Label)
    // Unset means unknown, rather than empty or full.
    SLATE_ATTRIBUTE(TOptional<float>, Fraction)
    SLATE_ARGUMENT(FLinearColor, Color)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);

  private:
    FProgressBarStyle Style;
};
