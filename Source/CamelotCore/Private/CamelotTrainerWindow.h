#pragma once
#include "Widgets/SCompoundWidget.h"
#include "DaocTrainerTypes.h"
class ADaocCombatHUD;
class SVerticalBox;
class SWidgetSwitcher;

/// Server-driven specialization and realm-ability allocation interface.
class SCamelotTrainerWindow : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotTrainerWindow) : _Owner(nullptr)
    {
    }
    SLATE_ARGUMENT(ADaocCombatHUD *, Owner)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
    /// Refreshes training choices, point balances and pending-request feedback.
    void Refresh(const FDaocTrainerState &NewState);
    /// Reports whether this trainer view is open.
    bool IsOpen() const
    {
        return State.bOpen;
    }
    /// Allows this widget to receive keyboard focus.
    virtual bool SupportsKeyboardFocus() const override
    {
        return true;
    }
    /// Handles trainer keyboard interaction, including closing on Escape.
    virtual FReply OnKeyDown(const FGeometry &Geometry, const FKeyEvent &Event) override;

  private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FCamelotUIRenderTest;
#endif
    TWeakObjectPtr<ADaocCombatHUD> Owner;
    FDaocTrainerState State;
    TSharedPtr<SVerticalBox> SpecRows, RealmRows;
    TSharedPtr<SWidgetSwitcher> Pages;
    TMap<int32, int32> DesiredRanks;
    /// Rebuilds training choices for the selected specialization or realm-ability category.
    void RebuildRows(bool Realm);
    /// Returns the selected allocation clamped to the current server-driven limit.
    int32 Desired(const FDaocTrainerLine &Line, bool Realm) const;
    /// Queues a revision-checked training allocation for the active nearby trainer.
    FReply Train(const FDaocTrainerLine &Line, bool Realm);
};
