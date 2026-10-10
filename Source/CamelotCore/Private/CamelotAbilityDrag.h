#pragma once
#include "Input/DragAndDrop.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "DaocCombatTypes.h"
class UTexture2D;
class UDaocCombatComponent;
class UCamelotUITheme;
class ADaocCombatHUD;

/// Drag payload retaining the learned ability and its originating combat component.
class FCamelotAbilityDrag : public FDragDropOperation
{
  public:
    DRAG_DROP_OPERATOR_TYPE(FCamelotAbilityDrag, FDragDropOperation)
    /// Creates a drag operation retaining its source identity and current snapshot.
    static TSharedRef<FCamelotAbilityDrag> New(const FDaocCombatAbility &Ability, UDaocCombatComponent *Source, UTexture2D *Texture);
    /// Builds the drag cursor decorator from the retained item or ability metadata.
    virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
    FDaocCombatAbility Ability;
    TWeakObjectPtr<UDaocCombatComponent> Source;

  private:
    TStrongObjectPtr<UTexture2D> Texture;
    FSlateBrush Brush;
};
/// Learned-ability icon that creates validated hotbar drag operations.
class SCamelotAbilityIcon : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotAbilityIcon) : _Source(nullptr), _Theme(nullptr)
    {
    }
    SLATE_ARGUMENT(FDaocCombatAbility, Ability)
    SLATE_ARGUMENT(UDaocCombatComponent *, Source)
    SLATE_ARGUMENT(const UCamelotUITheme *, Theme)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
    /// Handles slot/icon presses and requests drag detection when appropriate.
    virtual FReply OnMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
    /// Creates a drag payload bound to the current source and revision.
    virtual FReply OnDragDetected(const FGeometry &, const FPointerEvent &) override;

  private:
    FDaocCombatAbility Ability;
    TWeakObjectPtr<UDaocCombatComponent> Source;
    TStrongObjectPtr<UTexture2D> Texture;
    FSlateBrush Brush;
};
/// Hotbar slot accepting compatible learned-ability drag payloads.
class SCamelotHotbarDropZone : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotHotbarDropZone) : _Owner(nullptr), _Slot(0)
    {
    }
    SLATE_ARGUMENT(ADaocCombatHUD *, Owner)
    SLATE_ARGUMENT(int32, Slot)
    SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
    /// Validates the source drag payload before forwarding the slot operation.
    virtual FReply OnDrop(const FGeometry &, const FDragDropEvent &) override;
    /// Records the hovered slot for cursor-enabled binding actions.
    virtual void OnMouseEnter(const FGeometry &, const FPointerEvent &) override;
    /// Clears the hovered slot when the pointer leaves this widget.
    virtual void OnMouseLeave(const FPointerEvent &) override;

  private:
    TWeakObjectPtr<ADaocCombatHUD> Owner;
    int32 Slot = 0;
};
