#include "CamelotAbilityDrag.h"
#include "CamelotUITheme.h"
#include "DaocCombatComponent.h"
#include "DaocCombatHUD.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"

TSharedRef<FCamelotAbilityDrag> FCamelotAbilityDrag::New(const FDaocCombatAbility &A, UDaocCombatComponent *C, UTexture2D *T)
{
    auto Op = MakeShared<FCamelotAbilityDrag>();
    Op->Ability = A;
    Op->Source = C;
    Op->Texture.Reset(T);
    Op->Brush = *FCoreStyle::Get().GetBrush("WhiteBrush");
    Op->Brush.ImageSize = FVector2D(48, 48);
    if (T)
    {
        Op->Brush.SetResourceObject(T);
        Op->Brush.DrawAs = ESlateBrushDrawType::Image;
    }
    else
        Op->Brush.TintColor = FLinearColor(.2f, .24f, .29f, 1);
    Op->bCreateNewWindow = false;
    Op->Construct();
    return Op;
}
TSharedPtr<SWidget> FCamelotAbilityDrag::GetDefaultDecorator() const
{
    return SNew(SBox).WidthOverride(48).HeightOverride(48)[SNew(SImage).Image(&Brush)];
}
void SCamelotAbilityIcon::Construct(const FArguments &Args)
{
    Ability = Args._Ability;
    Source = Args._Source;
    const auto *Theme = Args._Theme ? Args._Theme : GetDefault<UCamelotUITheme>();
    Brush = *FCoreStyle::Get().GetBrush("WhiteBrush");
    Brush.ImageSize = FVector2D(36, 36);
    Texture.Reset(Theme->LoadAbilityIcon(Ability));
    if (Texture.IsValid())
    {
        Brush.SetResourceObject(Texture.Get());
        Brush.DrawAs = ESlateBrushDrawType::Image;
    }
    else
        Brush.TintColor = Theme->Border;
    SetToolTipText(FText::FromString(Ability.Name));
    ChildSlot[SNew(SBox).WidthOverride(36).HeightOverride(36)[SNew(SImage).Image(&Brush)]];
}
FReply SCamelotAbilityIcon::OnMouseButtonDown(const FGeometry &, const FPointerEvent &E)
{
    if (E.GetEffectingButton() == EKeys::LeftMouseButton && Source.IsValid() && Ability.Kind != EDaocAbilityKind::Specialization)
        return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
    return FReply::Unhandled();
}
FReply SCamelotAbilityIcon::OnDragDetected(const FGeometry &, const FPointerEvent &)
{
    if (!Source.IsValid() || Ability.Kind == EDaocAbilityKind::Specialization)
        return FReply::Unhandled();
    return FReply::Handled().BeginDragDrop(FCamelotAbilityDrag::New(Ability, Source.Get(), Texture.Get()));
}
void SCamelotHotbarDropZone::Construct(const FArguments &A)
{
    Owner = A._Owner;
    Slot = A._Slot;
    ChildSlot[A._Content.Widget];
}
FReply SCamelotHotbarDropZone::OnDrop(const FGeometry &, const FDragDropEvent &E)
{
    const auto Op = E.GetOperationAs<FCamelotAbilityDrag>();
    auto *C = Owner.IsValid() ? Owner->GetCombat() : nullptr;
    if (Op.IsValid() && C && Slot >= 0 && Slot < C->VisibleHotbarCount * 8 && Op->Source.Get() == C && C->AssignHotbar(Slot, Op->Ability))
    {
        Owner->RefreshGameUI();
        return FReply::Handled();
    }
    return FReply::Unhandled();
}
void SCamelotHotbarDropZone::OnMouseEnter(const FGeometry &G, const FPointerEvent &E)
{
    SCompoundWidget::OnMouseEnter(G, E);
    if (Owner.IsValid())
        Owner->SetHoveredHotbarSlot(Slot);
}
void SCamelotHotbarDropZone::OnMouseLeave(const FPointerEvent &E)
{
    SCompoundWidget::OnMouseLeave(E);
    if (Owner.IsValid())
        Owner->ClearHoveredHotbarSlot(Slot);
}
