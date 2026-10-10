#include "CamelotInventoryUI.h"
#include "Camelot/CamelotInventory.h"
#include "CamelotPlayerPreview.h"
#include "CamelotUITheme.h"
#include "DaocCombatHUD.h"
#include "DaocNetworkSubsystem.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

bool FCamelotInventoryView::Move(int32 From, int32 To, int32 Count, int64 Revision)
{
    const bool Sent = Session.IsValid() && Session->RequestMoveItem(From, To, Count, Revision);
    Feedback = Sent ? TEXT("") : TEXT("Item changed or connection unavailable. Try again.");
    return Sent;
}
bool FCamelotInventoryView::EquipOrRemove(int32 Slot)
{
    if (!Item(Slot))
        return false;
    if (Slot >= 40)
        return Move(Slot, 100, 0, State.Revision);
    if (!State.bBackpackKnown)
    {
        Feedback = TEXT("Backpack is loading.");
        return false;
    }
    for (int32 I = 40; I <= 79; ++I)
        if (!Item(I))
            return Move(Slot, I, 0, State.Revision);
    Feedback = TEXT("Your backpack is full.");
    return false;
}
TSharedRef<FCamelotItemDrag> FCamelotItemDrag::New(UDaocNetworkSubsystem *S, int32 I, int64 R, const FString &N)
{
    auto Op = MakeShared<FCamelotItemDrag>();
    Op->Session = S;
    Op->Slot = I;
    Op->Revision = R;
    Op->Name = N;
    Op->bCreateNewWindow = false;
    Op->Construct();
    return Op;
}
TSharedPtr<SWidget> FCamelotItemDrag::GetDefaultDecorator() const
{
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f, .035f, .05f, 1)).Padding(10)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text(FText::FromString(Name))];
}
void SCamelotInventorySlot::Construct(const FArguments &A)
{
    View = A._View;
    Slot = A._Slot;
    const auto *Theme = A._Theme ? A._Theme : GetDefault<UCamelotUITheme>();
    Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
    SetToolTipText(TAttribute<FText>::CreateLambda([this, Label = A._Label] {
        const auto *I = View->Item(Slot);
        return FText::FromString(I ? FString::Printf(TEXT("%s\nLevel %d · Quality %d%%\nCondition %d%% · Durability "
                                                          "%d%%\nBonus %d%% · Weight %.1f lb"),
                                                     *I->Name, I->Level, I->Quality, I->Condition, I->Durability, I->Bonus, I->Weight / 10.f)
                                   : Label);
    }));
    ChildSlot[SNew(SBox).HeightOverride(34)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor_Lambda([this, Theme] { return View->Selected == Slot ? Theme->Accent : Theme->Border; }).Padding(1)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Theme->Recess).Padding(4)[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(26)[SNew(SImage).Image(&Brush).Visibility_Lambda([this] { return Texture.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })]] + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 10)).ColorAndOpacity(Theme->Text).Text_Lambda([this, Label = A._Label] {
        const auto *I = View->Item(Slot);
        return FText::FromString(I ? (Texture.IsValid() ? FString() : I->Name.Left(16)) : Label);
    })] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).ColorAndOpacity(Theme->Accent).Text_Lambda([this] {
        const auto *I = View->Item(Slot);
        return I && I->Count > 1 && Texture.IsValid() ? FText::AsNumber(I->Count) : FText();
    })]]]]];
    RefreshIcon(Theme);
}
void SCamelotInventorySlot::RefreshIcon(const UCamelotUITheme *Theme)
{
    const auto *I = View->Item(Slot);
    const int32 Model = I ? I->Model : INDEX_NONE;
    if (IconModel == Model)
        return;
    IconModel = Model;
    Texture.Reset();
    Brush.SetResourceObject(nullptr);
    Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
    if (const auto *Asset = Theme->ItemIcons.Find(Model))
        Texture.Reset(Asset->LoadSynchronous());
    if (Texture.IsValid())
    {
        Brush.SetResourceObject(Texture.Get());
        Brush.DrawAs = ESlateBrushDrawType::Image;
        Brush.ImageSize = FVector2D(26, 26);
    }
}
FReply SCamelotInventorySlot::OnMouseButtonDown(const FGeometry &, const FPointerEvent &E)
{
    if (E.GetEffectingButton() != EKeys::LeftMouseButton)
        return FReply::Unhandled();
    View->Selected = Slot;
    View->Feedback.Reset();
    return View->Item(Slot) ? FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton) : FReply::Handled();
}
FReply SCamelotInventorySlot::OnMouseButtonDoubleClick(const FGeometry &, const FPointerEvent &E)
{
    if (E.GetEffectingButton() != EKeys::LeftMouseButton)
        return FReply::Unhandled();
    View->EquipOrRemove(Slot);
    return FReply::Handled();
}
FReply SCamelotInventorySlot::OnDragDetected(const FGeometry &, const FPointerEvent &)
{
    const auto *I = View->Item(Slot);
    return I && View->Session.IsValid() ? FReply::Handled().BeginDragDrop(FCamelotItemDrag::New(View->Session.Get(), Slot, View->State.Revision, I->Name)) : FReply::Unhandled();
}
FReply SCamelotInventorySlot::OnDrop(const FGeometry &, const FDragDropEvent &E)
{
    const auto Op = E.GetOperationAs<FCamelotItemDrag>();
    if (Op.IsValid() && View->Session.IsValid() && Op->Session == View->Session)
    {
        View->Move(Op->Slot, Slot, 0, Op->Revision);
        return FReply::Handled();
    }
    return FReply::Unhandled();
}
TSharedRef<SWidget> SCamelotInventoryPanel::MakeSlot(int32 Slot, const TCHAR *Label, const UCamelotUITheme *Theme)
{
    auto Widget = SNew(SCamelotInventorySlot).View(View).Slot(Slot).Label(FString(Label)).Theme(Theme);
    Slots.Add(Widget);
    return Widget;
}
void SCamelotInventoryPanel::Construct(const FArguments &A)
{
    Owner = A._Owner;
    Session = A._Session;
    View = MakeShared<FCamelotInventoryView>();
    const auto *Theme = Owner.IsValid() ? Owner->GetUITheme() : GetDefault<UCamelotUITheme>();
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", Theme->FontSize);
    auto Left = SNew(SVerticalBox), Right = SNew(SVerticalBox);
    const int32 L[] = {21, 25, 28, 22, 27, 23, 26};
    const TCHAR *LN[] = {TEXT("Head"), TEXT("Chest"), TEXT("Arms"), TEXT("Hands"), TEXT("Legs"), TEXT("Feet"), TEXT("Cloak")};
    const int32 R[] = {29, 32, 24, 37, 33, 34, 35, 36};
    const TCHAR *RN[] = {TEXT("Neck"), TEXT("Waist"), TEXT("Jewel"), TEXT("Mythical"), TEXT("Left wrist"), TEXT("Right wrist"), TEXT("Left ring"), TEXT("Right ring")};
    for (int32 I = 0; I < 7; ++I)
        Left->AddSlot().AutoHeight().Padding(0, 1)[MakeSlot(L[I], LN[I], Theme)];
    for (int32 I = 0; I < 8; ++I)
        Right->AddSlot().AutoHeight().Padding(0, 1)[MakeSlot(R[I], RN[I], Theme)];
    auto Weapons = SNew(SUniformGridPanel).SlotPadding(FMargin(2));
    const TCHAR *W[] = {TEXT("Right hand"), TEXT("Left hand"), TEXT("Two-handed"), TEXT("Ranged"), TEXT("Quiver 1"), TEXT("Quiver 2"), TEXT("Quiver 3"), TEXT("Quiver 4")};
    for (int32 I = 0; I < 8; ++I)
        Weapons->AddSlot(I, 0)[MakeSlot(10 + I, W[I], Theme)];
    auto Bag = SNew(SUniformGridPanel).SlotPadding(FMargin(2));
    for (int32 I = 0; I < 40; ++I)
        Bag->AddSlot(I % 8, I / 8)[MakeSlot(40 + I, TEXT(""), Theme)];
    auto Actions = SNew(SHorizontalBox);
    Actions->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[SNew(SButton).IsFocusable(false).Text(FText::FromString(TEXT("Details"))).IsEnabled_Lambda([this] { return View->Item(View->Selected) != nullptr; }).OnClicked_Lambda([this] {
        const bool Sent = View->Session.IsValid() && View->Session->RequestInspectItem(View->Selected, View->State.Revision);
        View->Feedback = Sent ? TEXT("")
                              : TEXT("Details are pending or the item "
                                     "changed. Try again shortly.");
        return FReply::Handled();
    })];
    Actions->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[SNew(SButton).IsFocusable(false).Text_Lambda([this] { return FText::FromString(View->Selected >= 40 ? TEXT("Equip") : TEXT("Unequip")); }).IsEnabled_Lambda([this] { return View->Item(View->Selected) != nullptr; }).OnClicked_Lambda([this] {
        View->EquipOrRemove(View->Selected);
        return FReply::Handled();
    })];
    for (int32 Use = 0; Use < 3; ++Use)
        Actions->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[SNew(SButton)
                                                               .IsFocusable(false)
                                                               .Text(FText::FromString(Use == 0   ? TEXT("Use")
                                                                                       : Use == 1 ? TEXT("Use 1")
                                                                                                  : TEXT("Use 2")))
                                                               .IsEnabled_Lambda([this] { return View->Item(View->Selected) != nullptr; })
                                                               .OnClicked_Lambda([this, Use] {
                                                                   if (View->Session.IsValid() && !View->Session->RequestUseItem(View->Selected, Use, View->State.Revision))
                                                                       View->Feedback = TEXT("Item changed or connection unavailable.");
                                                                   return FReply::Handled();
                                                               })];
    Actions->AddSlot().AutoWidth()[SNew(SSpinBox<int32>)
                                       .MinValue(1)
                                       .MaxValue_Lambda([this] {
                                           const auto *I = View->Item(View->Selected);
                                           return I ? FMath::Max(1, I->Count) : 1;
                                       })
                                       .Value_Lambda([this] {
                                           const auto *I = View->Item(View->Selected);
                                           return FMath::Clamp(SplitCount, 1, I ? FMath::Max(1, I->Count) : 1);
                                       })
                                       .OnValueChanged_Lambda([this](int32 N) { SplitCount = N; })];
    Actions->AddSlot().AutoWidth().Padding(6, 0, 0, 0)[SNew(SButton)
                                                           .IsFocusable(false)
                                                           .Text(FText::FromString(TEXT("Split")))
                                                           .IsEnabled_Lambda([this] {
                                                               const auto *I = View->Item(View->Selected);
                                                               return I && I->Count > 1 && View->State.bBackpackKnown;
                                                           })
                                                           .OnClicked_Lambda([this] {
                                                               const auto *Item = View->Item(View->Selected);
                                                               if (Item)
                                                                   for (int32 I = 40; I <= 79; ++I)
                                                                       if (!View->Item(I))
                                                                       {
                                                                           View->Move(View->Selected, I, FMath::Clamp(SplitCount, 1, Item->Count), View->State.Revision);
                                                                           return FReply::Handled();
                                                                       }
                                                               View->Feedback = TEXT("Your backpack is full.");
                                                               return FReply::Handled();
                                                           })];
    ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[Left] + SHorizontalBox::Slot().FillWidth(1.5f).Padding(8, 0)[SAssignNew(Preview, SCamelotPlayerPreview).Owner(Owner.Get()).Session(Session.Get())] + SHorizontalBox::Slot().FillWidth(1)[Right]] + SVerticalBox::Slot().AutoHeight().Padding(0, 8)[Weapons] + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Accent).Text_Lambda([this] { return FText::FromString(View->State.bBackpackKnown ? FString::Printf(TEXT("Backpack · %d / 40"), View->State.Items.FilterByPredicate([](const auto &I) { return I.Slot >= 40; }).Num()) : TEXT("Backpack · Loading")); })] + SVerticalBox::Slot().AutoHeight()[Bag] + SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).AutoWrapText(true).Text_Lambda([this] {
                                                                          const auto *I = View->Item(View->Selected);
                                                                          return I ? FText::FromString(FString::Printf(TEXT("%s\nLevel %d · Quality "
                                                                                                                            "%d%% · Condition %d%% · "
                                                                                                                            "Durability %d%%\nBonus "
                                                                                                                            "%d%% · Weight %.1f lb"),
                                                                                                                       *I->Name, I->Level, I->Quality, I->Condition, I->Durability, I->Bonus, I->Weight / 10.f))
                                                                                   : FText();
                                                                      })] + SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Text).AutoWrapText(true).Text_Lambda([this] { return View->Selected == View->State.DetailSlot ? FText::FromString(View->State.DetailText) : FText(); })]]] +
              SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Actions] + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(STextBlock).Font(Font).ColorAndOpacity(Theme->Accent).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(View->Feedback); })]];
    Refresh();
}
void SCamelotInventoryPanel::Refresh()
{
    auto *Net = Session.IsValid() ? Session.Get() : Owner.IsValid() ? UDaocNetworkSubsystem::Get(Owner.Get())
                                                                    : nullptr;
    View->Session = Net;
    const auto State = Net ? Net->GetInventoryState() : FDaocInventoryState();
    if (State.Revision != View->State.Revision || State.Items.Num() != View->State.Items.Num())
    {
        View->State = State;
        const auto *Theme = Owner.IsValid() ? Owner->GetUITheme() : GetDefault<UCamelotUITheme>();
        for (auto &Slot : Slots)
            Slot->RefreshIcon(Theme);
    }
    View->State.DetailSlot = State.DetailSlot;
    View->State.DetailText = State.DetailText;
    Preview->Refresh();
}
void SCamelotInventoryPanel::SetActive(bool Active)
{
    Preview->SetActive(Active);
    if (Active)
        Refresh();
}
