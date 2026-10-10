#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "DaocAnimationsCommandlet.h"
#include "DaocEquipmentCatalog.h"
#include "DaocEquipmentComponent.h"
#include "DaocFrontendHUD.h"
#include "DaocFrontendTheme.h"
#include "DaocGameMode.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocFrontendTest, "DAoC.Frontend.ThemeRulesAndAppearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocFrontendTest::RunTest(const FString &)
{
    auto *Classic = LoadObject<UDaocFrontendTheme>(nullptr, TEXT("/Game/DAoC/UI/DA_ClassicFrontend.DA_ClassicFrontend"));
    auto *Enhanced = LoadObject<UDaocFrontendTheme>(nullptr, TEXT("/Game/DAoC/UI/DA_EnhancedFrontend.DA_EnhancedFrontend"));
    if (!TestNotNull(TEXT("Classic theme saved"), Classic) || !TestNotNull(TEXT("Enhanced theme saved"), Enhanced))
        return false;
    for (const TCHAR *Name : {TEXT("splash"), TEXT("realm_selection"), TEXT("character_selection"), TEXT("character_creation"), TEXT("character_customize"), TEXT("character_location"), TEXT("albion_icon"), TEXT("midgard_icon"), TEXT("hibernia_icon")})
    {
        TestNotNull(TEXT("Original UI asset present"), Classic->Image(Name));
        TestEqual(TEXT("Enhanced missing pieces inherit classic assets"), Enhanced->Image(Name), Classic->Image(Name));
    }
    TestEqual(TEXT("All three realm preview rooms imported"), Classic->PreviewStages.Num(), 3);
    TestEqual(TEXT("Game mode uses frontend HUD"), GetDefault<ADaocGameMode>()->HUDClass, TSubclassOf<AHUD>(ADaocFrontendHUD::StaticClass()));
    TSharedPtr<FJsonObject> Rules;
    if (!TestTrue(TEXT("Creation rules parse"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Classic->CreationRulesJson), Rules)))
        return false;
    TestEqual(TEXT("18 Catacombs creation races"), Rules->GetArrayField(TEXT("races")).Num(), 18);
    TestEqual(TEXT("All 47 advanced classes"), Rules->GetArrayField(TEXT("classes")).Num(), 47);
    TSet<int32> ClassIds;
    for (const auto &Entry : Rules->GetArrayField(TEXT("classes")))
    {
        const auto Class = Entry->AsObject();
        const int32 Id = int32(Class->GetNumberField(TEXT("id")));
        TestFalse(TEXT("Class IDs are unique"), ClassIds.Contains(Id));
        ClassIds.Add(Id);
        TestTrue(TEXT("Every class has a supported race"), Class->GetArrayField(TEXT("races")).Num() > 0);
        if (Id == 34 || Id == 39)
            TestEqual(TEXT("Valkyrie and Bainshee require female characters"), int32(Class->GetNumberField(TEXT("gender"))), 1);
    }
    TestTrue(TEXT("Friar can be created directly"), ClassIds.Contains(10));
    for (int32 Id : {14, 15, 16, 17, 18, 20, 35, 36, 37, 38, 51, 52, 53, 54, 57})
        TestFalse(TEXT("Base classes are excluded"), ClassIds.Contains(Id));
    auto *Catalog = LoadObject<UDaocEquipmentCatalog>(nullptr, TEXT("/Game/DAoC/Equipment/DA_EquipmentCatalog.DA_EquipmentCatalog"));
    if (!TestNotNull(TEXT("Character assembly catalog"), Catalog))
        return false;
    TestEqual(TEXT("All 36 race/gender appearance families"), Catalog->Customizations.Num(), 36);
    auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto *Actor = World->SpawnActor<AActor>();
    auto *Body = NewObject<USkeletalMeshComponent>(Actor);
    Actor->SetRootComponent(Body);
    Body->RegisterComponent();
    auto *Renderer = NewObject<UDaocEquipmentComponent>(Actor);
    Renderer->RegisterComponent();
    for (const auto &Race : Rules->GetArrayField(TEXT("races")))
        for (const TCHAR *Gender : {TEXT("male"), TEXT("female")})
        {
            const int32 Model = int32(Race->AsObject()->GetNumberField(Gender));
            auto *Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Model, Model));
            if (!TestNotNull(TEXT("Creation body imported"), Mesh))
                continue;
            Renderer->ClearEquipment();
            Body->SetSkeletalMesh(Mesh);
            const auto *Figure = Catalog->Figures.FindByPredicate([&](const FDaocEquipmentFigure &F) { return F.Model == Model; });
            if (!TestNotNull(TEXT("Creation model has assembly mapping"), Figure))
                continue;
            const auto *Custom = Catalog->Customizations.FindByPredicate([&](const FDaocFigureCustomization &C) { return C.Figure == Figure->Figure; });
            if (!TestNotNull(TEXT("Creation model has customization mapping"), Custom))
                continue;
            TestTrue(TEXT("Face choices prepared"), Custom->FaceTextures.Num() > 0);
            for (const auto &Hair : Custom->Hair)
            {
                TestEqual(TEXT("All 16 hair colors prepared"), Hair.Textures.Num(), 16);
                TestNotNull(TEXT("Hair geometry imported"), Hair.Mesh.LoadSynchronous());
            }
            FDaocCharacterAppearance Appearance;
            Appearance.Model = Model;
            Appearance.FaceType = 0;
            Appearance.HairColor = 0xb0;
            Renderer->ApplyAppearance(Body, Catalog, Appearance);
            UTexture *First = nullptr;
            int32 Head = INDEX_NONE;
            for (int32 I = 0; I < Figure->BaseSectionParts.Num(); ++I)
                if (Figure->BaseSectionParts[I] == 0)
                {
                    Head = I;
                    break;
                }
            if (Head != INDEX_NONE)
            {
                auto *Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Head));
                if (TestNotNull(TEXT("Face has dynamic atlas binding"), Material))
                    Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("EquipmentTexture")), First);
                if (Custom->FaceTextures.Num() > 1)
                {
                    Appearance.FaceType = 0x10;
                    Renderer->ApplyAppearance(Body, Catalog, Appearance);
                    UTexture *Second = nullptr;
                    if (auto *Changed = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Head)))
                        Changed->GetTextureParameterValue(FMaterialParameterInfo(TEXT("EquipmentTexture")), Second);
                    TestTrue(TEXT("Face choice changes the actual rendered atlas"), First && Second && First != Second);
                }
            }
        }
    // Switching back and forth must rebuild the body and retain no old materials.
    for (int32 Model : {137, 32, 145, 32, 137})
    {
        Renderer->ClearEquipment();
        Body->Stop();
        Body->EmptyOverrideMaterials();
        auto *Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Model, Model));
        Body->SetSkeletalMesh(nullptr);
        Body->SetSkeletalMesh(Mesh);
        FDaocCharacterAppearance A;
        A.Model = Model;
        A.FaceType = 0;
        Renderer->ApplyAppearance(Body, Catalog, A);
        TestEqual(TEXT("Repeated race switch retains selected body"), Body->GetSkeletalMeshAsset(), Mesh);
        auto *Idle = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_Idle.A_daoc_%d_Idle"), Model, Model));
        if (Idle)
        {
            Body->PlayAnimation(Idle, true);
            UDaocAnimationsCommandlet::EvaluatePreviewPose(Body, 0);
        }
        if (Model == 137 || Model == 145)
        {
            const int32 Bone = Body->GetBoneIndex(TEXT("Bip01 EyeLids"));
            TestTrue(TEXT("Troll idle eyelid starts at its own bind position"), Bone >= 0 && Body->GetBoneSpaceTransforms()[Bone].GetTranslation().Equals(Mesh->GetRefSkeleton().GetRefBonePose()[Bone].GetTranslation(), .05));
        }
    }
    Renderer->ClearEquipment();
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
