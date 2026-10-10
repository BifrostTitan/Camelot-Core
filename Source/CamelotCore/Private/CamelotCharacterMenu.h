#pragma once
#include "Widgets/SCompoundWidget.h"
#include "DaocCombatTypes.h"
class ADaocCombatHUD;
class SWidgetSwitcher;
class SVerticalBox;

/// Pages present existing session data; they do not simulate stats or training.
class SCamelotCharacterMenu : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotCharacterMenu) : _Owner(nullptr)
    {
    }
    SLATE_ARGUMENT(ADaocCombatHUD *, Owner)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
    /// Refreshes character, skill and equipment pages from confirmed player state.
    void Refresh();
    /// Selects a character-menu page using its widget-switcher index.
    void SelectPage(int32 Page);
    /// Returns the current character-menu page index.
    int32 GetPage() const;
    /// Changes menu visibility and activates only resources needed by its current page.
    void SetMenuOpen(bool Open);

  private:
    TWeakObjectPtr<ADaocCombatHUD> Owner;
    TSharedPtr<SWidgetSwitcher> Pages;
    TSharedPtr<SVerticalBox> SkillRows, EffectRows;
    TArray<FDaocCombatAbility> LastSkills;
    TArray<FDaocCombatEffect> LastEffects;
    FString CharacterDetails;
    TSharedPtr<class SCamelotInventoryPanel> Inventory;
    bool bMenuOpen = false;
};
