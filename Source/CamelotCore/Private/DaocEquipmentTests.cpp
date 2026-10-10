#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DaocEquipmentCatalog.h"
#include "DaocEquipmentComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocEquipmentTest, "DAoC.Characters.EquipmentAssembly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocEquipmentTest::RunTest(const FString &)
{
    TestEqual(TEXT("Plate defaults to its first supported torso"), UDaocEquipmentComponent::SelectGeometryBit(12, 0), 2);
    TestEqual(TEXT("Plate extension selects larger shoulder variant"), UDaocEquipmentComponent::SelectGeometryBit(12, 3), 3);
    TestEqual(TEXT("Unsupported extension retains highest allowed variant"), UDaocEquipmentComponent::SelectGeometryBit(12, 15), 3);
    TestEqual(TEXT("Suppressed geometry is never selected"), UDaocEquipmentComponent::SelectGeometryBit(-1, 3), INDEX_NONE);
    auto *Catalog = LoadObject<UDaocEquipmentCatalog>(nullptr, TEXT("/Game/DAoC/Equipment/DA_EquipmentCatalog.DA_EquipmentCatalog"));
    if (!TestNotNull(TEXT("Saved equipment catalog"), Catalog))
        return false;
    auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto *Owner = World->SpawnActor<AActor>();
    auto *Body = NewObject<USkeletalMeshComponent>(Owner);
    Owner->SetRootComponent(Body);
    Body->RegisterComponent();
    auto *Renderer = NewObject<UDaocEquipmentComponent>(Owner);
    Renderer->RegisterComponent();
    for (int32 Model : {14, 479})
    {
        auto *Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Model, Model));
        if (!TestNotNull(TEXT("Prepared player body"), Mesh))
            break;
        Renderer->ClearEquipment();
        Body->SetSkeletalMesh(Mesh);
        Body->PlayAnimation(LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_Idle.A_daoc_%d_Idle"), Model, Model)), true);
        FDaocCharacterAppearance A;
        A.Model = Model;
        A.bEquipmentKnown = true;
        A.RightWeapon = 0;
        A.LeftWeapon = 1;
        const auto Equip = [&](int32 Slot, int32 Id, int32 Color = 0) {
            FDaocEquipmentAppearance E;
            E.Slot = Slot;
            E.Model = Id;
            E.ColorOrEmblem = Color;
            A.Equipment.Add(E);
        };
        Equip(25, 58, 21);
        Equip(22, 34);
        Equip(26, 96);
        Equip(10, 4);
        Equip(11, 59);
        Renderer->ApplyAppearance(Body, Catalog, A);
        TestTrue(TEXT("Robe and cloak assemble modular armor"), Renderer->ArmorPartCount >= 4);
        TestEqual(TEXT("Sword and shield equip independently"), Renderer->WeaponCount, 2);
        if (auto *Instance = Body->GetAnimInstance())
            for (const TCHAR *Name : {TEXT("Bip01 FrontRobe 01"), TEXT("Bip01 BackRobe 01"), TEXT("Bip01 Helmet")})
                TestTrue(TEXT("Unused base-body skirt and attachment bones are "
                              "evaluated for equipment"),
                         Instance->GetRequiredBones().Contains(static_cast<FBoneIndexType>(Body->GetBoneIndex(Name))));
        else
            AddError(TEXT("Body animation instance missing"));
        TArray<USceneComponent *> Children;
        Body->GetChildrenComponents(false, Children);
        const int32 Count = Children.Num();
        int32 Weapons = 0;
        for (auto *Child : Children)
        {
            if (auto *Armor = Cast<USkeletalMeshComponent>(Child))
            {
                TestTrue(TEXT("Armor follows animated body pose"), Armor->LeaderPoseComponent.Get() == Body);
                if (Armor->GetSkeletalMeshAsset()->GetPathName().StartsWith(TEXT("/Game/DAoC/Customization/")))
                {
                    const auto &HairBones = Armor->GetSkeletalMeshAsset()->GetRefSkeleton();
                    const auto &BodyBones = Mesh->GetRefSkeleton();
                    TestEqual(TEXT("Hair and body bone counts match"), HairBones.GetNum(), BodyBones.GetNum());
                    for (int32 B = 0; B < FMath::Min(HairBones.GetNum(), BodyBones.GetNum()); ++B)
                    {
                        TestEqual(TEXT("Shared hair family preserves bone names"), HairBones.GetBoneName(B), BodyBones.GetBoneName(B));
                        TestEqual(TEXT("Shared hair family preserves hierarchy"), HairBones.GetParentIndex(B), BodyBones.GetParentIndex(B));
                        TestTrue(TEXT("Shared hair family preserves bind pose"), HairBones.GetRefBonePose()[B].Equals(BodyBones.GetRefBonePose()[B], 1e-4));
                    }
                }
                else
                    TestTrue(TEXT("Armor shares the exact character skeleton"), Armor->GetSkeletalMeshAsset()->GetSkeleton() == Mesh->GetSkeleton());
                auto *Material = Cast<UMaterialInstanceDynamic>(Armor->GetMaterial(0));
                TestNotNull(TEXT("Armor has item texture and dye material"), Material);
                if (Material)
                {
                    UTexture *Texture = nullptr;
                    TestTrue(TEXT("Item atlas bound"), Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("EquipmentTexture")), Texture) && Texture);
                }
            }
            else if (auto *Weapon = Cast<UStaticMeshComponent>(Child))
            {
                ++Weapons;
                TestTrue(TEXT("Weapon attached to a skeleton socket"), Body->GetBoneIndex(Weapon->GetAttachSocketName()) != INDEX_NONE);
                TestTrue(TEXT("Weapon uses matching skeletal unit conversion"), Weapon->GetRelativeScale3D().Equals(FVector(2.54), 0.0001));
            }
        }
        TestEqual(TEXT("Exactly two rigid held items"), Weapons, 2);
        TestFalse(TEXT("Ordinary crowd gear is not retained by default"), Renderer->CanRetainForCrowd());
        const auto EquippedParts = Children;
        Renderer->SetPresentationActive(true);
        Renderer->SetPresentationActive(false);
        TestTrue(TEXT("Recently promoted gear can be reused"), Renderer->CanRetainForCrowd());
        for (auto *Child : EquippedParts)
        {
            TestTrue(TEXT("Dormant gear is hidden and detached from network movement"), Child->bHiddenInGame && !Child->GetAttachParent());
            if (auto *Armor = Cast<USkeletalMeshComponent>(Child))
                TestFalse(TEXT("Dormant armor does not evaluate animations"), Armor->IsComponentTickEnabled());
        }
        Renderer->SetPresentationActive(true);
        for (auto *Child : EquippedParts)
            TestTrue(TEXT("Promotion reuses and reattaches the same equipment"), Child->GetAttachParent() == Body && !Child->bHiddenInGame);
        World->TimeSeconds += 3.1;
        TestFalse(TEXT("Unused equipment retention expires"), Renderer->CanRetainForCrowd());
        Renderer->ApplyAppearance(Body, Catalog, A);
        Children.Reset();
        Body->GetChildrenComponents(false, Children);
        TestEqual(TEXT("Unchanged snapshots retain existing components"), Children.Num(), Count);
        const auto *Figure = Catalog->Figures.FindByPredicate([&](const auto &F) { return F.Model == Model; });
        if (Figure)
            for (int32 I = 0; I < Figure->BaseSectionParts.Num(); ++I)
                if (Figure->BaseSectionParts[I] == 1 || Figure->BaseSectionParts[I] == 3 || Figure->BaseSectionParts[I] == 5)
                    TestFalse(TEXT("Robe replaces torso and suppresses sleeves/legs"), Body->IsMaterialSectionShown(I, 0));
        A.bHoodUp = true;
        Renderer->ApplyAppearance(Body, Catalog, A);
        if (Figure)
            for (int32 I = 0; I < Figure->BaseSectionParts.Num(); ++I)
                if (Figure->BaseSectionParts[I] == 8)
                    TestFalse(TEXT("Raised hood hides hair"), Body->IsMaterialSectionShown(I, 0));
        A.bHoodUp = false;
        Equip(21, 2251);
        Renderer->ApplyAppearance(Body, Catalog, A);
        Children.Reset();
        Body->GetChildrenComponents(false, Children);
        TestTrue(TEXT("Catacombs helmet uses its dedicated fitted socket"), Children.ContainsByPredicate([](auto *C) { return Cast<UStaticMeshComponent>(C) && C->GetAttachSocketName() == TEXT("Bip01 Helmet"); }));
        A.Equipment.Reset();
        Equip(25, 46);
        Equip(12, 4);
        A.RightWeapon = A.LeftWeapon = 2;
        A.bHoodUp = false;
        Renderer->ApplyAppearance(Body, Catalog, A);
        TestEqual(TEXT("Two handed slot is not duplicated"), Renderer->WeaponCount, 1);
        TestTrue(TEXT("Plate armor replaces the robe"), Renderer->ArmorPartCount > 0);
        A.RightWeapon = A.LeftWeapon = 15;
        Renderer->ApplyAppearance(Body, Catalog, A);
        TestEqual(TEXT("Inactive weapons are not held"), Renderer->WeaponCount, 0);
        A.Equipment.Reset();
        Renderer->ApplyAppearance(Body, Catalog, A);
        TestEqual(TEXT("Unequipping removes all modular parts"), Renderer->ArmorPartCount, 0);
        if (Figure)
            for (int32 I = 0; I < Figure->BaseSectionParts.Num(); ++I)
            {
                const bool HasHair = Catalog->Customizations.ContainsByPredicate([&](const FDaocFigureCustomization &C) { return C.Figure == Figure->Figure && C.Hair.ContainsByPredicate([](const FDaocHairVariation &H) { return H.Style == 0; }); });
                TestEqual(TEXT("Empty equipment restores body while retaining selected "
                               "hairstyle"),
                          Body->IsMaterialSectionShown(I, 0), Figure->BaseSectionParts[I] != 7 && !(HasHair && Figure->BaseSectionParts[I] == 8));
            }
        Renderer->ClearEquipment();
        Children.Reset();
        Body->GetChildrenComponents(false, Children);
        TestEqual(TEXT("Clear removes every generated component"), Children.Num(), 0);
    }
    auto *Troll = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/DAoC/Characters/SK_daoc_137.SK_daoc_137"));
    if (TestNotNull(TEXT("Troll body imported"), Troll))
    {
        Body->SetSkeletalMesh(Troll);
        FDaocCharacterAppearance A;
        A.Model = 137;
        A.bEquipmentKnown = true;
        for (const auto Pair : {TPair<int32, int32>(25, 2787), {27, 2788}, {28, 2789}, {23, 2791}, {22, 2792}})
        {
            FDaocEquipmentAppearance E;
            E.Slot = Pair.Key;
            E.Model = Pair.Value;
            E.Extension = 2;
            A.Equipment.Add(E);
        }
        const auto HasTorso = [&](const TCHAR *Suffix) {
            TArray<USceneComponent *> Children;
            Body->GetChildrenComponents(false, Children);
            return Children.ContainsByPredicate([&](USceneComponent *C) {
                const auto *M = Cast<USkeletalMeshComponent>(C);
                return M && M->GetSkeletalMeshAsset() && M->GetSkeletalMeshAsset()->GetName() == Suffix;
            });
        };
        Renderer->ApplyAppearance(Body, Catalog, A);
        TestEqual(TEXT("Inconnu set has one component per armor part"), Renderer->ArmorPartCount, 5);
        TestTrue(TEXT("Extension two selects Body03"), HasTorso(TEXT("SK_equipment_f31_p1_b2")));
        A.Equipment[0].Extension = 3;
        Renderer->ApplyAppearance(Body, Catalog, A);
        TestTrue(TEXT("Extension-only packet changes rebuild shoulder plating"), HasTorso(TEXT("SK_equipment_f31_p1_b3")));
        TestFalse(TEXT("Previous torso is removed instead of overlapped"), HasTorso(TEXT("SK_equipment_f31_p1_b2")));
        TestEqual(TEXT("Changing extension does not duplicate armor"), Renderer->ArmorPartCount, 5);
        Renderer->ClearEquipment();
    }
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
