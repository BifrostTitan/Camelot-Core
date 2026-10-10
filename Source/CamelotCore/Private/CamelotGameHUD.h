#pragma once
#include "Widgets/SCompoundWidget.h"
#include "DaocCombatTypes.h"
#include "Styling/SlateBrush.h"
class ADaocCombatHUD;
class SEditableTextBox;
class SScrollBox;
class SVerticalBox;
template <typename ItemType>
class SListView;

/// Slate gameplay interface composing status, hotbars, chat and character panels.
class SCamelotGameHUD : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotGameHUD) : _Owner(nullptr)
    {
    }
    SLATE_ARGUMENT(ADaocCombatHUD *, Owner)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
    /// Refreshes status, hotbars, chat and open character pages from HUD snapshots.
    void Refresh();
    /// Returns the chat-entry widget used for keyboard focus.
    TSharedRef<SWidget> GetChatInput() const;
    /// Toggles character-panel visibility and its cursor interaction state.
    void ToggleCharacterMenu();
    /// Reports whether the character panel is currently visible.
    bool IsCharacterMenuOpen() const
    {
        return bCharacterMenuOpen;
    }
    /// Opens the character menu at the requested page index.
    void ShowCharacterPage(int32 Page);

  private:
    /// Builds one themed hotbar slot using its independent binding index.
    TSharedRef<SWidget> Slot(int32 Index);
    /// Builds display text from the reported local player status.
    FText StatusText() const;
    /// Builds display text for the currently selected target.
    FText TargetText() const;
    /// Returns a reported resource fraction, or unset when unavailable.
    TOptional<float> Resource(int32 Kind) const;
    TWeakObjectPtr<ADaocCombatHUD> Owner;
    TSharedPtr<SEditableTextBox> ChatInput;
    TSharedPtr<SListView<TSharedPtr<FDaocChatMessage>>> ChatList;
    TSharedPtr<SVerticalBox> Effects;
    TSharedPtr<class SCamelotCharacterMenu> CharacterMenu;
    TSharedPtr<class SCamelotTrainerWindow> TrainerWindow;
    bool bCharacterMenuOpen = false;
    /// Returns the number of hotbars currently exposed by the combat component.
    int32 BarCount() const;
    FSlateBrush Icons[24];
    FDaocCombatAbility Abilities[24];
    TArray<TSharedPtr<FDaocChatMessage>> ChatItems;
    int64 LastChatRevision = -1;
    TArray<FDaocCombatEffect> LastEffects;
    FString ChatError;
};
