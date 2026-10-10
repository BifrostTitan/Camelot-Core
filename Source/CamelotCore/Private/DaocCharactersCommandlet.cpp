#include "DaocCharactersCommandlet.h"
#if WITH_EDITOR
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SkeletalMeshAttributes.h"
#include "UObject/SavePackage.h"

UTexture2D *DaocImportCharacterTexture(const FString &, const FString &);
UMaterial *DaocImportCharacterMaterial(const FString &, UTexture2D *, const FJsonObject &);
namespace
{
void PreserveSourceDetail(FSkeletalMeshLODInfo &LOD)
{
    // UE defaults to 50% reduction, including LOD 0. Character faces are already
    // low polygon meshes: reducing them destroys eyelids and atlas boundaries.
    auto &Reduction = LOD.ReductionSettings;
    Reduction.TerminationCriterion = SMTC_NumOfTriangles;
    Reduction.NumOfTrianglesPercentage = 1.0f;
    Reduction.NumOfVertPercentage = 1.0f;
    Reduction.MaxNumOfTrianglesPercentage = MAX_uint32;
    Reduction.MaxNumOfVertsPercentage = MAX_uint32;
    LOD.bHasBeenSimplified = false;
}
bool SaveCharacterAsset(UObject *Asset)
{
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
}
} // namespace
#endif
UDaocCharactersCommandlet::UDaocCharactersCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}
int32 UDaocCharactersCommandlet::Main(const FString &Params)
{
#if !WITH_EDITOR
    return 1;
#else
    FString File;
    float UnitScale = 2.54f;
    FParse::Value(*Params, TEXT("Input="), File);
    FParse::Value(*Params, TEXT("UnitScale="), UnitScale);
    FString Text;
    TSharedPtr<FJsonObject> Json;
    if (UnitScale <= 0 || !FMath::IsFinite(UnitScale) || !FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
        return 1;
    const TArray<TSharedPtr<FJsonValue>> *Bones = nullptr, *Sections = nullptr;
    if (!Json->TryGetArrayField(TEXT("bones"), Bones) || !Json->TryGetArrayField(TEXT("sections"), Sections) || Bones->Num() == 0 || Bones->Num() > 512 || Sections->Num() == 0)
        return 1;
    const int32 Model = Json->GetIntegerField(TEXT("model"));
    double ManifestScale;
    if (Json->TryGetNumberField(TEXT("unitScale"), ManifestScale))
    {
        UnitScale = ManifestScale;
        FParse::Value(*Params, TEXT("UnitScale="), UnitScale);
        if (!FMath::IsFinite(UnitScale) || UnitScale <= 0)
            return 1;
    }
    FString Name = FString::Printf(TEXT("SK_daoc_%d"), Model), Folder = TEXT("/Game/DAoC/Characters"), SharedSkeletonPath;
    Json->TryGetStringField(TEXT("assetName"), Name);
    Json->TryGetStringField(TEXT("assetFolder"), Folder);
    Json->TryGetStringField(TEXT("skeletonAsset"), SharedSkeletonPath);
    const FString PackageName = Folder + TEXT("/") + Name;
    if (!FPackageName::IsValidLongPackageName(PackageName) || FPaths::GetCleanFilename(Name) != Name)
        return 1;
    if (FParse::Param(*Params, TEXT("RestoreDetail")))
    {
        auto *Mesh = LoadObject<USkeletalMesh>(nullptr, *(PackageName + TEXT(".") + Name));
        if (!Mesh || Mesh->GetLODNum() != 1)
            return 1;
        const auto *Source = Mesh->GetMeshDescription(0);
        int32 ExpectedTriangles = 0;
        for (const auto &S : *Sections)
            ExpectedTriangles += S->AsObject()->GetArrayField(TEXT("indices")).Num() / 3;
        // Rebuild only when the unreduced import is still available. Never try
        // to recover lost topology by increasing a reduced render mesh's count.
        if (!Source || Source->Triangles().Num() != ExpectedTriangles)
        {
            UE_LOG(LogTemp, Error,
                   TEXT("Original character topology unavailable; explicit reimport "
                        "required"));
            return 1;
        }
        PreserveSourceDetail(*Mesh->GetLODInfo(0));
        Mesh->InvalidateDeriveDataCacheGUID();
        Mesh->Build();
        Mesh->PostEditChange();
        if (!SaveCharacterAsset(Mesh))
            return 1;
        UE_LOG(LogTemp, Display, TEXT("DAoC restored character %d source detail: %d triangles"), Model, ExpectedTriangles);
        return 0;
    }
    if (FParse::Param(*Params, TEXT("MaterialsOnly")))
    {
        for (int32 S = 0; S < Sections->Num(); ++S)
        {
            auto Section = (*Sections)[S]->AsObject();
            auto *Texture = DaocImportCharacterTexture(FPaths::GetPath(File), Section->GetStringField(TEXT("texturePng")));
            if (!Texture)
                return 1;
            auto *Material = DaocImportCharacterMaterial((Name == FString::Printf(TEXT("SK_daoc_%d"), Model) ? FString::Printf(TEXT("M_char_%d_%d"), Model, S) : FString::Printf(TEXT("M_%s_%d"), *Name, S)), Texture, *Section);
            if (!Material)
                return 1;
            Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
            Material->PostEditChange();
            if (!SaveCharacterAsset(Material))
                return 1;
        }
        UE_LOG(LogTemp, Display, TEXT("DAoC refreshed character %d material bindings"), Model);
        return 0;
    }
    // Preserve asset identity and animation bindings when explicitly rebuilding
    // geometry.
    USkeletalMesh *Existing = nullptr;
    if (FPackageName::DoesPackageExist(PackageName))
    {
        if (!FParse::Param(*Params, TEXT("Reimport")))
        {
            UE_LOG(LogTemp, Error,
                   TEXT("Character asset already exists: %s; use -Reimport to "
                        "refresh geometry"),
                   *PackageName);
            return 1;
        }
        Existing = LoadObject<USkeletalMesh>(nullptr, *(PackageName + TEXT(".") + Name));
        if (!Existing || !Existing->GetSkeleton() || Existing->GetLODNum() != 1)
            return 1;
    }
    FMeshDescription Description;
    FSkeletalMeshAttributes Attributes(Description);
    Attributes.Register();
    Attributes.GetVertexInstanceUVs().SetNumChannels(1);
    FReferenceSkeleton Reference;
    FReferenceSkeletonModifier Modifier(Reference, nullptr);
    TSet<FName> Names;
    for (int32 I = 0; I < Bones->Num(); ++I)
    {
        auto B = (*Bones)[I]->AsObject();
        const FName BoneName(*B->GetStringField(TEXT("name")));
        const int32 Parent = B->GetIntegerField(TEXT("parent"));
        if (BoneName.IsNone() || Names.Contains(BoneName) || (I == 0 ? Parent != -1 : Parent < 0 || Parent >= I))
            return 1;
        Names.Add(BoneName);
        const auto &T = B->GetArrayField(TEXT("translation"));
        const auto &R = B->GetArrayField(TEXT("rotation"));
        if (T.Num() != 3 || R.Num() != 9)
            return 1;
        // Reader stores row-vector matrices. Reflect both basis axes B R B into UE.
        FMatrix Matrix = FMatrix::Identity;
        const double Sign[] = {1, -1, 1};
        for (int32 Row = 0; Row < 3; ++Row)
            for (int32 Col = 0; Col < 3; ++Col)
                Matrix.M[Row][Col] = R[Row * 3 + Col]->AsNumber() * Sign[Row] * Sign[Col];
        FVector BasisScale = Matrix.ExtractScaling();
        if (BasisScale.GetMin() <= UE_SMALL_NUMBER)
            return 1;
        if (Matrix.Determinant() < 0)
        {
            BasisScale.X = -BasisScale.X;
            for (int32 Col = 0; Col < 3; ++Col)
                Matrix.M[0][Col] = -Matrix.M[0][Col];
        }
        FQuat Rotation(Matrix);
        Rotation.Normalize();
        const float Scale = B->GetNumberField(TEXT("scale"));
        if (!FMath::IsFinite(Scale) || Scale <= 0)
            return 1;
        const FTransform Pose(Rotation, FVector(T[0]->AsNumber(), -T[1]->AsNumber(), T[2]->AsNumber()) * UnitScale, BasisScale * Scale);
        if (Pose.ContainsNaN())
            return 1;
        Modifier.Add(FMeshBoneInfo(BoneName, BoneName.ToString(), Parent), Pose);
        const auto ID = Attributes.CreateBone();
        Attributes.GetBoneNames()[ID] = BoneName;
        Attributes.GetBoneParentIndices()[ID] = Parent;
        Attributes.GetBonePoses()[ID] = Pose;
    }
    Reference.RebuildRefSkeleton(nullptr, true);
    if (Existing)
    {
        const auto &Old = Existing->GetRefSkeleton();
        if (Old.GetNum() != Reference.GetNum())
            return 1;
        for (int32 I = 0; I < Reference.GetNum(); ++I)
            if (Old.GetBoneName(I) != Reference.GetBoneName(I) || Old.GetParentIndex(I) != Reference.GetParentIndex(I) || !Old.GetRefBonePose()[I].Equals(Reference.GetRefBonePose()[I], .001f))
            {
                UE_LOG(LogTemp, Error,
                       TEXT("Character reimport changes the skeleton; refusing to "
                            "invalidate animations"));
                return 1;
            }
    }
    TArray<FSkeletalMaterial> Materials;
    FBox Bounds(ForceInit);
    for (int32 S = 0; S < Sections->Num(); ++S)
    {
        auto Section = (*Sections)[S]->AsObject();
        const auto &Vertices = Section->GetArrayField(TEXT("vertices"));
        const auto &Weights = Section->GetArrayField(TEXT("weights"));
        const auto &Indices = Section->GetArrayField(TEXT("indices"));
        if (Weights.Num() != Vertices.Num() || Indices.Num() % 3)
            return 1;
        const FName Slot(*FString::Printf(TEXT("Part_%d"), S));
        const auto Group = Description.CreatePolygonGroup();
        Attributes.GetPolygonGroupMaterialSlotNames()[Group] = Slot;
        TArray<FVertexID> IDs;
        for (int32 V = 0; V < Vertices.Num(); ++V)
        {
            const auto &Values = Vertices[V]->AsArray();
            if (Values.Num() != 8)
                return 1;
            const FVector P(Values[0]->AsNumber() * UnitScale, -Values[1]->AsNumber() * UnitScale, Values[2]->AsNumber() * UnitScale);
            if (P.ContainsNaN())
                return 1;
            Bounds += P;
            const auto ID = Description.CreateVertex();
            IDs.Add(ID);
            Attributes.GetVertexPositions()[ID] = FVector3f(P);
            TArray<UE::AnimationCore::FBoneWeight> Influences;
            for (const auto &Value : Weights[V]->AsArray())
            {
                const auto &Pair = Value->AsArray();
                if (Pair.Num() != 2)
                    return 1;
                const int32 Bone = int32(Pair[0]->AsNumber());
                const float Weight = Pair[1]->AsNumber();
                if (Bone < 0 || Bone >= Bones->Num() || !FMath::IsFinite(Weight) || Weight <= 0)
                    return 1;
                Influences.Emplace(Bone, Weight);
            }
            if (Influences.IsEmpty())
                return 1;
            Attributes.GetVertexSkinWeights().Set(ID, UE::AnimationCore::FBoneWeights::Create(Influences));
        }
        for (int32 T = 0; T < Indices.Num(); T += 3)
        {
            TArray<FVertexInstanceID> Corners;
            for (int32 C = 0; C < 3; ++C)
            {
                const int32 V = int32(Indices[T + C]->AsNumber());
                if (!IDs.IsValidIndex(V))
                    return 1;
                auto ID = Description.CreateVertexInstance(IDs[V]);
                Corners.Add(ID);
                const auto &Values = Vertices[V]->AsArray();
                Attributes.GetVertexInstanceNormals()[ID] = FVector3f(Values[3]->AsNumber(), -Values[4]->AsNumber(), Values[5]->AsNumber());
                Attributes.GetVertexInstanceUVs().Set(ID, 0, FVector2f(Values[6]->AsNumber(), Values[7]->AsNumber()));
                Attributes.GetVertexInstanceColors()[ID] = FVector4f(1, 1, 1, 1);
            }
            Description.CreateTriangle(Group, Corners);
        }
        auto *Texture = DaocImportCharacterTexture(FPaths::GetPath(File), Section->GetStringField(TEXT("texturePng")));
        if (!Texture)
            return 1;
        auto *Material = DaocImportCharacterMaterial((Name == FString::Printf(TEXT("SK_daoc_%d"), Model) ? FString::Printf(TEXT("M_char_%d_%d"), Model, S) : FString::Printf(TEXT("M_%s_%d"), *Name, S)), Texture, *Section);
        if (!Material)
            return 1;
        Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
        Material->PostEditChange();
        if (!SaveCharacterAsset(Material))
            return 1;
        Materials.Emplace(Material, true, false, Slot, Slot);
    }
    auto *Mesh = Existing ? Existing : NewObject<USkeletalMesh>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
    if (Existing)
        Mesh->PreEditChange(nullptr);
    Mesh->SetRefSkeleton(Reference);
    Mesh->GetMaterials() = Materials;
    Mesh->SetImportedBounds(FBoxSphereBounds(Bounds));
    if (!Existing)
        Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
    auto &LOD = Existing ? *Mesh->GetLODInfo(0) : Mesh->AddLODInfo();
    LOD.BuildSettings.bRecomputeNormals = false;
    LOD.BuildSettings.bRecomputeTangents = true;
    PreserveSourceDetail(LOD);
    Mesh->CreateMeshDescription(0, MoveTemp(Description));
    Mesh->CommitMeshDescription(0);
    Mesh->CalculateInvRefMatrices();
    const FString SkeletonName = Name + TEXT("_Skeleton");
    auto *Skeleton = Existing ? Existing->GetSkeleton() : !SharedSkeletonPath.IsEmpty() ? LoadObject<USkeleton>(nullptr, *SharedSkeletonPath)
                                                                                        : NewObject<USkeleton>(CreatePackage(*(Folder + TEXT("/") + SkeletonName)), *SkeletonName, RF_Public | RF_Standalone);
    if (!Skeleton)
        return 1;
    Mesh->SetSkeleton(Skeleton);
    if (!Skeleton->MergeAllBonesToBoneTree(Mesh))
        return 1;
    Mesh->InvalidateDeriveDataCacheGUID();
    Mesh->Build();
    Mesh->PostEditChange();
    if (!Mesh->GetResourceForRendering() || Mesh->GetLODNum() != 1)
        return 1;
    if ((SharedSkeletonPath.IsEmpty() && !SaveCharacterAsset(Skeleton)) || !SaveCharacterAsset(Mesh))
        return 1;
    UE_LOG(LogTemp, Display, TEXT("DAoC skeletal model %d: %d bones, %d sections, height %.1f cm"), Model, Bones->Num(), Sections->Num(), Bounds.GetSize().Z);
    return 0;
#endif
}

bool UDaocCharactersCommandlet::ImportManifest(const FString &File, bool bReimport)
{
    if (File.Contains(TEXT("\"")))
        return false;
    auto *Commandlet = NewObject<UDaocCharactersCommandlet>();
    return Commandlet->Main(FString::Printf(TEXT("-Input=\"%s\" %s"), *File, bReimport ? TEXT("-Reimport") : TEXT(""))) == 0;
}
