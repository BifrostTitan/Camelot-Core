#include "DaocAdminPanel.h"
#include "CamelotUITheme.h"
#include "CamelotUIWidgets.h"
#include "DaocCombatHUD.h"
#include "DaocDeveloperComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
struct FCommand
{
    FString Category, Title, Example, Help;
};
const TArray<FCommand> Commands = {{TEXT("npc"), TEXT("Place a saved NPC"), TEXT("/mob fastcreate 408 1 1 Test NPC"),
                                    TEXT("Creates model 408, level 1 at your feet and saves it to the "
                                         "database. Change the model, level and name before sending. The save "
                                         "argument is 1.")},
                                   {TEXT("npc"), TEXT("Spawn an unsaved test NPC"), TEXT("/mob fastcreate 408 1 0 Test NPC"),
                                    TEXT("Creates a live NPC at your feet without immediately saving it. "
                                         "Save=0 is temporary; later editing commands can save it. Newly "
                                         "created fastcreate NPCs may attack nearby players.")},
                                   {TEXT("npc"), TEXT("Place a peaceful NPC"), TEXT("/mob create"),
                                    TEXT("Creates and saves a default NPC at your feet with the peace flag. "
                                         "Select it, then set its model, name and level. Do not repeat the "
                                         "spawn command to select it.")},
                                   {TEXT("npc"), TEXT("Change selected NPC model"), TEXT("/mob model 408"),
                                    TEXT("Requires an NPC target. Uses the DAoC model ID and saves the "
                                         "change. Rendering uses the imported model catalog.")},
                                   {TEXT("npc"), TEXT("Name selected NPC"), TEXT("/mob name Test NPC"),
                                    TEXT("Requires an NPC target. Saves its name; spaces in the name are "
                                         "supported.")},
                                   {TEXT("npc"), TEXT("Set selected NPC level"), TEXT("/mob level 1"), TEXT("Requires an NPC target. Updates and saves its combat level.")},
                                   {TEXT("npc"), TEXT("Move selected NPC here"), TEXT("/mob movehere"),
                                    TEXT("Requires an NPC target. Moves it to your current server position "
                                         "and saves its placement.")},
                                   {TEXT("npc"), TEXT("Set NPC template"), TEXT("/mob npctemplate <template-id>"),
                                    TEXT("Use a server NPC template ID, not a mesh/model ID. Replaces the "
                                         "selected NPC's template, or creates one when no NPC is selected. "
                                         "Replace the placeholder first.")},
                                   {TEXT("npc"), TEXT("Toggle NPC peace flag"), TEXT("/mob peace"),
                                    TEXT("Requires an NPC target. Toggles whether players can attack it. "
                                         "Check the response: this is a toggle, not a fixed on/off setting.")},
                                   {TEXT("teleport"), TEXT("Teleport to coordinates"), TEXT("/jump to <x> <y> <z> <region>"),
                                    TEXT("Use absolute DOL coordinates and a REGION ID, not Unreal "
                                         "coordinates or a zone ID. The destination zone renders through the "
                                         "normal teleport sequence. Use a known safe terrain height.")},
                                   {TEXT("teleport"), TEXT("Teleport to another player"), TEXT("/jump to <player-name>"),
                                    TEXT("Teleports you to an online player. Replace the placeholder with "
                                         "their exact name.")},
                                   {TEXT("teleport"), TEXT("Teleport to target"), TEXT("/jump to target"), TEXT("Requires a selected NPC/player. Uses its server position.")},
                                   {TEXT("teleport"), TEXT("Relative teleport"), TEXT("/jump rel 0 0 100"),
                                    TEXT("Adds X/Y/Z offsets in server units to your current position. This "
                                         "example moves upward by 100 units.")},
                                   {TEXT("teleport"), TEXT("Save return position"), TEXT("/jump push"),
                                    TEXT("Pushes your current position onto DOL's return stack before a "
                                         "teleport.")},
                                   {TEXT("teleport"), TEXT("Return to saved position"), TEXT("/jump pop"), TEXT("Returns to the latest position pushed with /jump push.")},
                                   {TEXT("teleport"), TEXT("Refresh server world"), TEXT("/jump refresh"),
                                    TEXT("Refreshes the server's nearby object replication. For local "
                                         "terrain/fixtures use /client regen instead.")},
                                   {TEXT("weather"), TEXT("Start a random server storm"), TEXT("/weather start"),
                                    TEXT("Starts a DOL storm in the current REGION. It moves across the "
                                         "region; precipitation begins when its front reaches you. The zone "
                                         "profile selects rain or snow.")},
                                   {TEXT("weather"), TEXT("Inspect server storm"), TEXT("/weather info"),
                                    TEXT("Shows the current region's storm position, speed, width and "
                                         "intensity.")},
                                   {TEXT("weather"), TEXT("Stop server storm"), TEXT("/weather stop"),
                                    TEXT("Stops DOL weather in this region. Persistent profile snowfall and "
                                         "local weather overrides continue until separately changed.")},
                                   {TEXT("weather"), TEXT("Preview local snow"), TEXT("/client weather snow 0.6"),
                                    TEXT("Sets snowfall at 60% intensity on this client. Does not change "
                                         "server weather or other clients.")},
                                   {TEXT("weather"), TEXT("Preview local rain"), TEXT("/client weather rain 0.6"),
                                    TEXT("Sets rain at 60% intensity on this client. Works even if this "
                                         "zone's default is sunny.")},
                                   {TEXT("weather"), TEXT("Preview clear skies"), TEXT("/client weather clear"),
                                    TEXT("Temporarily overrides this client's outdoor weather with clear "
                                         "skies.")},
                                   {TEXT("weather"), TEXT("Resume zone weather"), TEXT("/client weather auto"),
                                    TEXT("Clears the local override. The zone's persistent, server or cycle "
                                         "policy resumes.")},
                                   {TEXT("player"), TEXT("Heal target or self"), TEXT("/heal"),
                                    TEXT("DOL heals the selected living target, or you when no living target "
                                         "is selected.")},
                                   {TEXT("player"), TEXT("Heal yourself"), TEXT("/heal me"),
                                    TEXT("Restores your health, mana and endurance even when an NPC is "
                                         "selected.")},
                                   {TEXT("player"), TEXT("List all permitted commands"), TEXT("/cmdhelp"),
                                    TEXT("Requests the DOL command registry. Your server account privilege "
                                         "still determines which commands may execute.")},
                                   {TEXT("local"), TEXT("Toggle flight"), TEXT("/client fly"),
                                    TEXT("Toggles server-authorized developer flight. Space ascends and Ctrl "
                                         "descends after closing the panel.")},
                                   {TEXT("local"), TEXT("Toggle fixture inspector"), TEXT("/client inspect"),
                                    TEXT("Toggles the debug hammer and fixture ID/NIF laser. Close the panel "
                                         "to aim it.")},
                                   {TEXT("local"), TEXT("Regenerate current zone"), TEXT("/client regen"),
                                    TEXT("Rebuilds local terrain, fixtures and grass from current settings. "
                                         "Does not teleport or edit the server database.")}};
class SDaocAdminPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SDaocAdminPanel)
    {
    }
    SLATE_ARGUMENT(UDaocDeveloperComponent *, Developer)
    SLATE_END_ARGS()
    void Construct(const FArguments &Args)
    {
        Developer = Args._Developer;
        const auto *Pawn = Developer.IsValid() ? Cast<APawn>(Developer->GetOwner()) : nullptr;
        const auto *PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
        const auto *HUD = PC ? Cast<ADaocCombatHUD>(PC->GetHUD()) : nullptr;
        const auto *Theme = HUD ? HUD->GetUITheme() : GetDefault<UCamelotUITheme>();
        auto Button = [this](const TCHAR *Label, TFunction<void()> Action) {
            return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([Action] {
                Action();
                return FReply::Handled();
            });
        };
        auto Tabs = SNew(SHorizontalBox);
        for (const auto &Tab : TArray<TPair<FString, FString>>{{TEXT("npc"), TEXT("NPCs")}, {TEXT("teleport"), TEXT("Teleport")}, {TEXT("weather"), TEXT("Weather")}, {TEXT("player"), TEXT("Players")}, {TEXT("local"), TEXT("Local tools")}})
        {
            const FString Key = Tab.Key;
            Tabs->AddSlot().AutoWidth().Padding(2)[Button(*Tab.Value, [this, Key] { ShowCategory(Key); })];
        }
        Tabs->AddSlot().FillWidth(1);
        Tabs->AddSlot().AutoWidth()[Button(TEXT("Close (Esc)"), [this] {
            if (auto *D = Developer.Get())
                D->CloseAdminPanel();
        })];
        ChildSlot[SNew(SBorder)
                      .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                      .BorderBackgroundColor(FLinearColor(0, 0, 0, .8f))
                      .Padding(20)
                      .HAlign(HAlign_Center)
                      .VAlign(VAlign_Center)[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SBox).WidthOverride(920).HeightOverride(650)[SNew(SCamelotPanel)
                                                                                                                                                 .Theme(Theme)
                                                                                                                                                 .Title(FText::FromString(TEXT("ADMINISTRATION")))[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] {
                                                                                                                                                     auto *D = Developer.Get();
                                                                                                                                                     return FText::FromString(D ? D->GetAdminContext() : TEXT("Disconnected"));
                                                                                                                                                 })] + SVerticalBox::Slot().AutoHeight()[Tabs] +
                                                                                                                                                                                                   SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(SHorizontalBox) +
                                                                                                                                                                                                                                                   SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Cycle NPC target"),
                                                                                                                                                                                                                                                                                             [this] {
                                                                                                                                                                                                                                                                                                 if (auto *D = Developer.Get())
                                                                                                                                                                                                                                                                                                     D->CycleAdminTarget();
                                                                                                                                                                                                                                                                                             })] +
                                                                                                                                                                                                                                                   SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("Ask server for topic help"),
                                                                                                                                                                                                                                                                                                           [this] {
                                                                                                                                                                                                                                                                                                               if (auto *D = Developer.Get())
                                                                                                                                                                                                                                                                                                                   D->ExecuteAdminCommand(TEXT("/adminhelp ") + Category);
                                                                                                                                                                                                                                                                                                           })]] +
                                                                                                                                                                                                   SVerticalBox::Slot().FillHeight(1).Padding(0, 5)[SAssignNew(CommandList, SScrollBox)] +
                                                                                                                                                                                                   SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                                                                                                                                                                                                                                         .AutoWrapText(true)
                                                                                                                                                                                                                                         .Text(FText::FromString(TEXT("Choose a command to fill the box, "
                                                                                                                                                                                                                                                                      "edit its arguments, then Send or "
                                                                                                                                                                                                                                                                      "press Enter. NPC editing commands "
                                                                                                                                                                                                                                                                      "can save database changes.")))] +
                                                                                                                                                                                                   SVerticalBox::Slot().AutoHeight().Padding(0, 5)[SNew(SHorizontalBox) +
                                                                                                                                                                                                                                                   SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Input, SEditableTextBox)
                                                                                                                                                                                                                                                                                           .HintText(FText::FromString(TEXT("/adminhelp, /mob ..., "
                                                                                                                                                                                                                                                                                                                            "/jump ...")))
                                                                                                                                                                                                                                                                                           .OnTextCommitted_Lambda([this](const FText &, ETextCommit::Type Type) {
                                                                                                                                                                                                                                                                                               if (Type == ETextCommit::OnEnter)
                                                                                                                                                                                                                                                                                                   Execute();
                                                                                                                                                                                                                                                                                           })
                                                                                                                                                                                                                                                                                           .OnKeyDownHandler_Lambda([this](const FGeometry &, const FKeyEvent &Event) {
                                                                                                                                                                                                                                                                                               if (Event.GetKey() == EKeys::Escape)
                                                                                                                                                                                                                                                                                               {
                                                                                                                                                                                                                                                                                                   if (auto *D = Developer.Get())
                                                                                                                                                                                                                                                                                                       D->CloseAdminPanel();
                                                                                                                                                                                                                                                                                                   return FReply::Handled();
                                                                                                                                                                                                                                                                                               }
                                                                                                                                                                                                                                                                                               return FReply::Unhandled();
                                                                                                                                                                                                                                                                                           })] +
                                                                                                                                                                                                                                                   SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("Send"), [this] { Execute(); })]] +
                                                                                                                                                                                                   SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                                                                                                                                                                                                                                         .Text(FText::FromString(TEXT("Server responses and command "
                                                                                                                                                                                                                                                                      "history")))] +
                                                                                                                                                                                                   SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(150)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] {
                                                                                                                                                                                                                                                                        auto *D = Developer.Get();
                                                                                                                                                                                                                                                                        return FText::FromString(D ? D->GetAdminLog() : FString());
                                                                                                                                                                                                                                                                    })]]]]]]]];
        ShowCategory(TEXT("npc"));
    }
    virtual bool SupportsKeyboardFocus() const override
    {
        return true;
    }
    virtual FReply OnKeyDown(const FGeometry &Geometry, const FKeyEvent &Event) override
    {
        if (Event.GetKey() == EKeys::Escape)
        {
            if (auto *D = Developer.Get())
                D->CloseAdminPanel();
            return FReply::Handled();
        }
        return SCompoundWidget::OnKeyDown(Geometry, Event);
    }

  private:
    void ShowCategory(const FString &Key)
    {
        Category = Key;
        CommandList->ClearChildren();
        for (const auto &C : Commands)
            if (C.Category == Key)
            {
                const FString Example = C.Example;
                CommandList->AddSlot().Padding(0, 3)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(C.Title + TEXT("  ") + C.Example)).OnClicked_Lambda([this, Example] {
                    Input->SetText(FText::FromString(Example));
                    return FReply::Handled();
                })] + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(C.Help))]];
            }
    }
    void Execute()
    {
        if (auto *D = Developer.Get())
            D->ExecuteAdminCommand(Input->GetText().ToString());
    }
    TWeakObjectPtr<UDaocDeveloperComponent> Developer;
    TSharedPtr<SScrollBox> CommandList;
    TSharedPtr<SEditableTextBox> Input;
    FString Category;
};
} // namespace
TSharedRef<SWidget> CreateDaocAdminPanel(UDaocDeveloperComponent *Developer)
{
    return SNew(SDaocAdminPanel).Developer(Developer);
}
