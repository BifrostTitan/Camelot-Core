#include "DaocObjectsCommandlet.h"
#include "DaocZoneRendering.h"
#include "DaocObjectAssets.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/Texture2D.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "DaocZoneActor.h"
#include "Engine/World.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "DaocTerrain.h"

namespace
{
bool Save(UObject *Asset)
{
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
}
TSharedPtr<FJsonObject> ReadJson(const FString &File)
{
    FString Text;
    TSharedPtr<FJsonObject> Object;
    if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object))
        return nullptr;
    return Object;
}
UTexture2D *ImportTexture(const FString &Directory, const FString &Filename)
{
    if (Filename.IsEmpty())
        return nullptr;
    const FString Name = FPaths::GetBaseFilename(Filename);
    const FString PackageName = TEXT("/Game/DAoC/ObjectTextures/") + Name;
    if (FPackageName::DoesPackageExist(PackageName))
        if (auto *Existing = LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn))
            return Existing;
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Directory, Filename)))
        return nullptr;
    auto &Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    auto Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
    TArray64<uint8> Raw;
    if (!Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
        return nullptr;
    const int32 W = Wrapper->GetWidth(), H = Wrapper->GetHeight();
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192 || int64(W) * H * 4 != Raw.Num())
        return nullptr;
    auto *Texture = NewObject<UTexture2D>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
    Texture->Source.Init(W, H, 1, 1, TSF_BGRA8, Raw.GetData());
    Texture->SRGB = true;
    Texture->CompressionSettings = TC_Default;
    Texture->AddressX = Texture->AddressY = TA_Wrap;
    Texture->PostEditChange();
    return Save(Texture) ? Texture : nullptr;
}
UMaterial *ImportMaterial(const FString &Name, UTexture2D *Texture, const FJsonObject &Section, bool bInstancedScenery = false)
{
    const FString PackageName = TEXT("/Game/DAoC/ObjectMaterials/") + Name;
    UMaterial *Material = FPackageName::DoesPackageExist(PackageName) ? LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn) : nullptr;
    if (Material)
    {
        const bool bNaniteMaterial = Material->BlendMode == BLEND_Opaque || Material->BlendMode == BLEND_Masked;
        if (bInstancedScenery && (!Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes) || (bNaniteMaterial && !Material->GetUsageByFlag(MATUSAGE_Nanite))))
        {
            Material->Modify();
            Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
            Material->SetUsageByFlag(MATUSAGE_Nanite, bNaniteMaterial);
            Material->PostEditChange();
            if (!Save(Material))
                return nullptr;
        }
        return Material;
    }
    Material = NewObject<UMaterial>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
    const uint32 Flags = Section.GetIntegerField(TEXT("alphaFlags"));
    Material->BlendMode = (Flags & 512) ? BLEND_Masked : ((Flags & 1) ? BLEND_Translucent : BLEND_Opaque);
    Material->TwoSided = Material->BlendMode != BLEND_Opaque;
    Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, bInstancedScenery);
    Material->SetUsageByFlag(MATUSAGE_Nanite, bInstancedScenery && (Material->BlendMode == BLEND_Opaque || Material->BlendMode == BLEND_Masked));
    Material->OpacityMaskClipValue = FMath::Clamp(float(Section.GetNumberField(TEXT("threshold"))) / 255.f, 0.001f, 0.999f);
    Material->SetShadingModel(MSM_DefaultLit);
    auto Add = [Material](UMaterialExpression *E) { Material->GetExpressionCollection().AddExpression(E); };
    auto *Color = NewObject<UMaterialExpressionConstant4Vector>(Material);
    const auto &D = Section.GetArrayField(TEXT("diffuse"));
    Color->Constant = FLinearColor(D[0]->AsNumber(), D[1]->AsNumber(), D[2]->AsNumber(), D[3]->AsNumber());
    Add(Color);
    auto *Vertex = NewObject<UMaterialExpressionVertexColor>(Material);
    Add(Vertex);
    auto *Tint = NewObject<UMaterialExpressionMultiply>(Material);
    Tint->A.Connect(0, Color);
    Tint->B.Connect(0, Vertex);
    Tint->A.Mask = 1;
    Tint->A.MaskR = Tint->A.MaskG = Tint->A.MaskB = 1;
    Tint->A.MaskA = 0;
    Add(Tint);
    auto *Alpha = NewObject<UMaterialExpressionMultiply>(Material);
    Alpha->A.Connect(4, Color);
    Alpha->B.Connect(4, Vertex);
    Add(Alpha);
    UMaterialExpression *ResultColor = Tint;
    UMaterialExpression *ResultAlpha = Alpha;
    if (Texture)
    {
        auto *Sample = NewObject<UMaterialExpressionTextureSample>(Material);
        Sample->Texture = Texture;
        Sample->SamplerType = SAMPLERTYPE_Color;
        Add(Sample);
        auto *Multiply = NewObject<UMaterialExpressionMultiply>(Material);
        Multiply->A.Connect(0, Sample);
        Multiply->B.Connect(0, Tint);
        Add(Multiply);
        ResultColor = Multiply;
        auto *MultiplyAlpha = NewObject<UMaterialExpressionMultiply>(Material);
        MultiplyAlpha->A.Connect(4, Sample);
        MultiplyAlpha->B.Connect(0, Alpha);
        Add(MultiplyAlpha);
        ResultAlpha = MultiplyAlpha;
    }
    auto *Roughness = NewObject<UMaterialExpressionConstant>(Material);
    Roughness->R = 1.f;
    Add(Roughness);
    auto *Specular = NewObject<UMaterialExpressionConstant>(Material);
    Specular->R = 0.f;
    Add(Specular);
    auto *Metallic = NewObject<UMaterialExpressionConstant>(Material);
    Metallic->R = 0.f;
    Add(Metallic);
    Material->GetEditorOnlyData()->BaseColor.Expression = ResultColor;
    Material->GetEditorOnlyData()->OpacityMask.Expression = ResultAlpha;
    Material->GetEditorOnlyData()->Opacity.Expression = ResultAlpha;
    Material->GetEditorOnlyData()->Roughness.Expression = Roughness;
    Material->GetEditorOnlyData()->Specular.Expression = Specular;
    Material->GetEditorOnlyData()->Metallic.Expression = Metallic;
    Material->PostEditChange();
    return Save(Material) ? Material : nullptr;
}
bool ValidSection(const FJsonObject &S)
{
    const TArray<TSharedPtr<FJsonValue>> *Vertices, *Indices, *Diffuse;
    double Flags, Threshold;
    FString Texture;
    if (!S.TryGetArrayField(TEXT("vertices"), Vertices) || !S.TryGetArrayField(TEXT("indices"), Indices) || !S.TryGetArrayField(TEXT("diffuse"), Diffuse) || !S.TryGetNumberField(TEXT("alphaFlags"), Flags) || !S.TryGetNumberField(TEXT("threshold"), Threshold) || !S.TryGetStringField(TEXT("texturePng"), Texture))
        return false;
    if (Vertices->IsEmpty() || Vertices->Num() > 1000000 || Indices->IsEmpty() || Indices->Num() > 6000000 || Indices->Num() % 3 || Diffuse->Num() != 4)
        return false;
    if (FPaths::GetCleanFilename(Texture) != Texture || Flags < 0 || Flags > 65535 || Threshold < 0 || Threshold > 255)
        return false;
    for (const auto &V : *Vertices)
    {
        const TArray<TSharedPtr<FJsonValue>> *Row;
        if (!V->TryGetArray(Row) || Row->Num() != 12)
            return false;
        for (const auto &N : *Row)
        {
            double Number;
            if (!N->TryGetNumber(Number) || !FMath::IsFinite(Number) || FMath::Abs(Number) > 1e9)
                return false;
        }
    }
    for (const auto &I : *Indices)
    {
        double N;
        if (!I->TryGetNumber(N) || !FMath::IsFinite(N) || N < 0 || N >= Vertices->Num() || N != FMath::FloorToDouble(N))
            return false;
    }
    for (const auto &I : *Diffuse)
    {
        double N;
        if (!I->TryGetNumber(N) || !FMath::IsFinite(N))
            return false;
    }
    return true;
}
UStaticMesh *ImportMesh(const FString &Directory, const FString &NifName, const FString &Filename, bool bReimport)
{
    if (FPaths::GetCleanFilename(Filename) != Filename)
        return nullptr;
    auto Data = ReadJson(FPaths::Combine(Directory, Filename));
    const TArray<TSharedPtr<FJsonValue>> *Sections;
    double Version;
    if (!Data || !Data->TryGetNumberField(TEXT("version"), Version) || Version != 1 || !Data->TryGetArrayField(TEXT("sections"), Sections) || Sections->IsEmpty() || Sections->Num() > 1024)
        return nullptr;
    int64 TotalVertices = 0;
    for (const auto &S : *Sections)
    {
        if (S->Type != EJson::Object || !ValidSection(*S->AsObject()))
            return nullptr;
        TotalVertices += S->AsObject()->GetArrayField(TEXT("vertices")).Num();
        if (TotalVertices > 2000000)
            return nullptr;
    }
    const FString Name = DaocObjectAssets::AssetName(NifName);
    const FString PackageName = TEXT("/Game/DAoC/Nifs/") + Name;
    UStaticMesh *Mesh = FPackageName::DoesPackageExist(PackageName) ? LoadObject<UStaticMesh>(nullptr, *DaocObjectAssets::ObjectPath(NifName), nullptr, LOAD_NoWarn) : nullptr;
    // Loading an asset can start a background rebuild from DDC. Do not replace
    // its source models while MeshUtilities still reads their vertex buffers.
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (Mesh && !bReimport)
        return Mesh;
    if (Mesh)
        Mesh->PreEditChange(nullptr);
    if (!Mesh)
    {
        Mesh = NewObject<UStaticMesh>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
        Mesh->InitResources();
    }
    Mesh->SetNumSourceModels(0);
    Mesh->GetStaticMaterials().Reset();
    Mesh->SetLightingGuid();
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    auto Normals = Attributes.GetVertexInstanceNormals();
    auto Colors = Attributes.GetVertexInstanceColors();
    auto UVs = Attributes.GetVertexInstanceUVs();
    UVs.SetNumChannels(1);
    auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
    int32 SectionIndex = 0;
    bool bMissingNormals = false;
    for (const auto &SectionValue : *Sections)
    {
        const auto Section = SectionValue->AsObject();
        const FString TextureFile = Section->GetStringField(TEXT("texturePng"));
        UTexture2D *Texture = ImportTexture(Directory, TextureFile);
        if (!TextureFile.IsEmpty() && !Texture)
            return nullptr;
        const FString MaterialName = TEXT("M_") + Name + TEXT("_") + FString::FromInt(SectionIndex++);
        UMaterial *Material = ImportMaterial(MaterialName, Texture, *Section, true);
        if (!Material)
            return nullptr;
        Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, *MaterialName, *MaterialName));
        const FPolygonGroupID Group = Description.CreatePolygonGroup();
        SlotNames[Group] = *MaterialName;
        const auto &Vertices = Section->GetArrayField(TEXT("vertices"));
        TArray<FVertexID> IDs;
        IDs.Reserve(Vertices.Num());
        for (const auto &Value : Vertices)
        {
            const auto &V = Value->AsArray();
            FVertexID ID = Description.CreateVertex();
            IDs.Add(ID);
            // C# rotates Z-up NIFs to Y-up; swapping back to UE Z-up reflects Y.
            Positions[ID] = FVector3f(V[0]->AsNumber(), -V[1]->AsNumber(), V[2]->AsNumber());
        }
        const auto &Indices = Section->GetArrayField(TEXT("indices"));
        for (int32 I = 0; I < Indices.Num(); I += 3)
        {
            TArray<FVertexInstanceID> Triangle;
            // NIF is counterclockwise. Reflecting Y already makes it clockwise
            // for UE; swapping corners again would turn every exterior inside out.
            for (int32 Corner : {0, 1, 2})
            {
                int32 Index = int32(Indices[I + Corner]->AsNumber());
                const auto &V = Vertices[Index]->AsArray();
                FVertexInstanceID ID = Description.CreateVertexInstance(IDs[Index]);
                Triangle.Add(ID);
                Normals[ID] = FVector3f(V[3]->AsNumber(), -V[4]->AsNumber(), V[5]->AsNumber());
                bMissingNormals |= Normals[ID].IsNearlyZero();
                UVs.Set(ID, 0, FVector2f(V[6]->AsNumber(), V[7]->AsNumber()));
                Colors[ID] = FVector4f(V[8]->AsNumber(), V[9]->AsNumber(), V[10]->AsNumber(), V[11]->AsNumber());
            }
            Description.CreatePolygon(Group, Triangle);
        }
    }
    auto &Source = Mesh->AddSourceModel();
    Source.BuildSettings.bRecomputeNormals = bMissingNormals;
    Source.BuildSettings.bRecomputeTangents = true;
    Source.BuildSettings.bGenerateLightmapUVs = false;
    Source.BuildSettings.bUseFullPrecisionUVs = true;
    // Keep source geometry and walking collision intact. Unsupported material
    // sections retain the conventional mesh, including after a reimport.
    auto NaniteSettings = Mesh->GetNaniteSettings();
    NaniteSettings.bEnabled = false;
    Mesh->SetNaniteSettings(NaniteSettings);
    bool bMasked = false;
    for (const FStaticMaterial &Slot : Mesh->GetStaticMaterials())
        bMasked |= Slot.MaterialInterface && Slot.MaterialInterface->GetBlendMode() == BLEND_Masked;
    DaocZoneRendering::ConfigureNanite(*Mesh, bMasked);
    UStaticMesh::FBuildMeshDescriptionsParams Build;
    Build.bFastBuild = false;
    if (!Mesh->BuildFromMeshDescriptions({&Description}, Build))
        return nullptr;
    UDaocObjectsCommandlet::EnableWalkingCollision(Mesh);
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (!Mesh->GetRenderData() || Mesh->GetBounds().SphereRadius <= 0 || !Save(Mesh))
        return nullptr;
    UE_LOG(LogTemp, Display, TEXT("Imported %s: %d sections, %lld vertices, bounds %s"), *NifName, SectionIndex, TotalVertices, *Mesh->GetBounds().BoxExtent.ToString());
    return Mesh;
}
} // namespace
UTexture2D *DaocImportCharacterTexture(const FString &Directory, const FString &Filename)
{
    return ImportTexture(Directory, Filename);
}
UMaterial *DaocImportCharacterMaterial(const FString &Name, UTexture2D *Texture, const FJsonObject &Section)
{
    auto *Material = ImportMaterial(Name, Texture, Section);
    if (Material)
    {
        for (auto Expression : Material->GetExpressions())
            if (auto *Sample = Cast<UMaterialExpressionTextureSample>(Expression.Get()))
                Sample->Texture = Texture;
        const uint32 Flags = Section.GetIntegerField(TEXT("alphaFlags"));
        Material->BlendMode = (Flags & 512) ? BLEND_Masked : ((Flags & 1) ? BLEND_Translucent : BLEND_Opaque);
        Material->TwoSided = Material->BlendMode != BLEND_Opaque;
        Material->OpacityMaskClipValue = FMath::Clamp(float(Section.GetNumberField(TEXT("threshold"))) / 255.f, 0.001f, 0.999f);
    }
    return Material;
}
#endif

UDaocObjectsCommandlet::UDaocObjectsCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}
int32 UDaocObjectsCommandlet::Main(const FString &Params)
{
#if WITH_EDITOR
    if (FParse::Param(*Params, TEXT("SelfTest")))
    {
        // Ask UE's own polygon-plane implementation which side is front-facing.
        FMeshDescription TestMesh;
        FStaticMeshAttributes TestAttributes(TestMesh);
        TestAttributes.Register();
        TArray<FVertexInstanceID> TestVertices;
        for (FVector3f Position : {FVector3f(0, 0, 0), FVector3f(1, 0, 0), FVector3f(0, -1, 0)})
        {
            FVertexID V = TestMesh.CreateVertex();
            TestAttributes.GetVertexPositions()[V] = Position;
            TestVertices.Add(TestMesh.CreateVertexInstance(V));
        }
        TestMesh.CreatePolygon(TestMesh.CreatePolygonGroup(), TestVertices);
        FStaticMeshOperations::ComputeTriangleTangentsAndNormals(TestMesh);
        if (TestAttributes.GetTriangleNormals()[FTriangleID(0)].Z < .99)
        {
            UE_LOG(LogTemp, Error, TEXT("Reflected NIF front-face regression failed"));
            return 1;
        }
        const FVector TerrainCorners[] = {FVector(0, 0, 0), FVector(1, 0, 0), FVector(0, 1, 0), FVector(1, 1, 0)};
        int Cell[6];
        DaocTerrain::CellTriangles(0, 1, 2, 3, Cell);
        for (int T = 0; T < 6; T += 3)
            if (FVector::CrossProduct(TerrainCorners[Cell[T + 2]] - TerrainCorners[Cell[T]], TerrainCorners[Cell[T + 1]] - TerrainCorners[Cell[T]]).Z <= 0)
            {
                UE_LOG(LogTemp, Error, TEXT("Terrain front-face regression failed"));
                return 1;
            }
        UE_LOG(LogTemp, Display, TEXT("UE clockwise NIF and terrain front-face regressions passed."));
        const auto Reflect = [](FVector V) { return FVector(V.X, -V.Y, V.Z); };
        const double Angles[] = {-double(PI), -double(HALF_PI), .3, double(HALF_PI), double(PI)};
        for (FVector Axis : {FVector::UpVector, FVector::ForwardVector, FVector(1, 2, 3).GetSafeNormal()})
            for (double Angle : Angles)
            {
                FVector P(3, 4, 7);
                FVector Expected = Reflect(FQuat(Axis, Angle).RotateVector(P));
                FVector Actual = FQuat(DaocObjectAssets::ImportedRotationAxis(Axis), Angle).RotateVector(Reflect(P));
                if (!Actual.Equals(Expected, 1e-5))
                {
                    UE_LOG(LogTemp, Error, TEXT("NIF heading/basis regression failed"));
                    return 1;
                }
            }
        UE_LOG(LogTemp, Display, TEXT("NIF heading/basis regressions passed."));
    }
    FString Directory;
    if (!FParse::Value(*Params, TEXT("ObjectDirectory="), Directory))
        return 1;
    auto Manifest = ReadJson(FPaths::Combine(Directory, TEXT("manifest.json")));
    const TArray<TSharedPtr<FJsonValue>> *Models;
    if (!Manifest || !Manifest->TryGetArrayField(TEXT("models"), Models) || Models->IsEmpty())
        return 1;
    const TArray<TSharedPtr<FJsonValue>> *Failures;
    if (!Manifest->TryGetArrayField(TEXT("failures"), Failures) || !Failures->IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Object preparation has failures; resolve them before importing."));
        return 1;
    }
    int32 Count = 0;
    for (const auto &Model : *Models)
    {
        if (Model->Type != EJson::Object)
            return 1;
        FString Name, File;
        if (!Model->AsObject()->TryGetStringField(TEXT("name"), Name) || !Model->AsObject()->TryGetStringField(TEXT("file"), File))
            return 1;
        UStaticMesh *Mesh = ImportMesh(Directory, Name, File, FParse::Param(*Params, TEXT("Reimport")));
        if (!Mesh)
        {
            UE_LOG(LogTemp, Error, TEXT("Object import failed: %s"), *Name);
            return 1;
        }
        if (FParse::Param(*Params, TEXT("SelfTest")))
        {
            // Check the actual saved mesh description, not just a synthetic ordering.
            FMeshDescription *Description = Mesh->GetMeshDescription(0);
            if (!Description)
                return 1;
            FStaticMeshAttributes Attributes(*Description);
            FStaticMeshOperations::ComputeTriangleTangentsAndNormals(*Description);
            auto Normals = Attributes.GetVertexInstanceNormals();
            int32 Agree = 0, Disagree = 0;
            for (FTriangleID Triangle : Description->Triangles().GetElementIDs())
            {
                auto Vertices = Description->GetTriangleVertexInstances(Triangle);
                FVector Normal(Attributes.GetTriangleNormals()[Triangle]);
                FVector Reference = FVector::ZeroVector;
                for (FVertexInstanceID V : Vertices)
                    Reference += FVector(Normals[V]);
                double Dot = FVector::DotProduct(Normal, Reference.GetSafeNormal());
                if (Dot > .01)
                    ++Agree;
                else if (Dot < -.01)
                    ++Disagree;
            }
            // Some original models intentionally contain reversed/internal faces.
            // A reversed importer instead makes almost every polygon disagree.
            if (Disagree > Agree && Disagree > 10)
            {
                UE_LOG(LogTemp, Error, TEXT("%s has reversed faces: %d agree, %d disagree"), *Name, Agree, Disagree);
                return 1;
            }
            UE_LOG(LogTemp, Display, TEXT("Front faces %s: %d agree, %d disagree with source normals."), *Name, Agree, Disagree);
        }
        ++Count;
    }
    FAssetCompilingManager::Get().FinishAllCompilation();
    UE_LOG(LogTemp, Display, TEXT("DAoC object import completed: %d static meshes."), Count);
    FString Zone;
    if (FParse::Value(*Params, TEXT("ZoneDirectory="), Zone))
    {
        UWorld *World = UWorld::CreateWorld(EWorldType::Editor, false);
        ADaocZoneActor *Actor = World->SpawnActor<ADaocZoneActor>();
        if (!Actor || !Actor->LoadZoneExportFromDirectory(Zone))
            return 1;
        TArray<UProceduralMeshComponent *> TerrainComponents;
        Actor->GetComponents(TerrainComponents);
        if (TerrainComponents.IsEmpty())
            return 1;
        FProcMeshSection *Terrain = TerrainComponents[0]->GetProcMeshSection(0);
        if (!Terrain)
            return 1;
        for (const FProcMeshVertex &Vertex : Terrain->ProcVertexBuffer)
        {
            if (!Vertex.Normal.IsNormalized() || Vertex.Normal.Z <= 0)
            {
                UE_LOG(LogTemp, Error, TEXT("Generated terrain lighting normals point downward or are invalid"));
                return 1;
            }
        }
        for (int I = 0; I < Terrain->ProcIndexBuffer.Num(); I += 3)
        {
            const auto &A = Terrain->ProcVertexBuffer[Terrain->ProcIndexBuffer[I]];
            const auto &B = Terrain->ProcVertexBuffer[Terrain->ProcIndexBuffer[I + 1]];
            const auto &C = Terrain->ProcVertexBuffer[Terrain->ProcIndexBuffer[I + 2]];
            const FVector FaceNormal = FVector::CrossProduct(C.Position - A.Position, B.Position - A.Position).GetSafeNormal();
            if (FaceNormal.Z <= 0 || FVector::DotProduct(FaceNormal, A.Normal + B.Normal + C.Normal) <= 0)
            {
                UE_LOG(LogTemp, Error, TEXT("Generated terrain has downward front faces"));
                return 1;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("Generated terrain front faces and all lighting normals point upward and agree."));
        TArray<UInstancedStaticMeshComponent *> Components;
        Actor->GetComponents(Components);
        int32 MaskedGroups = 0, SolidGroups = 0;
        for (auto *Component : Components)
        {
            bool bMasked = false;
            for (const FStaticMaterial &Slot : Component->GetStaticMesh()->GetStaticMaterials())
                bMasked |= Slot.MaterialInterface && Slot.MaterialInterface->GetBlendMode() == BLEND_Masked;
            if (Component->CastShadow == bMasked)
            {
                UE_LOG(LogTemp, Error, TEXT("Classic scenery shadow classification failed"));
                return 1;
            }
            if (bMasked)
                ++MaskedGroups;
            else
                ++SolidGroups;
        }
        Actor->SetMaskedSceneryShadows(true);
        Actor->GetComponents(Components);
        for (auto *Component : Components)
            if (!Component->CastShadow)
            {
                UE_LOG(LogTemp, Error, TEXT("Scenery shadow override failed"));
                return 1;
            }
        Actor->SetMaskedSceneryShadows(false);
        Actor->GetComponents(Components);
        UE_LOG(LogTemp, Display, TEXT("Scenery shadow control passed: %d masked groups, %d solid groups; override restored shadows."), MaskedGroups, SolidGroups);
        int32 Instances = 0;
        for (auto *Component : Components)
            Instances += Component->GetInstanceCount();
        UE_LOG(LogTemp, Display, TEXT("Zone fixture verification: %d mesh groups, %d instances."), Components.Num(), Instances);
        Actor->RebuildZone();
        Actor->GetComponents(Components);
        int32 Rebuilt = 0;
        for (auto *Component : Components)
            Rebuilt += Component->GetInstanceCount();
        if (Rebuilt != Instances)
        {
            UE_LOG(LogTemp, Error, TEXT("Fixture rebuild duplicated or lost instances"));
            return 1;
        }
        TArray<FString> Rows;
        if (!FFileHelper::LoadFileToStringArray(Rows, *FPaths::Combine(Zone, TEXT("fixtures.csv"))))
            return 1;
        int32 Expected = 0;
        for (const FString &Row : Rows)
        {
            FString ID, Rest;
            if (Row.Split(TEXT(","), &ID, &Rest) && ID.TrimStartAndEnd().IsNumeric())
                ++Expected;
        }
        if (Instances != Expected)
        {
            UE_LOG(LogTemp, Error, TEXT("Fixture coverage: expected %d, created %d"), Expected, Instances);
            return 1;
        }
        UE_LOG(LogTemp, Display, TEXT("Fixture coverage and repeated rebuild passed: %d / %d."), Instances, Expected);
        World->DestroyWorld(false);
        if (!Instances)
            return 1;
    }
    return 0;
#else
    return 1;
#endif
}

bool UDaocObjectsCommandlet::EnableWalkingCollision(UStaticMesh *Mesh)
{
#if WITH_EDITOR
    if (!Mesh)
        return false;
    Mesh->Modify();
    Mesh->CreateBodySetup();
    auto *Body = Mesh->GetBodySetup();
    Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
    Body->bDoubleSidedGeometry = true;
    Body->InvalidatePhysicsData();
    Body->CreatePhysicsMeshes();
    Mesh->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

bool UDaocObjectsCommandlet::ImportManifest(const FString &File, bool bReimport)
{
    if (File.Contains(TEXT("\"")))
        return false;
    auto *Commandlet = NewObject<UDaocObjectsCommandlet>();
    return Commandlet->Main(FString::Printf(TEXT("-ObjectDirectory=\"%s\" %s"), *File, bReimport ? TEXT("-Reimport") : TEXT(""))) == 0;
}

UTexture2D *UDaocObjectsCommandlet::ImportTextureAsset(const FString &Directory, const FString &Filename)
{
#if WITH_EDITOR
    if (FPaths::GetCleanFilename(Filename) != Filename)
        return nullptr;
    return ImportTexture(Directory, Filename);
#else
    return nullptr;
#endif
}
