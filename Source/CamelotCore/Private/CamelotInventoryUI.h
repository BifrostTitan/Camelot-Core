#pragma once
#include "Widgets/SCompoundWidget.h"
#include "Input/DragAndDrop.h"
#include "DaocInventoryTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Texture2D.h"
class ADaocCombatHUD;
class UDaocNetworkSubsystem;
class UCamelotUITheme;
class SCamelotInventorySlot;
class SCamelotPlayerPreview;
/// Session-backed inventory view forwarding validated item operations.
struct FCamelotInventoryView
{
    TWeakObjectPtr<UDaocNetworkSubsystem> Session;
    FDaocInventoryState State;
    int32 Selected = INDEX_NONE;
    FString Feedback;
    /// Finds a confirmed owned item by classic inventory slot; returns nullptr for an empty slot.
    const FDaocInventoryItem *Item(int32 Slot) const
    {
        return State.Items.FindByPredicate([Slot](const auto &I) { return I.Slot == Slot; });
    }
    /// Queues position, heading, speed and visibility in classic server units.
    bool Move(int32 From, int32 To, int32 Count, int64 Revision);
    /// Requests equipment placement or the first available backpack destination.
    bool EquipOrRemove(int32 Slot);
};
/// Item drag payload carrying its slot, session and inventory revision.
class FCamelotItemDrag : public FDragDropOperation
{
  public:
    DRAG_DROP_OPERATOR_TYPE(FCamelotItemDrag, FDragDropOperation)
    /// Creates a drag operation retaining its source identity and current snapshot.
    static TSharedRef<FCamelotItemDrag> New(UDaocNetworkSubsystem *Session, int32 Slot, int64 Revision, const FString &Name);
    /// Builds the drag cursor decorator from the retained item or ability metadata.
    virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
    TWeakObjectPtr<UDaocNetworkSubsystem> Session;
    int32 Slot = 0;
    int64 Revision = 0;

  private:
    FString Name;
};
/// Equipment or backpack slot displaying item state and accepting drag operations.
class SCamelotInventorySlot : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotInventorySlot) : _Slot(0), _Theme(nullptr)
    {
    }
    SLATE_ARGUMENT(TSharedPtr<FCamelotInventoryView>, View)
    SLATE_ARGUMENT(int32, Slot)
    SLATE_ARGUMENT(FString, Label)
    SLATE_ARGUMENT(const UCamelotUITheme *, Theme)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
    /// Updates item artwork from the theme, retaining a text fallback for unmapped items.
    void RefreshIcon(const UCamelotUITheme *Theme);
    /// Handles slot/icon presses and requests drag detection when appropriate.
    virtual FReply OnMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
    /// Requests the selected owned item equip or unequip convenience action.
    virtual FReply OnMouseButtonDoubleClick(const FGeometry &, const FPointerEvent &) override;
    /// Creates a drag payload bound to the current source and revision.
    virtual FReply OnDragDetected(const FGeometry &, const FPointerEvent &) override;
    /// Validates the source drag payload before forwarding the slot operation.
    virtual FReply OnDrop(const FGeometry &, const FDragDropEvent &) override;

  private:
    TSharedPtr<FCamelotInventoryView> View;
    int32 Slot = 0, IconModel = INDEX_NONE;
    TStrongObjectPtr<class UTexture2D> Texture;
    FSlateBrush Brush;
};
/// Equipment/backpack interface with server-driven item actions and player preview.
class SCamelotInventoryPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotInventoryPanel) : _Owner(nullptr), _Session(nullptr)
    {
    }
    SLATE_ARGUMENT(ADaocCombatHUD *, Owner)
    SLATE_ARGUMENT(UDaocNetworkSubsystem *, Session)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
    /// Refreshes equipment and backpack slots from the confirmed inventory snapshot.
    void Refresh();
    /// Enables this view or releases resources that are only needed while visible.
    void SetActive(bool Active);

  private:
    TWeakObjectPtr<ADaocCombatHUD> Owner;
    TWeakObjectPtr<UDaocNetworkSubsystem> Session;
    TSharedPtr<FCamelotInventoryView> View;
    TArray<TSharedPtr<SCamelotInventorySlot>> Slots;
    TSharedPtr<SCamelotPlayerPreview> Preview;
    int32 SplitCount = 1;
    /// Builds one equipment or backpack slot with its semantic label.
    TSharedRef<SWidget> MakeSlot(int32 Slot, const TCHAR *Label, const UCamelotUITheme *Theme);
};
