#include "DaocFrontendHUD.h"
#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DaocCharacterSizing.h"
#include "DaocEquipmentCatalog.h"
#include "DaocEquipmentComponent.h"
#include "DaocFrontendTheme.h"
#include "DaocNetworkSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
struct RaceChoice
{
    int32 Id = 0, Realm = 0, Male = 0, Female = 0;
    FString Name;
    TArray<int32> Stats;
};
struct ClassChoice
{
    int32 Id = 0;
    FString Name;
    TArray<int32> Races;
    int32 Gender = -1;
};
int32 Points(const TArray<int32> &Base, const TArray<int32> &Values)
{
    if (Base.Num() != 8 || Values.Num() != 8)
        return -1;
    int32 Total = 0;
    for (int32 I = 0; I < 8; ++I)
    {
        const int32 A = Values[I] - Base[I];
        if (A < 0)
            return -1;
        Total += A + FMath::Max(0, A - 10) + FMath::Max(0, A - 15);
    }
    return Total;
}
} // namespace
class SDaocFrontend : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SDaocFrontend)
    {
    }
    SLATE_ARGUMENT(ADaocFrontendHUD *, Owner)
    SLATE_END_ARGS()
    void Construct(const FArguments &Args)
    {
        Owner = Args._Owner;
        ReadRules();
        ChildSlot[SAssignNew(Content, SBox)];
        SetPage(TEXT("Login"));
#if !UE_BUILD_SHIPPING
        FString PreviewPage;
        if (FParse::Value(FCommandLine::Get(), TEXT("CamelotFrontendPreview="), PreviewPage))
        {
            Draft = FDaocCreationRequest();
            Draft.Appearance.Realm = Session() ? Session()->Realm : 1;
            Draft.Appearance.EyeSize = Draft.Appearance.LipSize = 0x55;
            Draft.Appearance.EyeColor = 0x10;
            Draft.Appearance.HairColor = 0xb0;
            for (const auto &R : Races)
                if (R.Realm == Draft.Appearance.Realm)
                {
                    ChooseRace(R.Id);
                    break;
                }
            int32 PreviewRace = 0;
            if (FParse::Value(FCommandLine::Get(), TEXT("CamelotPreviewRace="), PreviewRace))
                ChooseRace(PreviewRace);
            if (PreviewPage == TEXT("Creation") || PreviewPage == TEXT("Customize") || PreviewPage == TEXT("Location") || PreviewPage == TEXT("Realm") || PreviewPage == TEXT("Login"))
                SetPage(PreviewPage);
        }
#endif
    }
    void Update()
    {
        auto *S = Session();
        if (!S)
            return;
#if !UE_BUILD_SHIPPING
        FString PreviewPage;
        if (FParse::Value(FCommandLine::Get(), TEXT("CamelotFrontendPreview="), PreviewPage))
            return;
#endif
        if (S->ConnectionState == TEXT("Failed"))
        {
            Feedback = S->LastNetworkError;
            if (Page != TEXT("Login"))
                SetPage(TEXT("Login"));
            return;
        }
        if (S->ConnectionState == TEXT("Characters"))
        {
            if (Page == TEXT("Connecting") && S->AllowedRealm == 0)
                SetPage(TEXT("Realm"));
            if (S->bCharactersReady && (Page == TEXT("Connecting") || Page == TEXT("Waiting")))
            {
                if (!PendingName.IsEmpty())
                    SelectedName = PendingName;
                PendingName.Reset();
                Feedback = S->LastNetworkError;
                SetPage(TEXT("Characters"));
            }
            if (Page == TEXT("Characters") && SeenRoster != S->RosterRevision)
                SetPage(TEXT("Characters"));
        }
        else if (S->ConnectionState != TEXT("Disconnected") && Page == TEXT("Login"))
            SetPage(TEXT("Connecting"));
        if (S->ConnectionState == TEXT("Selecting") || S->ConnectionState == TEXT("Opening") || S->ConnectionState == TEXT("World") || S->ConnectionState == TEXT("Initializing") || S->ConnectionState == TEXT("Loading") || S->ConnectionState == TEXT("LoadingFailed"))
        {
            if (Page != TEXT("Entering"))
                SetPage(TEXT("Entering"));
        }
    }

  private:
    TWeakObjectPtr<ADaocFrontendHUD> Owner;
    TSharedPtr<SBox> Content;
    TArray<TSharedPtr<FSlateBrush>> Brushes;
    TArray<TSharedPtr<FButtonStyle>> Styles;
    TArray<RaceChoice> Races;
    TArray<ClassChoice> Classes;
    FDaocCreationRequest Draft;
    FString Page, Feedback, SelectedName, PendingName, Account, Secret, Host = TEXT("127.0.0.1");
    int32 SeenRoster = -1;
    UDaocNetworkSubsystem *Session() const
    {
        return Owner.IsValid() ? Owner->GetSession() : nullptr;
    }
    FLinearColor Gold() const
    {
        return Owner.IsValid() && Owner->GetTheme() ? Owner->GetTheme()->TextColor : FLinearColor(.88f, .78f, .5f);
    }
    const FSlateBrush *Brush(UObject *Resource, FVector2D Size)
    {
        auto B = MakeShared<FSlateBrush>();
        B->SetResourceObject(Resource);
        B->ImageSize = Size;
        B->DrawAs = ESlateBrushDrawType::Image;
        Brushes.Add(B);
        return &B.Get();
    }
    TSharedRef<SWidget> Label(const FString &Value, int32 Size = 12)
    {
        return SNew(STextBlock).Text(FText::FromString(Value)).ColorAndOpacity(Gold()).Font(FCoreStyle::GetDefaultFontStyle("Regular", FMath::Max(10, Size - 2))).AutoWrapText(true);
    }
    TSharedRef<SWidget> Button(const FString &Value, TFunction<void()> Action, TAttribute<bool> Enabled = true)
    {
        auto Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
        if (auto *Theme = Owner.IsValid() ? Owner->GetTheme() : nullptr)
        {
            const auto Image = [&](FName Key) {
                FSlateBrush B;
                B.SetResourceObject(Theme->Image(Key));
                B.ImageSize = FVector2D(64, 20);
                B.DrawAs = ESlateBrushDrawType::Box;
                B.Margin = FMargin(.125f, .2f);
                return B;
            };
            if (Theme->Image(TEXT("button_normal")))
                Style->SetNormal(Image(TEXT("button_normal"))).SetHovered(Image(TEXT("button_hover"))).SetPressed(Image(TEXT("button_pressed"))).SetDisabled(Image(TEXT("button_disabled")));
        }
        Styles.Add(Style);
        return SNew(SButton).ButtonStyle(&Style.Get()).IsEnabled(Enabled).OnClicked_Lambda([Action]() {
            Action();
            return FReply::Handled();
        })[Label(Value, 14)];
    }
    void ReadRules()
    {
        if (!Owner.IsValid() || !Owner->GetTheme())
            return;
        FString Json = Owner->GetTheme()->CreationRulesJson;
        if (Json.IsEmpty() && Owner->ClassicTheme)
            Json = Owner->ClassicTheme->CreationRulesJson;
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root))
            return;
        for (const auto &V : Root->GetArrayField(TEXT("races")))
        {
            auto O = V->AsObject();
            RaceChoice R;
            R.Id = int32(O->GetNumberField(TEXT("id")));
            R.Realm = int32(O->GetNumberField(TEXT("realm")));
            R.Name = O->GetStringField(TEXT("name"));
            R.Male = int32(O->GetNumberField(TEXT("male")));
            R.Female = int32(O->GetNumberField(TEXT("female")));
            for (const auto &S : O->GetArrayField(TEXT("stats")))
                R.Stats.Add(int32(S->AsNumber()));
            Races.Add(R);
        }
        for (const auto &V : Root->GetArrayField(TEXT("classes")))
        {
            auto O = V->AsObject();
            ClassChoice C;
            C.Id = int32(O->GetNumberField(TEXT("id")));
            C.Name = O->GetStringField(TEXT("name"));
            double Gender;
            if (O->TryGetNumberField(TEXT("gender"), Gender))
                C.Gender = int32(Gender);
            for (const auto &R : O->GetArrayField(TEXT("races")))
                C.Races.Add(int32(R->AsNumber()));
            Classes.Add(C);
        }
    }
    const RaceChoice *Race() const
    {
        return Races.FindByPredicate([&](const RaceChoice &R) { return R.Id == Draft.Appearance.Race; });
    }
    void ChooseRace(int32 Id)
    {
        const bool NewRace = Draft.Appearance.Race != Id;
        Draft.Appearance.Race = Id;
        if (const auto *R = Race())
        {
            if (NewRace || Draft.Stats.Num() != 8)
                Draft.Stats = R->Stats;
            Draft.Appearance.Model = Draft.Appearance.Gender ? R->Female : R->Male;
            if (!Classes.ContainsByPredicate([&](const ClassChoice &C) { return C.Id == Draft.Appearance.ClassId && C.Races.Contains(Id) && (C.Gender < 0 || C.Gender == Draft.Appearance.Gender); }))
                for (const auto &C : Classes)
                    if (C.Races.Contains(Id) && (C.Gender < 0 || C.Gender == Draft.Appearance.Gender))
                    {
                        Draft.Appearance.ClassId = C.Id;
                        break;
                    }
        }
        if (Owner.IsValid())
            Owner->Preview(Draft.Appearance);
    }
    void NewCharacter()
    {
        auto *S = Session();
        if (!S || !S->bCharactersReady)
            return;
        Draft = FDaocCreationRequest();
        Draft.Appearance.Realm = S->Realm;
        Draft.Appearance.Gender = 0;
        Draft.Appearance.ModelFlags = 0x1000;
        Draft.Appearance.EyeSize = Draft.Appearance.LipSize = Draft.Appearance.MoodType = 0x55;
        Draft.Appearance.EyeColor = 0x10;
        Draft.Appearance.HairColor = 0xb0;
        Draft.Appearance.FaceType = Draft.Appearance.HairStyle = 0;
        Draft.Region = S->Realm == 1 ? 1 : S->Realm == 2 ? 100
                                                         : 200;
        Draft.Slot = 0;
        while (S->CharacterSlots.ContainsByPredicate([&](const FDaocCharacterSlot &C) { return C.Slot == Draft.Slot; }))
            ++Draft.Slot;
        if (Draft.Slot >= (S->ClientVersion >= 73 ? 10 : 8))
        {
            Feedback = TEXT("This realm has no free character slots.");
            return;
        }
        for (const auto &R : Races)
            if (R.Realm == S->Realm)
            {
                ChooseRace(R.Id);
                break;
            }
        Feedback.Reset();
        SetPage(TEXT("Creation"));
    }
    void Select(const FDaocCharacterSlot &Slot)
    {
        SelectedName = Slot.Appearance.Name;
        if (Owner.IsValid())
            Owner->Preview(Slot.Appearance);
    }
    void SetPage(const FString &Next)
    {
        Page = Next;
        if (Owner.IsValid())
            Owner->SetPreviewFocus(Page == TEXT("Customize"), Page == TEXT("Characters") || Page == TEXT("Creation") || Page == TEXT("Customize"));
        Rebuild();
    }
    void Rebuild()
    {
        auto OldBrushes = MoveTemp(Brushes);
        auto OldStyles = MoveTemp(Styles);
        auto *S = Session();
        auto *Theme = Owner.IsValid() ? Owner->GetTheme() : nullptr;
        const bool PreviewPage = Page == TEXT("Characters") || Page == TEXT("Creation") || Page == TEXT("Customize");
        FName Background = Page == TEXT("Realm") ? TEXT("realm_selection") : Page == TEXT("Characters") ? TEXT("character_selection")
                                                                         : Page == TEXT("Creation")     ? TEXT("character_creation")
                                                                         : Page == TEXT("Customize")    ? TEXT("character_customize")
                                                                         : Page == TEXT("Location")     ? TEXT("character_location")
                                                                                                        : TEXT("splash");
        TSharedRef<SOverlay> Overlay = SNew(SOverlay);
        Overlay->AddSlot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black)];
        if (PreviewPage && Owner.IsValid())
        {
            Overlay->AddSlot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black)];
            auto *Texture = Owner->GetPreviewMaterial();
            Overlay->AddSlot().Padding(FMargin(0, 50, 0, 70))[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SBox).WidthOverride(1024).HeightOverride(648)[SNew(SImage).Image(Brush(Texture, FVector2D(1024, 648)))]]];
        }
        if (Page == TEXT("Location") && Theme)
        {
            const TCHAR *Left[] = {TEXT(""), TEXT("cotswold"), TEXT("mularnvillage"), TEXT("magmell")};
            const TCHAR *Right[] = {TEXT(""), TEXT("caergothwaite"), TEXT("aegirhamn"), TEXT("domnann")};
            const int32 Realm = FMath::Clamp(Draft.Appearance.Realm, 1, 3);
            auto Artwork = SNew(SHorizontalBox);
            for (FName Key : {FName(Left[Realm]), FName(Right[Realm])})
                Artwork->AddSlot().FillWidth(1)[SNew(SImage).Image(Brush(Theme->Image(Key), FVector2D(512, 630)))];
            Overlay->AddSlot().Padding(FMargin(0, 50, 0, 75))[Artwork];
        }
        if (Theme && Theme->Image(Background))
            Overlay->AddSlot()[SNew(SImage).Image(Brush(Theme->Image(Background), FVector2D(1024, 768)))];
        TSharedRef<SCanvas> Canvas = SNew(SCanvas);
        Overlay->AddSlot()[Canvas];
        const auto Place = [&](float X, float Y, float W, float H, TSharedRef<SWidget> Widget) {
            Canvas->AddSlot()
                .Position(TAttribute<FVector2D>::CreateLambda([this, X, Y]() {
                    const auto Size = GetCachedGeometry().GetLocalSize();
                    return FVector2D(X * Size.X / 1024, Y * Size.Y / 768);
                }))
                .Size(TAttribute<FVector2D>::CreateLambda([this, W, H]() {
                    const auto Size = GetCachedGeometry().GetLocalSize();
                    return FVector2D(W * Size.X / 1024, H * Size.Y / 768);
                }))[Widget];
        };
        const auto FeedbackWidget = SNew(STextBlock)
                                        .Text_Lambda([this]() {
                                            auto *Net = Session();
                                            return FText::FromString(!Feedback.IsEmpty() ? Feedback : Net ? Net->LastNetworkError
                                                                                                          : TEXT(""));
                                        })
                                        .ColorAndOpacity(FLinearColor(1.f, .6f, .4f))
                                        .AutoWrapText(true);
        Place(260, 16, 500, 42, FeedbackWidget);
        if (Page == TEXT("Login"))
        {
            auto Form = SNew(SVerticalBox);
            Form->AddSlot().AutoHeight()[Label(TEXT("Connect to Dawn of Light"), 24)];
            if (S)
            {
                Host = S->ServerAddress;
                Account = S->Username;
            }
            const auto Field = [&](const FString &Title, FString &Value, bool Password) {
                Form->AddSlot().AutoHeight().Padding(0, 8)[Label(Title)];
                Form->AddSlot().AutoHeight()[SNew(SEditableTextBox).Text(FText::FromString(Value)).IsPassword(Password).OnTextChanged_Lambda([&Value](const FText &T) { Value = T.ToString(); })];
            };
            Field(TEXT("Server IPv4"), Host, false);
            Field(TEXT("Account"), Account, false);
            Field(TEXT("Password"), Secret, true);
            Form->AddSlot().AutoHeight().Padding(0, 16)[Button(TEXT("Login"), [this]() {
                if (auto *N = Session())
                {
                    N->ServerAddress = Host;
                    N->bUseFrontend = true;
                    N->SetCredentials(Account, Secret, TEXT(""));
                    N->Connect();
                    Secret.Reset();
                    Feedback.Reset();
                    SetPage(TEXT("Connecting"));
                }
            })];
            Place(320, 260, 384, 330, Form);
        }
        else if (Page == TEXT("Connecting") || Page == TEXT("Entering") || Page == TEXT("Waiting"))
        {
            Place(310, 510, 480, 90,
                  SNew(STextBlock)
                      .Text_Lambda([this]() {
                          auto *N = Session();
                          return FText::FromString(FString(TEXT("Loading... ")) + (N ? N->ConnectionState + TEXT("\n") + N->LastNetworkError : TEXT("")));
                      })
                      .ColorAndOpacity(Gold())
                      .Font(FCoreStyle::GetDefaultFontStyle("Regular", 22)));
        }
        else if (Page == TEXT("Realm"))
        {
            const int32 Order[] = {1, 3, 2};
            const TCHAR *Names[] = {TEXT("Albion"), TEXT("Hibernia"), TEXT("Midgard")};
            const TCHAR *Descriptions[] = {TEXT("The rolling hills of Camelot and the legends of King Arthur. "
                                                "Knights, "
                                                "sorcerers and rogues defend the realm of Albion."),
                                           TEXT("A realm of ancient forests and living magic. Celts, elves and "
                                                "the "
                                                "creatures of nature defend Hibernia."),
                                           TEXT("Snowy mountains, mighty forests and the legends of the Norse. "
                                                "Warriors and mystics defend Midgard.")};
            for (int32 I = 0; I < 3; ++I)
            {
                const int32 Realm = Order[I];
                auto Column = SNew(SVerticalBox);
                const FName Icon = Realm == 1 ? TEXT("albion_icon") : Realm == 2 ? TEXT("midgard_icon")
                                                                                 : TEXT("hibernia_icon");
                if (Theme && Theme->Image(Icon))
                    Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(148).HeightOverride(150)[SNew(SImage).Image(Brush(Theme->Image(Icon), FVector2D(148, 150)))]];
                Column->AddSlot().AutoHeight().Padding(0, 24)[Button(
                    Names[I],
                    [this, Realm]() {
                        if (auto *N = Session())
                            if (N->RequestRealm(Realm))
                            {
                                Feedback.Reset();
                                SetPage(TEXT("Waiting"));
                            }
                    },
                    TAttribute<bool>::CreateLambda([this, Realm]() {
                        auto *N = Session();
                        return N && (N->AllowedRealm == 0 || N->AllowedRealm == Realm);
                    }))];
                Column->AddSlot().AutoHeight().Padding(0, 18)[Label(Descriptions[I], 15)];
                Place(30 + I * 342, 172, 280, 430, Column);
            }
        }
        else if (Page == TEXT("Characters"))
        {
            SeenRoster = S ? S->RosterRevision : 0;
            auto List = SNew(SVerticalBox);
            List->AddSlot().AutoHeight()[Label(TEXT("Characters"), 22)];
            if (S)
            {
                if (!S->CharacterSlots.ContainsByPredicate([&](const FDaocCharacterSlot &C) { return C.Appearance.Name == SelectedName; }) && !S->CharacterSlots.IsEmpty())
                    Select(S->CharacterSlots[0]);
                for (const auto &C : S->CharacterSlots)
                {
                    List->AddSlot().AutoHeight().Padding(0, 4)[Button(FString::Printf(TEXT("%s\nLevel %d %s"), *C.Appearance.Name, C.Appearance.Level, *C.ClassName), [this, C]() {
                        Select(C);
                        Rebuild();
                    })];
                }
            }
            List->AddSlot().AutoHeight().Padding(0, 12)[Button(TEXT("Create Character"), [this]() { NewCharacter(); })];
            Place(25, 105, 205, 520, SNew(SScrollBox) + SScrollBox::Slot()[List]);
            Place(800, 130, 196, 300,
                  Label(S ? FString::Printf(TEXT("%s\n\nRealm %d\n\nSelect a character to "
                                                 "preview appearance and equipment."),
                                            *SelectedName, S->Realm)
                          : TEXT(""),
                        18));
            Place(835, 716, 160, 36,
                  Button(
                      TEXT("Play"),
                      [this]() {
                          if (auto *N = Session())
                              if (N->SelectCharacter(SelectedName))
                                  SetPage(TEXT("Entering"));
                      },
                      !SelectedName.IsEmpty()));
            Place(790, 620, 200, 32, Button(TEXT("Delete Character..."), [this]() { SetPage(TEXT("Delete")); }, !SelectedName.IsEmpty()));
        }
        else if (Page == TEXT("Delete"))
        {
            Place(310, 300, 500, 80, Label(TEXT("Delete ") + SelectedName + TEXT(" permanently?"), 24));
            Place(350, 410, 150, 35, Button(TEXT("Cancel"), [this]() { SetPage(TEXT("Characters")); }));
            Place(530, 410, 150, 35, Button(TEXT("Confirm Delete"), [this]() {
                      if (auto *N = Session())
                          if (N->DeleteCharacter(SelectedName))
                          {
                              SelectedName.Reset();
                              SetPage(TEXT("Waiting"));
                          }
                  }));
        }
        else if (Page == TEXT("Creation"))
        {
            auto Right = SNew(SVerticalBox);
            Right->AddSlot().AutoHeight()[Label(TEXT("Name"), 20)];
            Right->AddSlot().AutoHeight()[SNew(SEditableTextBox).Text(FText::FromString(Draft.Appearance.Name)).OnTextChanged_Lambda([this](const FText &T) { Draft.Appearance.Name = T.ToString(); })];
            Right->AddSlot().AutoHeight()[Button(TEXT("Check Name"), [this]() {
                if (!Draft.Appearance.Name.IsEmpty())
                    Draft.Appearance.Name[0] = FChar::ToUpper(Draft.Appearance.Name[0]);
                if (auto *N = Session())
                    N->CheckCharacterName(Draft.Appearance.Name);
                Rebuild();
            })];
            Right->AddSlot().AutoHeight()[SNew(STextBlock)
                                              .Text_Lambda([this]() {
                                                  auto *N = Session();
                                                  return FText::FromString(!N || (Draft.Appearance.Name.IsEmpty() || N->CheckedCharacterName != Draft.Appearance.Name) ? TEXT("") : N->NameCheckResult < 0 ? TEXT("Checking...")
                                                                                                                                                                                : N->NameCheckResult == 0  ? TEXT("Name available")
                                                                                                                                                                                                           : TEXT("Name unavailable"));
                                              })
                                              .ColorAndOpacity(Gold())];
            Right->AddSlot().AutoHeight().Padding(0, 10)[Label(TEXT("Race"), 20)];
            for (const auto &R : Races)
                if (R.Realm == Draft.Appearance.Realm)
                    Right->AddSlot().AutoHeight()[Button(R.Name, [this, R]() {
                        ChooseRace(R.Id);
                        Rebuild();
                    })];
            auto Gender = SNew(SHorizontalBox);
            for (int32 I = 0; I < 2; ++I)
                Gender->AddSlot()[Button(I ? TEXT("Female") : TEXT("Male"), [this, I]() {
                    Draft.Appearance.Gender = I;
                    ChooseRace(Draft.Appearance.Race);
                    Rebuild();
                })];
            Right->AddSlot().AutoHeight().Padding(0, 8)[Gender];
            Right->AddSlot().AutoHeight()[Label(TEXT("Class"), 20)];
            for (const auto &C : Classes)
                if (C.Races.Contains(Draft.Appearance.Race) && (C.Gender < 0 || C.Gender == Draft.Appearance.Gender))
                    Right->AddSlot().AutoHeight()[Button(C.Name, [this, C]() {
                        Draft.Appearance.ClassId = C.Id;
                        Rebuild();
                    })];
            Place(796, 90, 205, 550, SNew(SScrollBox) + SScrollBox::Slot()[Right]);
            auto Left = SNew(SVerticalBox);
            const auto *R = Race();
            Left->AddSlot().AutoHeight()[Label(R ? R->Name : TEXT("Race"), 22)];
            for (const auto &C : Classes)
                if (C.Id == Draft.Appearance.ClassId)
                    Left->AddSlot().AutoHeight().Padding(0, 12)[Label(C.Name, 22)];
            const TCHAR *Stats[] = {TEXT("Strength"), TEXT("Dexterity"), TEXT("Constitution"), TEXT("Quickness"), TEXT("Intelligence"), TEXT("Piety"), TEXT("Empathy"), TEXT("Charisma")};
            Left->AddSlot().AutoHeight().Padding(0, 20)[Label(TEXT("Attributes"), 20)];
            for (int32 I = 0; I < Draft.Stats.Num(); ++I)
            {
                auto Row = SNew(SHorizontalBox);
                Row->AddSlot().AutoWidth()[Button(TEXT("-"), [this, I]() {
                    if (const auto *B = Race())
                        Draft.Stats[I] = FMath::Max(B->Stats[I], Draft.Stats[I] - 1);
                    Rebuild();
                })];
                Row->AddSlot()[Label(FString::Printf(TEXT("%s %d"), Stats[I], Draft.Stats[I]), 12)];
                Row->AddSlot().AutoWidth()[Button(TEXT("+"), [this, I]() {
                    if (const auto *B = Race())
                    {
                        auto V = Draft.Stats;
                        ++V[I];
                        if (Points(B->Stats, V) <= 30)
                            Draft.Stats = V;
                    }
                    Rebuild();
                })];
                Left->AddSlot().AutoHeight().Padding(0, 3)[Row];
            }
            Left->AddSlot().AutoHeight().Padding(0, 12)[Label(FString::Printf(TEXT("%d points remaining"), R ? 30 - Points(R->Stats, Draft.Stats) : 30), 16)];
            Left->AddSlot().AutoHeight()[Button(TEXT("Assign 30 Points"), [this]() {
                if (const auto *B = Race())
                {
                    Draft.Stats = B->Stats;
                    for (int32 I : {0, 1, 2})
                        Draft.Stats[I] += 10;
                }
                Rebuild();
            })];
            Place(14, 100, 220, 540, Left);
            Place(835, 716, 160, 36,
                  Button(
                      TEXT("Continue"), [this]() { SetPage(TEXT("Customize")); }, TAttribute<bool>::CreateLambda([this]() {
                          auto *N = Session();
                          return Race() && Points(Race()->Stats, Draft.Stats) == 30 && N && N->CheckedCharacterName == Draft.Appearance.Name && N->NameCheckResult == 0;
                      })));
        }
        else if (Page == TEXT("Customize"))
        {
            auto Right = SNew(SVerticalBox);

            const auto Cycle = [&](const FString &Name, int32 *Value, int32 Max, bool Upper) {
                const auto Change = [this, Value, Max, Upper](int32 Step) {
                    int32 V = Upper ? (*Value >> 4) : (*Value & 15);
                    V = (V + Step + Max + 1) % (Max + 1);
                    *Value = Upper ? ((*Value & 15) | (V << 4)) : ((*Value & 240) | V);
                    Owner->Preview(Draft.Appearance);
                    Rebuild();
                };
                auto Row = SNew(SHorizontalBox);
                Row->AddSlot().AutoWidth()[Button(TEXT("<"), [Change]() { Change(-1); })];
                Row->AddSlot()[Label(Name + FString::Printf(TEXT(" %d"), (Upper ? (*Value >> 4) : (*Value & 15)) + 1), 14)];
                Row->AddSlot().AutoWidth()[Button(TEXT(">"), [Change]() { Change(1); })];
                Right->AddSlot().AutoHeight().Padding(0, 5)[Row];
            };
            Cycle(TEXT("Face"), &Draft.Appearance.FaceType, 5, true);
            Cycle(TEXT("Hair"), &Draft.Appearance.HairStyle, 3, true);
            Cycle(TEXT("Eye Color"), &Draft.Appearance.EyeColor, 7, true);
            Cycle(TEXT("Skin Tone"), &Draft.Appearance.EyeColor, 7, false);
            Cycle(TEXT("Hair Color"), &Draft.Appearance.HairColor, 15, true);
            const auto Morph = [&](const FString &Name, int32 *Value, bool Upper) {
                Right->AddSlot().AutoHeight().Padding(0, 5)[Label(Name, 14)];
                Right->AddSlot().AutoHeight()[SNew(SSlider).Value(float(Upper ? (*Value >> 4) : (*Value & 15)) / 10.f).OnValueChanged_Lambda([this, Value, Upper](float V) {
                    const int32 N = FMath::RoundToInt(V * 10);
                    *Value = Upper ? ((*Value & 15) | (N << 4)) : ((*Value & 240) | N);
                    Owner->Preview(Draft.Appearance);
                })];
            };
            Morph(TEXT("Eyes"), &Draft.Appearance.EyeSize, true);
            Morph(TEXT("Nose"), &Draft.Appearance.EyeSize, false);
            Morph(TEXT("Lips"), &Draft.Appearance.LipSize, true);
            Morph(TEXT("Jaw"), &Draft.Appearance.LipSize, false);
            Cycle(TEXT("Mood"), &Draft.Appearance.MoodType, 10, true);
            auto HeightRow = SNew(SHorizontalBox);
            const TCHAR *Heights[] = {TEXT("Small"), TEXT("Medium"), TEXT("Large")};
            for (int32 I = 1; I <= 3; ++I)
            {
                const FString Caption = FString(Heights[I - 1]) + (((Draft.Appearance.ModelFlags >> 11) & 3) == I ? TEXT(" *") : TEXT(""));
                HeightRow->AddSlot()[Button(Caption, [this, I]() {
                    Draft.Appearance.ModelFlags = (Draft.Appearance.ModelFlags & ~0x1800) | (I << 11);
                    Owner->Preview(Draft.Appearance);
                    Rebuild();
                })];
            }
            Right->AddSlot().AutoHeight().Padding(0, 8)[Label(TEXT("Height"), 14)];
            Right->AddSlot().AutoHeight()[HeightRow];
            Right->AddSlot().AutoHeight().Padding(0, 12)[Button(TEXT("Default"), [this]() {
                Draft.Appearance.FaceType = Draft.Appearance.HairStyle = 0;
                Draft.Appearance.EyeColor = 0x10;
                Draft.Appearance.HairColor = 0xb0;
                Draft.Appearance.EyeSize = Draft.Appearance.LipSize = 0x55;
                Owner->Preview(Draft.Appearance);
                Rebuild();
            })];
            Place(795, 120, 207, 520, SNew(SScrollBox) + SScrollBox::Slot()[Right]);
            Place(835, 716, 160, 36, Button(TEXT("Continue"), [this]() { SetPage(TEXT("Location")); }));
            Place(435, 716, 160, 36, Button(TEXT("Race / Class"), [this]() { SetPage(TEXT("Creation")); }));
        }
        else if (Page == TEXT("Location"))
        {
            const TCHAR *Names[] = {TEXT(""), TEXT("Cotswold Village"), TEXT("Mularn Village"), TEXT("Mag Mell")};
            const TCHAR *SINames[] = {TEXT(""), TEXT("Caer Gothwaite"), TEXT("Aegirhamn"), TEXT("Grove of Domnann")};
            for (int32 I = 0; I < 2; ++I)
            {
                const int32 Realm = Draft.Appearance.Realm;
                const FString Caption = FString(I ? SINames[Realm] : Names[Realm]) + (Draft.bIsles == (I != 0) ? TEXT("  (Selected)") : TEXT(""));
                Place(30 + I * 512, 450, 452, 36, Button(Caption, [this, I]() {
                          Draft.bIsles = I != 0;
                          const int32 ClassicRegions[] = {0, 1, 100, 200};
                          const int32 IslesRegions[] = {0, 51, 151, 181};
                          Draft.Region = Draft.bIsles ? IslesRegions[Draft.Appearance.Realm] : ClassicRegions[Draft.Appearance.Realm];
                          Feedback.Reset();
                          Rebuild();
                      }));
                const TCHAR *Descriptions[] = {TEXT("Begin your adventure in the original lands of your realm, "
                                                    "with "
                                                    "trainers and nearby hunting grounds."),
                                               TEXT("Begin your adventure in the Shrouded Isles, with trainers and "
                                                    "merchants in a new starting town.")};
                Place(38 + I * 512, 500, 432, 130, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0, 0, 0, .6f)).Padding(16)[Label(Descriptions[I], 16)]);
            }
            Place(835, 716, 160, 36, Button(TEXT("Create"), [this]() {
                      if (auto *N = Session())
                      {
                          if (N->CreateCharacter(Draft))
                          {
                              PendingName = Draft.Appearance.Name;
                              Feedback.Reset();
                              SetPage(TEXT("Waiting"));
                          }
                          else
                          {
                              Feedback = TEXT("Creation could not be sent. Recheck the "
                                              "name, free slot and attributes.");
                              Rebuild();
                          }
                      }
                  }));
            Place(435, 716, 160, 36, Button(TEXT("Customize"), [this]() { SetPage(TEXT("Customize")); }));
        }
        if (Page != TEXT("Connecting") && Page != TEXT("Entering") && Page != TEXT("Waiting"))
        {
            Place(35, 716, 160, 36, Button(Page == TEXT("Login") ? TEXT("Quit") : TEXT("Cancel"), [this]() {
                      if (Page == TEXT("Login"))
                      {
                          if (Owner.IsValid() && Owner->GetOwningPlayerController())
                              Owner->GetOwningPlayerController()->ConsoleCommand(TEXT("quit"));
                      }
                      else if (Page == TEXT("Realm") || Page == TEXT("Characters"))
                      {
                          if (auto *N = Session())
                              N->Disconnect();
                          SetPage(TEXT("Login"));
                      }
                      else
                          SetPage(TEXT("Characters"));
                  }));
            if (Page != TEXT("Login") && Page != TEXT("Realm"))
                Place(230, 716, 160, 36, Button(TEXT("Realm"), [this]() { SetPage(TEXT("Realm")); }, S && S->AllowedRealm == 0));
        }
        Place(810, 24, 180, 30, Button(Owner.IsValid() && Owner->bEnhancedUI ? TEXT("Enhanced UI") : TEXT("Classic UI"), [this]() {
                  if (Owner.IsValid())
                  {
                      Owner->bEnhancedUI = !Owner->bEnhancedUI;
                      Rebuild();
                  }
              }));
        if (PreviewPage)
        {
            Place(25, 640, 40, 28, Button(TEXT("<"), [this]() { Owner->Orbit(-15, 0); }));
            Place(75, 640, 40, 28, Button(TEXT(">"), [this]() { Owner->Orbit(15, 0); }));
            Place(125, 640, 40, 28, Button(TEXT("+"), [this]() { Owner->Orbit(0, -.1f); }));
            Place(175, 640, 40, 28, Button(TEXT("-"), [this]() { Owner->Orbit(0, .1f); }));
        }
        Content->SetContent(Overlay);
    }
};

ADaocFrontendHUD::ADaocFrontendHUD()
{
    PrimaryActorTick.bCanEverTick = true;
}
UDaocNetworkSubsystem *ADaocFrontendHUD::GetSession() const
{
    return UDaocNetworkSubsystem::Get(this);
}
UDaocFrontendTheme *ADaocFrontendHUD::GetTheme() const
{
    return bEnhancedUI && EnhancedTheme ? EnhancedTheme.Get() : ClassicTheme.Get();
}
void ADaocFrontendHUD::BeginPlay()
{
    Super::BeginPlay();
    if (!ClassicTheme)
        ClassicTheme = LoadObject<UDaocFrontendTheme>(nullptr, TEXT("/Game/DAoC/UI/DA_ClassicFrontend.DA_ClassicFrontend"), nullptr, LOAD_NoWarn);
    if (!EnhancedTheme)
        EnhancedTheme = LoadObject<UDaocFrontendTheme>(nullptr, TEXT("/Game/DAoC/UI/DA_EnhancedFrontend.DA_EnhancedFrontend"), nullptr, LOAD_NoWarn);
}
void ADaocFrontendHUD::Tick(float Delta)
{
    Super::Tick(Delta);
    auto *Session = GetSession();
    auto *Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    const bool Show = Session && Session->bUseFrontend && (Session->ConnectionState != TEXT("Playing") || !Session->bWorldReady) && !FParse::Param(FCommandLine::Get(), TEXT("DaocOffline"));
    if (Show && Viewport && PlayerOwner)
    {
        if (!Screen.IsValid())
        {
            Preview(FDaocCharacterAppearance());
            Screen = SNew(SDaocFrontend).Owner(this);
            Viewport->AddViewportWidgetContent(Screen.ToSharedRef(), 100);
        }
        if (!bMenuInput)
        {
            bMenuInput = true;
            PlayerOwner->SetIgnoreMoveInput(true);
            PlayerOwner->SetIgnoreLookInput(true);
            PlayerOwner->bShowMouseCursor = true;
            PlayerOwner->SetInputMode(FInputModeUIOnly());
        }
        Screen->Update();
#if !UE_BUILD_SHIPPING
        FString CaptureFile;
        if (FParse::Value(FCommandLine::Get(), TEXT("CamelotFrontendScreenshot="), CaptureFile))
        {
            CaptureElapsed += Delta;
            if (CaptureElapsed > 12.f && !bCaptureRequested)
            {
                FScreenshotRequest::RequestScreenshot(CaptureFile, true, false);
                bCaptureRequested = true;
            }
            if (CaptureElapsed > 15.f)
                FPlatformMisc::RequestExit(false);
        }
#endif
    }
    else if (Screen.IsValid())
    {
        if (Viewport)
            Viewport->RemoveViewportWidgetContent(Screen.ToSharedRef());
        Screen.Reset();
        ReleasePreview();
        if (PlayerOwner && bMenuInput)
        {
            PlayerOwner->SetIgnoreMoveInput(false);
            PlayerOwner->SetIgnoreLookInput(false);
            PlayerOwner->bShowMouseCursor = false;
            PlayerOwner->SetInputMode(FInputModeGameOnly());
        }
        bMenuInput = false;
    }
}
void ADaocFrontendHUD::DrawHUD()
{
    if (!Screen.IsValid())
        Super::DrawHUD();
}
void ADaocFrontendHUD::ReleasePreview()
{
    if (PreviewActor)
    {
        TInlineComponentArray<UMeshComponent *> Meshes(PreviewActor);
        for (auto *Mesh : Meshes)
            Mesh->SetTextureForceResidentFlag(false);
    }
    if (PreviewActor)
        PreviewActor->Destroy();
    PreviewActor = nullptr;
    PreviewBody = nullptr;
    PreviewEquipment = nullptr;
    PreviewCamera = nullptr;
    PreviewTexture = nullptr;
    PreviewMaterial = nullptr;
    PreviewStage = nullptr;
}
void ADaocFrontendHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Screen.IsValid() && GetWorld()->GetGameViewport())
        GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Screen.ToSharedRef());
    Screen.Reset();
    ReleasePreview();
    Super::EndPlay(Reason);
}
void ADaocFrontendHUD::Preview(const FDaocCharacterAppearance &Appearance)
{
    if (!PreviewActor)
    {
        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        PreviewActor = GetWorld()->SpawnActor<AActor>(FVector(0, 0, -100000), FRotator::ZeroRotator, Params);
        auto *Root = NewObject<USceneComponent>(PreviewActor);
        PreviewActor->SetRootComponent(Root);
        Root->RegisterComponent();
        Root->SetWorldLocation(FVector(0, 0, -100000));
        PreviewBody = NewObject<USkeletalMeshComponent>(PreviewActor);
        PreviewBody->SetupAttachment(Root);
        PreviewBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PreviewBody->SetForcedLOD(1);
        PreviewBody->bEnableUpdateRateOptimizations = false;
        PreviewBody->RegisterComponent();
        PreviewEquipment = NewObject<UDaocEquipmentComponent>(PreviewActor);
        PreviewEquipment->RegisterComponent();
        auto *Light = NewObject<UPointLightComponent>(PreviewActor);
        Light->SetupAttachment(Root);
        Light->SetRelativeLocation(FVector(150, -150, 230));
        Light->SetIntensity(25000);
        Light->SetSourceRadius(35);
        Light->AttenuationRadius = 1500;
        Light->SetCastShadows(true);
        Light->RegisterComponent();
        for (int32 I = 0; I < 2; ++I)
        {
            auto *Fill = NewObject<UPointLightComponent>(PreviewActor);
            Fill->SetupAttachment(Root);
            Fill->SetRelativeLocation(I ? FVector(-150, 100, 230) : FVector(200, 200, 150));
            Fill->SetIntensity(I ? 18000 : 16000);
            Fill->SetSourceRadius(60);
            Fill->AttenuationRadius = 2000;
            Fill->SetCastShadows(false);
            Fill->RegisterComponent();
        }
        PreviewTexture = NewObject<UTextureRenderTarget2D>(this);
        PreviewTexture->InitAutoFormat(3840, 2430);
        PreviewTexture->ClearColor = FLinearColor::Black;
        if (auto *Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DAoC/UI/M_FrontendPreview.M_FrontendPreview")))
        {
            PreviewMaterial = UMaterialInstanceDynamic::Create(Material, this);
            PreviewMaterial->SetTextureParameterValue(TEXT("PreviewTexture"), PreviewTexture);
        }
        PreviewCamera = NewObject<USceneCaptureComponent2D>(PreviewActor);
        PreviewCamera->SetupAttachment(Root);
        PreviewCamera->TextureTarget = PreviewTexture;
        PreviewCamera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        PreviewCamera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        PreviewCamera->ShowOnlyActors.Add(PreviewActor);
        PreviewCamera->FOVAngle = 52;
        PreviewCamera->LODDistanceFactor = .1f;
        PreviewCamera->bAlwaysPersistRenderingState = true;
        PreviewCamera->ShowFlags.SetAntiAliasing(true);
        PreviewCamera->ShowFlags.SetTemporalAA(true);
        PreviewCamera->ShowFlags.SetMotionBlur(false);
        PreviewCamera->ShowFlags.SetDepthOfField(false);
        PreviewCamera->PostProcessSettings.bOverride_AutoExposureMethod = true;
        PreviewCamera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
        PreviewCamera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        PreviewCamera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
        PreviewCamera->PostProcessSettings.bOverride_AutoExposureBias = true;
        PreviewCamera->PostProcessSettings.AutoExposureBias = 0;
        PreviewCamera->RegisterComponent();
        PreviewStage = NewObject<UStaticMeshComponent>(PreviewActor);
        PreviewStage->SetupAttachment(Root);
        PreviewStage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PreviewStage->SetCastShadow(false);
        PreviewStage->RegisterComponent();
    }
    auto *Session = GetSession();
    auto *Mesh = Session ? Session->ResolveCharacterMesh(Appearance.Model) : nullptr;
    TInlineComponentArray<UMeshComponent *> OldMeshes(PreviewActor);
    for (auto *Component : OldMeshes)
        Component->SetTextureForceResidentFlag(false);
    PreviewEquipment->ClearEquipment();
    PreviewBody->Stop();
    PreviewBody->EmptyOverrideMaterials();
    PreviewBody->SetSkeletalMesh(nullptr);
    PreviewBody->SetSkeletalMesh(Mesh);
    PreviewBody->SetRelativeScale3D(FVector(DaocCharacterSizing::AppearanceScale(Appearance.Model, Appearance.ModelFlags)));
    PreviewBody->SetRelativeLocation(FVector::ZeroVector);
    if (auto *Theme = GetTheme())
    {
        auto *Stage = Theme->PreviewStages.Find(Appearance.Realm ? Appearance.Realm : 1);
        if (!Stage && ClassicTheme)
            Stage = ClassicTheme->PreviewStages.Find(Appearance.Realm ? Appearance.Realm : 1);
        PreviewStage->SetStaticMesh(Stage ? Stage->LoadSynchronous() : nullptr);
        PreviewStage->SetRelativeScale3D(FVector(2.54));
        PreviewStage->SetRelativeLocation(FVector(-512, -512, -92.3) * 2.54);
        if (Theme->bBlackPreviewStage)
        {
            PreviewStage->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            PreviewStage->SetRelativeScale3D(FVector(10, 10, .05));
            PreviewStage->SetRelativeLocation(FVector(0, 0, -3));
            PreviewStage->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DAoC/UI/M_LoginStage.M_LoginStage")));
        }
        else
            PreviewStage->EmptyOverrideMaterials();
    }
    if (Mesh)
    {
        PreviewBody->SetRelativeRotation(FRotator(0, -90, 0));
        const auto Path = FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_Idle.A_daoc_%d_Idle"), Appearance.Model, Appearance.Model);
        if (auto *Clip = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn))
            if (Clip->GetSkeleton() == Mesh->GetSkeleton())
                PreviewBody->PlayAnimation(Clip, true);
        PreviewEquipment->ApplyAppearance(PreviewBody, Session->EquipmentCatalog, Appearance);
        TInlineComponentArray<UMeshComponent *> PreviewMeshes(PreviewActor);
        for (auto *Component : PreviewMeshes)
        {
            Component->SetTextureForceResidentFlag(true);
            if (auto *Skeletal = Cast<USkeletalMeshComponent>(Component))
                Skeletal->SetForcedLOD(1);
            if (auto *Static = Cast<UStaticMeshComponent>(Component))
                Static->SetForcedLodModel(1);
        }
        Orbit(0, 0);
    }
}
void ADaocFrontendHUD::Orbit(float Degrees, float Zoom)
{
    if (!PreviewBody || !PreviewCamera)
        return;
    OrbitAngle += Degrees;
    ZoomFactor = FMath::Clamp(ZoomFactor + Zoom, .45f, 2.f);
    const auto Bounds = PreviewBody->CalcBounds(PreviewBody->GetComponentTransform());
    const FVector Target = Bounds.Origin + (bFacePreview ? FVector(0, 0, Bounds.BoxExtent.Z * .45f) : FVector::ZeroVector);
    const float Distance = FMath::Max(150.f, Bounds.BoxExtent.Z * (bFacePreview ? 2.2f : 3.9f)) * ZoomFactor;
    const FVector Eye = Target + FVector(FMath::Cos(FMath::DegreesToRadians(OrbitAngle)) * Distance, FMath::Sin(FMath::DegreesToRadians(OrbitAngle)) * Distance, Bounds.BoxExtent.Z * .12f);
    PreviewCamera->SetWorldLocationAndRotation(Eye, (Target - Eye).Rotation());
    if (!PreviewCamera->bCaptureEveryFrame)
        PreviewCamera->CaptureScene();
}

void ADaocFrontendHUD::SetPreviewFocus(bool Face, bool Visible)
{
    bFacePreview = Face;
    if (PreviewCamera)
    {
        PreviewCamera->bCaptureEveryFrame = Visible;
        Orbit(0, 0);
    }
}
