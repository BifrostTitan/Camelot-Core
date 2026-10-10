#include "CamelotCharacterMenu.h"
#include "CamelotAbilityDrag.h"
#include "CamelotInventoryUI.h"
#include "CamelotUITheme.h"
#include "DaocCombatComponent.h"
#include "DaocCombatHUD.h"
#include "DaocNetworkSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const TCHAR *KindName(EDaocAbilityKind Kind)
{
    switch (Kind)
    {
    case EDaocAbilityKind::Specialization:
        return TEXT("Specializations");
    case EDaocAbilityKind::Style:
        return TEXT("Styles");
    case EDaocAbilityKind::Spell:
    case EDaocAbilityKind::ListSpell:
        return TEXT("Spells");
    case EDaocAbilityKind::Song:
        return TEXT("Songs");
    case EDaocAbilityKind::RealmAbility:
        return TEXT("Realm abilities");
    default:
        return TEXT("Abilities");
    }
}
} // namespace
void SCamelotCharacterMenu::Construct(const FArguments &Args)
{
    Owner = Args._Owner;
    const auto *Theme = Owner.IsValid() ? Owner->GetUITheme() : GetDefault<UCamelotUITheme>();
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", Theme->FontSize);
    auto Tabs = SNew(SHorizontalBox);
    const TCHAR *Titles[] = {TEXT("Character"), TEXT("Skills"), TEXT("Effects"), TEXT("Equipment")};
    for (int32 I = 0; I < 4; ++I)
        Tabs->AddSlot().FillWidth(1).Padding(0, 0, 4, 0)[SNew(SButton).IsFocusable(false).ButtonColorAndOpacity_Lambda([this, I, Theme] { return GetPage() == I ? Theme->Accent : Theme->Border; }).OnClicked_Lambda([this, I] {
            SelectPage(I);
            return FReply::Handled();
        })[SNew(STextBlock).Font(Font).Text(FText::FromString(Titles[I]))]];
    ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Tabs] + SVerticalBox::Slot().FillHeight(1)[SAssignNew(Pages, SWidgetSwitcher) + SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).WrapTextAt(360).Text_Lambda([this] { return FText::FromString(CharacterDetails); })]] + SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(SkillRows, SVerticalBox)]] + SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(EffectRows, SVerticalBox)]] + SWidgetSwitcher::Slot()[SAssignNew(Inventory, SCamelotInventoryPanel).Owner(Owner.Get())]]];
    Refresh();
}
void SCamelotCharacterMenu::SelectPage(int32 Page)
{
    Pages->SetActiveWidgetIndex(FMath::Clamp(Page, 0, 3));
    Inventory->SetActive(bMenuOpen && GetPage() == 3);
}
void SCamelotCharacterMenu::SetMenuOpen(bool Open)
{
    bMenuOpen = Open;
    Inventory->SetActive(Open && GetPage() == 3);
}
int32 SCamelotCharacterMenu::GetPage() const
{
    return Pages.IsValid() ? Pages->GetActiveWidgetIndex() : 0;
}
void SCamelotCharacterMenu::Refresh()
{
    if (bMenuOpen && GetPage() == 3)
        Inventory->Refresh();
    const auto *Theme = Owner.IsValid() ? Owner->GetUITheme() : GetDefault<UCamelotUITheme>();
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", Theme->FontSize);
    auto *Combat = Owner.IsValid() ? Owner->GetCombat() : nullptr;
    const auto *Net = Owner.IsValid() ? UDaocNetworkSubsystem::Get(Owner.Get()) : nullptr;
    CharacterDetails.Reset();
    if (Net && Net->bWorldReady)
    {
        const auto *Appearance = Net->PlayerAppearances.Find(Net->PlayerObjectId);
        const FString Name = Appearance && !Appearance->Name.IsEmpty() ? Appearance->Name : Net->CharacterName;
        CharacterDetails = Name;
        // Class/race labels and level are a login snapshot, not inferred live
        // attributes.
        for (const auto &Slot : Net->CharacterSlots)
            if (Slot.Appearance.Name.Equals(Name, ESearchCase::IgnoreCase))
            {
                CharacterDetails += FString::Printf(TEXT("\n\nLevel %d\nClass: %s\nRace: %s"), Slot.Appearance.Level, *Slot.ClassName, *Slot.RaceName);
                break;
            }
    }
    else
        CharacterDetails = TEXT("Character");
    const auto State = Combat ? Combat->GetState() : FDaocCombatState();
    if (State.bProgressKnown)
        CharacterDetails += FString::Printf(TEXT("\n\nPROGRESS\nExperience: %.1f%%\nSpecialization points: "
                                                 "%d\nRealm ability points: %d\nRealm points: %lld\nBounty points: "
                                                 "%lld"),
                                            State.ExperiencePercent, State.SkillPoints, State.RealmSkillPoints, State.RealmPoints, State.BountyPoints);
    if (State.bStatusKnown)
        CharacterDetails += FString::Printf(TEXT("\n\nRESOURCES\nHealth: %d%%\nMana: "
                                                 "%d%%\nEndurance: %d%%\nConcentration: %d%%"),
                                            State.HealthPercent, State.ManaPercent, State.EndurancePercent, State.ConcentrationPercent);

    const auto Current = Combat ? Combat->GetLearnedAbilities() : TArray<FDaocCombatAbility>();
    bool Changed = Current.Num() != LastSkills.Num();
    for (int32 I = 0; !Changed && I < Current.Num(); ++I)
        Changed = Current[I].Kind != LastSkills[I].Kind || Current[I].ServerIndex != LastSkills[I].ServerIndex || Current[I].SpellLine != LastSkills[I].SpellLine || Current[I].Name != LastSkills[I].Name || Current[I].LineName != LastSkills[I].LineName || Current[I].Level != LastSkills[I].Level || Current[I].Bonus != LastSkills[I].Bonus || Current[I].Icon != LastSkills[I].Icon;
    if (Changed || SkillRows->NumSlots() == 0)
    {
        LastSkills = Current;
        SkillRows->ClearChildren();
        if (Current.IsEmpty())
            SkillRows->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Muted).Text(FText::FromString(TEXT("No skills available.")))];
        TArray<FDaocCombatAbility> Sorted = Current;
        Sorted.StableSort([](const auto &A, const auto &B) { return A.Kind == B.Kind ? A.LineName < B.LineName : uint8(A.Kind) < uint8(B.Kind); });
        FString Heading;
        for (const auto &Ability : Sorted)
        {
            const FString Next = KindName(Ability.Kind);
            if (Heading != Next)
            {
                Heading = Next;
                SkillRows->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", Theme->FontSize)).ColorAndOpacity(Theme->Accent).Text(FText::FromString(Heading))];
            }
            const FString Bonus = Ability.Bonus ? FString::Printf(TEXT(" %+d"), Ability.Bonus) : FString();
            SkillRows->AddSlot().AutoHeight().Padding(0, 4)[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[SNew(SCamelotAbilityIcon).Ability(Ability).Source(Combat).Theme(Theme)] + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(10, 0)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).WrapTextAt(250).Text(FText::FromString(Ability.Name)).ToolTipText(FText::FromString(Ability.LineName))] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Muted).Text(FText::FromString(FString::Printf(TEXT("%d%s"), Ability.Level, *Bonus)))]];
        }
    }
    const auto Effects = Combat ? Combat->GetEffects() : TArray<FDaocCombatEffect>();
    Changed = Effects.Num() != LastEffects.Num();
    for (int32 I = 0; !Changed && I < Effects.Num(); ++I)
        Changed = Effects[I].InternalId != LastEffects[I].InternalId || Effects[I].Name != LastEffects[I].Name || Effects[I].ReportedSeconds != LastEffects[I].ReportedSeconds || Effects[I].bCancelable != LastEffects[I].bCancelable;
    if (Changed || EffectRows->NumSlots() == 0)
    {
        LastEffects = Effects;
        EffectRows->ClearChildren();
        if (Effects.IsEmpty())
            EffectRows->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Muted).Text(FText::FromString(TEXT("No effects reported.")))];
        for (const auto &E : Effects)
            EffectRows->AddSlot().AutoHeight().Padding(0, 4)[SNew(SButton).IsFocusable(false).IsEnabled(E.bCancelable).ToolTipText(FText::FromString(E.bCancelable ? TEXT("Click to request cancellation.") : TEXT("This effect cannot be canceled."))).OnClicked_Lambda([this, Id = E.InternalId] {
                if (Owner.IsValid())
                    if (auto *C = Owner->GetCombat())
                        C->CancelEffect(Id);
                return FReply::Handled();
            })[SNew(STextBlock).Font(Font).WrapTextAt(340).Text(FText::FromString(FString::Printf(TEXT("%s\nLast reported duration: %ds"), *E.Name, E.ReportedSeconds)))]];
    }
}
