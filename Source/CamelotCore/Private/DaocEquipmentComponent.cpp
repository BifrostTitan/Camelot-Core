#include "DaocEquipmentComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DaocArmorMeshComponent.h"
#include "DaocEquipmentCatalog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

void UDaocEquipmentComponent::SetPresentationActive(bool Active)
{
    if (Active && GetWorld())
        LastFullPresentation = GetWorld()->GetTimeSeconds();
    if (bDormant == !Active)
        return;
    if (!Active)
        DormantSockets.SetNum(Parts.Num());
    for (int32 I = 0; I < Parts.Num(); ++I)
        if (auto *Part = Parts[I].Get())
        {
            if (!Active)
            {
                DormantSockets[I] = Part->GetAttachSocketName();
                Part->SetHiddenInGame(true, true);
                // Detached cached gear does not follow every network transform or
                // enqueue scene updates while the GPU batch owns its visible copy.
                Part->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
            }
            else if (BodyMesh.IsValid())
            {
                Part->AttachToComponent(BodyMesh.Get(), FAttachmentTransformRules::KeepRelativeTransform, DormantSockets[I]);
                Part->SetHiddenInGame(false, true);
            }
            if (auto *Skeletal = Cast<USkeletalMeshComponent>(Part))
                Skeletal->SetComponentTickEnabled(Active);
        }
    bDormant = !Active;
    if (Active)
        DormantSockets.Reset();
}
bool UDaocEquipmentComponent::CanRetainForCrowd() const
{
    return GetWorld() && LastFullPresentation >= 0 && GetWorld()->GetTimeSeconds() - LastFullPresentation < 3;
}
int32 UDaocEquipmentComponent::SelectGeometryBit(int32 Mask, int32 Extension)
{
    if (Mask <= 0)
        return INDEX_NONE;
    int32 First = INDEX_NONE, Selected = INDEX_NONE;
    for (int32 Bit = 0; Bit < 31; ++Bit)
        if (Mask & (1 << Bit))
        {
            if (First == INDEX_NONE)
                First = Bit;
            if (Bit <= Extension)
                Selected = Bit;
        }
    return Selected == INDEX_NONE ? First : Selected;
}
void UDaocEquipmentComponent::ClearEquipment()
{
    for (auto Part : Parts)
        if (Part)
            Part->DestroyComponent();
    Parts.Reset();
    DormantSockets.Reset();
    bDormant = false;
    LastFullPresentation = -1;
    if (auto *Body = BodyMesh.Get())
        if (auto *Mesh = Body->GetSkeletalMeshAsset())
            for (int32 I = 0; I < Mesh->GetMaterials().Num(); ++I)
            {
                Body->ShowMaterialSection(I, I, true, 0);
                Body->SetMaterial(I, Mesh->GetMaterials()[I].MaterialInterface);
            }
    ArmorPartCount = WeaponCount = 0;
    bApplied = false;
    AppliedMesh = nullptr;
    AppliedCatalog = nullptr;
    BodyMesh.Reset();
}
void UDaocEquipmentComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    ClearEquipment();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}
void UDaocEquipmentComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearEquipment();
    Super::EndPlay(Reason);
}
void UDaocEquipmentComponent::ApplyAppearance(USkeletalMeshComponent *Body, UDaocEquipmentCatalog *Catalog, const FDaocCharacterAppearance &A)
{
    if (!Body || !Body->GetSkeletalMeshAsset() || !Catalog)
    {
        ClearEquipment();
        return;
    }
    uint32 Hash = HashCombine(GetTypeHash(A.Model), GetTypeHash(A.bEquipmentKnown));
    for (int32 V : {A.FaceType, A.HairStyle, A.HairColor, A.EyeColor})
        Hash = HashCombine(Hash, GetTypeHash(V));
    Hash = HashCombine(Hash, GetTypeHash(A.bHoodUp));
    Hash = HashCombine(Hash, GetTypeHash(A.RightWeapon | (A.LeftWeapon << 8)));
    for (const auto &E : A.Equipment)
    {
        Hash = HashCombine(Hash, GetTypeHash(E.Slot));
        Hash = HashCombine(Hash, GetTypeHash(E.Model));
        Hash = HashCombine(Hash, GetTypeHash(E.ColorOrEmblem));
        Hash = HashCombine(Hash, GetTypeHash(E.Effect));
        Hash = HashCombine(Hash, GetTypeHash(E.Extension));
    }
    if (bApplied && BodyMesh == Body && AppliedMesh == Body->GetSkeletalMeshAsset() && AppliedCatalog == Catalog && AppliedHash == Hash)
        return;
    ClearEquipment();
    BodyMesh = Body;
    AppliedMesh = Body->GetSkeletalMeshAsset();
    AppliedCatalog = Catalog;
    AppliedHash = Hash;
    bApplied = true;
    const auto *Figure = Catalog->Figures.FindByPredicate([&](const FDaocEquipmentFigure &F) { return F.Model == A.Model; });
    if (!Figure)
        return;
    struct Selection
    {
        int32 Mask = 0;
        TSoftObjectPtr<UTexture2D> Texture;
        int32 Color = 0;
        int32 Extension = 0;
    };
    Selection Selected[9];
    bool Suppressed[9] = {};
    bool EquippedCloak = false;
    const auto FindItem = [&](int32 Id) -> const FDaocEquipmentItem * {
        const auto *Item = Catalog->Items.FindByPredicate([&](const FDaocEquipmentItem &I) { return I.Model == Id && I.Figure == Figure->Figure; });
        if (!Item)
            Item = Catalog->Items.FindByPredicate([&](const FDaocEquipmentItem &I) { return I.Model == Id && I.Figure == 0; });
        if (!Item && !WarnedMissing.Contains(Id))
        {
            WarnedMissing.Add(Id);
            UE_LOG(LogTemp, Warning, TEXT("DAoC equipment model %d is not prepared for figure %d"), Id, Figure->Figure);
        }
        return Item;
    };
    const auto AddRigid = [&](const FDaocEquipmentItem &Item, const FName Bone) {
        auto *Mesh = Item.Mesh.LoadSynchronous();
        if (!Mesh || Body->GetBoneIndex(Bone) == INDEX_NONE)
            return;
        auto *C = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
        C->SetupAttachment(Body, Bone);
        C->SetStaticMesh(Mesh);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetRelativeScale3D(FVector(2.54));
        C->RegisterComponent();
        Parts.Add(C);
        // Rigid item NIFs use raw units; the body's skeletal import uses 2.54
        // cm/unit.
    };
    // Torso can define sleeve/leg geometry; explicit armor slots then supply
    // their own skins.
    for (int32 Slot : {25, 27, 28, 22, 23, 26, 21})
    {
        const auto *E = A.Equipment.FindByPredicate([&](const FDaocEquipmentAppearance &V) { return V.Slot == Slot && V.Model > 0; });
        if (!E)
            continue;
        const auto *Item = FindItem(E->Model);
        if (!Item)
            continue;
        if (Slot == 21 && !Item->Mesh.IsNull())
        {
            const int32 Before = Parts.Num();
            AddRigid(*Item, Body->GetBoneIndex(TEXT("Bip01 Helmet")) != INDEX_NONE ? TEXT("Bip01 Helmet") : TEXT("Bip01 Head"));
            if (Parts.Num() > Before)
                Suppressed[8] = Item->bHideHair;
            continue;
        }
        if (Slot == 26)
            EquippedCloak = true;
        for (int32 Part = 0; Part < 9; ++Part)
        {
            const int32 Mask = Item->PartMasks.IsValidIndex(Part) ? Item->PartMasks[Part] : 0;
            if (Mask < 0)
            {
                Suppressed[Part] = true;
                continue;
            }
            if (!Mask)
                continue;
            // Explicit sleeves and leggings supersede a torso's hide flags.
            Suppressed[Part] = false;
            Selected[Part].Mask = Mask;
            Selected[Part].Color = E->ColorOrEmblem;
            Selected[Part].Extension = (Slot == 25 && Part == 1) || (Slot == 22 && Part == 6) || (Slot == 23 && Part == 4) ? E->Extension : 0;
            if (Item->PartTextures.IsValidIndex(Part))
                Selected[Part].Texture = Item->PartTextures[Part];
        }
    }
    if (!EquippedCloak)
        Suppressed[7] = true;
    else
    {
        Selected[7].Mask = A.bHoodUp ? 2 : 1;
        if (A.bHoodUp)
            Suppressed[8] = true;
    }
    const auto *Custom = Catalog->Customizations.FindByPredicate([&](const FDaocFigureCustomization &C) { return C.Figure == Figure->Figure; });
    if (Custom && Catalog->FaceMaterial)
    {
        const int32 Face = A.FaceType >> 4;
        if (Custom->FaceTextures.IsValidIndex(Face))
            if (auto *Texture = Custom->FaceTextures[Face].LoadSynchronous())
            {
                auto *Material = UMaterialInstanceDynamic::Create(Catalog->FaceMaterial, Body);
                Material->SetTextureParameterValue(TEXT("EquipmentTexture"), Texture);
                for (int32 Section = 0; Section < Figure->BaseSectionParts.Num(); ++Section)
                    if (Figure->BaseSectionParts[Section] == 0)
                        Body->SetMaterial(Section, Material);
            }
    }
    for (int32 Part = 0; Part < 9; ++Part)
    {
        bool Replaced = false;
        if (Part == 8 && !Suppressed[8] && Custom && Catalog->HairMaterial)
        {
            const auto *Hair = Custom->Hair.FindByPredicate([&](const FDaocHairVariation &H) { return H.Style == (A.HairStyle >> 4); });
            const int32 Color = A.HairColor >> 4;
            if (Hair && Hair->Textures.IsValidIndex(Color))
                if (auto *Mesh = Hair->Mesh.LoadSynchronous())
                    if (auto *Texture = Hair->Textures[Color].LoadSynchronous())
                    {
                        auto *C = NewObject<UDaocArmorMeshComponent>(GetOwner(), NAME_None, RF_Transient);
                        C->SetupAttachment(Body);
                        C->SetSkeletalMesh(Mesh);
                        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                        C->RegisterComponent();
                        C->SetLeaderPoseComponent(Body, true);
                        auto *Material = UMaterialInstanceDynamic::Create(Catalog->HairMaterial, C);
                        Material->SetTextureParameterValue(TEXT("EquipmentTexture"), Texture);
                        Material->SetVectorParameterValue(TEXT("DyeColor"), Hair->Tints.IsValidIndex(Color) ? Hair->Tints[Color] : FLinearColor::White);
                        for (int32 I = 0; I < Mesh->GetMaterials().Num(); ++I)
                            C->SetMaterial(I, Material);
                        Parts.Add(C);
                        Replaced = true;
                    }
        }
        if (!Suppressed[Part] && Selected[Part].Mask > 0)
        {
            auto *Texture = Selected[Part].Texture.LoadSynchronous();
            if (Texture && Catalog->ArmorMaterial)
                for (const auto &G : Figure->Geometry)
                {
                    if (G.Part != Part || G.Bit != SelectGeometryBit(Selected[Part].Mask, Selected[Part].Extension))
                        continue;
                    auto *Mesh = G.Mesh.LoadSynchronous();
                    if (!Mesh)
                        continue;
                    auto *C = NewObject<UDaocArmorMeshComponent>(GetOwner(), NAME_None, RF_Transient);
                    C->SetupAttachment(Body);
                    C->SetSkeletalMesh(Mesh);
                    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    C->RegisterComponent();
                    C->SetLeaderPoseComponent(Body, true);
                    auto *Material = UMaterialInstanceDynamic::Create(Catalog->ArmorMaterial, C);
                    Material->SetTextureParameterValue(TEXT("EquipmentTexture"), Texture);
                    const auto *Dye = Catalog->DyeColors.Find(Selected[Part].Color);
                    Material->SetVectorParameterValue(TEXT("DyeColor"), Dye ? *Dye : FLinearColor::White);
                    for (int32 I = 0; I < Mesh->GetMaterials().Num(); ++I)
                        C->SetMaterial(I, Material);
                    Parts.Add(C);
                    ++ArmorPartCount;
                    Replaced = true;
                }
        }
        for (int32 Section = 0; Section < Figure->BaseSectionParts.Num(); ++Section)
            if (Figure->BaseSectionParts[Section] == Part)
                Body->ShowMaterialSection(Section, Section, !(Suppressed[Part] || Replaced), 0);
    }
    TSet<int32> HeldSlots;
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const int32 Active = Side == 0 ? A.RightWeapon : A.LeftWeapon;
        if (Active < 0 || Active > 3 || HeldSlots.Contains(Active))
            continue;
        const auto *E = A.Equipment.FindByPredicate([&](const FDaocEquipmentAppearance &V) { return V.Slot == 10 + Active && V.Model > 0; });
        if (!E)
            continue;
        const auto *Item = FindItem(E->Model);
        if (!Item || Item->Mesh.IsNull())
            continue;
        const FName Bone = Item->bShield ? (Side == 0 ? TEXT("Bip01 R Shield") : TEXT("Bip01 L Shield")) : (Side == 0 ? TEXT("Bip01 R Hand") : TEXT("Bip01 L Hand"));
        const int32 Before = Parts.Num();
        AddRigid(*Item, Bone);
        if (Parts.Num() > Before)
        {
            ++WeaponCount;
            HeldSlots.Add(Active);
        }
    }
    if (ArmorPartCount == 0 && !Parts.IsEmpty())
    {
        auto *Coverage = NewObject<UDaocArmorMeshComponent>(GetOwner(), NAME_None, RF_Transient);
        Coverage->SetupAttachment(Body);
        Coverage->SetSkeletalMesh(Body->GetSkeletalMeshAsset());
        Coverage->SetVisibility(false);
        Coverage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Coverage->RegisterComponent();
        Coverage->SetLeaderPoseComponent(Body, true);
        Parts.Add(Coverage);
    }
    if (!Parts.IsEmpty())
        Body->RecalcRequiredBones(0);
    UE_LOG(LogTemp, Display,
           TEXT("DAoC equipment model %d: %d equipped slots, %d armor parts, %d "
                "held weapons"),
           A.Model, A.Equipment.Num(), ArmorPartCount, WeaponCount);
}
