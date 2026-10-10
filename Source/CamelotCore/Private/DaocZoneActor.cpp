#include "DaocZoneActor.h"
#include "DaocZoneProfile.h"
#include "DaocClientAssetCatalog.h"
#include "DaocObjectAssets.h"
#include "DaocZoneRendering.h"
#include "DaocPcx.h"
#include "DaocTerrain.h"
#include "DaocTerrainTextures.h"
#include "String/LexFromString.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
constexpr float DaocUnitFactor = 255.0f / 65536.0f;

bool UsesNanite(const UStaticMesh *Mesh)
{
#if WITH_EDITOR
    return Mesh->IsNaniteEnabled();
#else
    return Mesh->HasValidNaniteData();
#endif
}

int32 ZoneIdFromExportDirectory(FString Directory)
{
    FPaths::NormalizeDirectoryName(Directory);
    const FString Folder = FPaths::GetCleanFilename(Directory);
    if (!Folder.StartsWith(TEXT("zone"), ESearchCase::IgnoreCase))
        return INDEX_NONE;
    const FString Digits = Folder.Mid(4);
    if (Digits.IsEmpty())
        return INDEX_NONE;
    for (TCHAR Character : Digits)
        if (!FChar::IsDigit(Character))
            return INDEX_NONE;
    int32 Id = INDEX_NONE;
    return LexTryParseString(Id, *Digits) && Id >= 0 && Id <= 65535 ? Id : INDEX_NONE;
}

bool HasMaskedMaterial(UStaticMesh *Mesh)
{
    for (const FStaticMaterial &Slot : Mesh->GetStaticMaterials())
        if (Slot.MaterialInterface && Slot.MaterialInterface->GetBlendMode() == BLEND_Masked)
            return true;
    return false;
}

UStaticMesh *MeshForLayer(const FDaocImportedNif &Nif, EDaocMeshLayer Layer)
{
    switch (Layer)
    {
    case EDaocMeshLayer::Collidee:
        return Nif.CollideeMesh;
    case EDaocMeshLayer::Pickee:
        return Nif.PickeeMesh;
    case EDaocMeshLayer::Visible:
    default:
        return Nif.VisibleMesh;
    }
}
} // namespace

ADaocZoneActor::ADaocZoneActor()
{
    PrimaryActorTick.bCanEverTick = false;
    TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
    RootComponent = TerrainMesh;
    TerrainMesh->SetMobility(EComponentMobility::Static);
    TerrainMesh->bUseAsyncCooking = true;
    WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Water"));
    WaterMesh->SetupAttachment(RootComponent);
    WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaterMesh->SetCastShadow(false);
}

void ADaocZoneActor::OnConstruction(const FTransform &Transform)
{
    Super::OnConstruction(Transform);
    RebuildZone();
}

void ADaocZoneActor::RebuildZone()
{
    // A playable pawn must have a floor on its first frame.
    TerrainMesh->bUseAsyncCooking = !GetWorld() || !GetWorld()->IsGameWorld();
    TerrainMesh->SetCollisionProfileName(TEXT("BlockAll"));
    // Recover IDs for existing saved levels as well as newly loaded exports.
    if (ZoneId == INDEX_NONE)
        ZoneId = ZoneIdFromExportDirectory(ZoneExportDirectory);
    ApplyProfileAppearance();
    RebuildTerrain();
    RebuildWater();
    RebuildInstances();
    RebuildGroundCover();
}

bool ADaocZoneActor::IsEnhancementZone() const
{
    if (SelectedZoneProfile && SelectedZoneProfile->ZoneId == ZoneId)
        return bEnableEnhancement;
    FString A = FPaths::ConvertRelativePathToFull(ZoneExportDirectory);
    FString B = FPaths::ConvertRelativePathToFull(EnhancementZoneDirectory);
    FPaths::NormalizeDirectoryName(A);
    FPaths::NormalizeDirectoryName(B);
    return bEnableEnhancement && !EnhancementZoneDirectory.IsEmpty() && A.Equals(B, ESearchCase::IgnoreCase);
}

void ADaocZoneActor::ApplyProfileAppearance()
{
    if (!SelectedZoneProfile || SelectedZoneProfile->ZoneId != ZoneId)
        return;
    const auto *P = SelectedZoneProfile.Get();
    const auto *Defaults = P->bUseDefaultEnhancement && ClientAssetCatalog ? ClientAssetCatalog->DefaultEnhancementSettings.Get() : nullptr;
    TerrainTextureMaterial = P->OriginalTerrainMaterial;
    EnhancedTerrainMaterial = P->EnhancedTerrainMaterial;
    if (!EnhancedTerrainMaterial && Defaults)
        EnhancedTerrainMaterial = Defaults->EnhancedTerrainMaterial;
    SceneryReplacements = P->SceneryReplacements;
    if (Defaults)
        for (const auto &R : Defaults->SceneryReplacements)
            if (!SceneryReplacements.ContainsByPredicate([&](const FDaocSceneryReplacement &Local) { return FPaths::GetBaseFilename(Local.NifName.ToString()).Equals(FPaths::GetBaseFilename(R.NifName.ToString()), ESearchCase::IgnoreCase); }))
                SceneryReplacements.Add(R);
    const auto *Cover = !Defaults || P->bOverrideGroundCover || !P->GroundCoverMeshes.IsEmpty() ? P : Defaults;
    GroundCoverMeshes = Cover->GroundCoverMeshes;
    GroundCoverSpacing = Cover->GroundCoverSpacing;
    GroundCoverFixturePadding = FMath::Max(0.f, P->GroundCoverFixturePadding);
    GroundCoverMinimumGrassWeight = P->GroundCoverMinimumGrassWeight;
    GroundCoverFullDensityGrassWeight = P->GroundCoverFullDensityGrassWeight;
    GroundCoverScaleMultiplier = Cover->GroundCoverScaleMultiplier;
    GroundCoverTallScaleMultiplier = Cover->GroundCoverTallScaleMultiplier;
    GroundCoverTallPatchChance = Cover->GroundCoverTallPatchChance;
    GroundCoverCullDistance = Cover->GroundCoverCullDistance;
    // Tint and optional material always belong to the selected zone.
    GroundCoverTint = Defaults ? P->GroundCoverTint * Defaults->GroundCoverTint : P->GroundCoverTint;
    GroundCoverMaterialOverride = P->GroundCoverMaterialOverride ? P->GroundCoverMaterialOverride : Cover->GroundCoverMaterialOverride;
    TerrainRoughness = P->TerrainRoughness;
    bCastMaskedSceneryShadows = P->bCastMaskedSceneryShadows;
    GroundCoverMaskWidth = P->GroundCoverMaskWidth;
    GroundCoverMask = P->GroundCoverMask;
}

bool ADaocZoneActor::LoadSelectedZoneProfile()
{
    return LoadZoneProfile(SelectedZoneProfile);
}

bool ADaocZoneActor::LoadZoneById(int32 NewZoneId)
{
    UDaocZoneProfile *Found = nullptr;
    for (UDaocZoneProfile *Profile : ZoneProfiles)
        if (Profile && Profile->ZoneId == NewZoneId)
        {
            if (Found)
            {
                UE_LOG(LogTemp, Error, TEXT("Duplicate zone profiles for ID %d"), NewZoneId);
                return false;
            }
            Found = Profile;
        }
    if (!Found && ClientAssetCatalog)
        if (const auto *Authored = ClientAssetCatalog->ZoneEnhancementProfiles.Find(NewZoneId))
            Found = Authored->LoadSynchronous();
    if (!Found && ClientAssetCatalog)
        if (const auto *Classic = ClientAssetCatalog->ClassicZones.Find(NewZoneId))
            Found = Classic->LoadSynchronous();
    if (!Found)
    {
        UE_LOG(LogTemp, Warning, TEXT("No prepared outdoor zone profile for ID %d"), NewZoneId);
        return false;
    }
    // Appearance profiles must retain their zone's imported terrain. A renamed
    // profile can otherwise load another zone's geometry under the correct ID.
    if (ClientAssetCatalog)
        if (const auto *Reference = ClientAssetCatalog->ClassicZones.Find(NewZoneId))
            if (const auto *Classic = Reference->LoadSynchronous(); Classic && Classic != Found && Classic->bHasTerrain)
            {
                bool Same = Found->TerrainWidth == Classic->TerrainWidth && Found->TerrainHeight == Classic->TerrainHeight && Found->TerrainHeightSamples.Num() == Classic->TerrainHeightSamples.Num();
                if (Same)
                    for (int32 I = 0; I < Found->TerrainHeightSamples.Num(); ++I)
                        if (!FMath::IsNearlyEqual(Found->TerrainHeightSamples[I], Classic->TerrainHeightSamples[I], .0001f))
                        {
                            Same = false;
                            break;
                        }
                if (!Same)
                {
                    UE_LOG(LogTemp, Error, TEXT("Zone %d appearance profile %s has stale imported terrain; refresh it from %s"), NewZoneId, *Found->GetPathName(), *Classic->GetPathName());
                    return false;
                }
            }
    return LoadZoneProfile(Found);
}

bool ADaocZoneActor::LoadZoneProfile(UDaocZoneProfile *Profile)
{
    // Validate before replacing the current zone. Profiles contain prepared data
    // and asset references, so switching requires no external files or editor import.
    if (!Profile || Profile->ZoneId < 0 || (Profile->bHasTerrain && (Profile->TerrainWidth < 2 || Profile->TerrainHeight < 2 || int64(Profile->TerrainWidth) * Profile->TerrainHeight != Profile->TerrainHeightSamples.Num() || Profile->TerrainHeightSamples.Num() > 4194304)))
        return false;
    for (float H : Profile->TerrainHeightSamples)
        if (!FMath::IsFinite(H))
            return false;
    if (!Profile->GroundCoverMask.IsEmpty() && (Profile->GroundCoverMaskWidth < 8 || Profile->GroundCoverMaskWidth > 2048 || Profile->GroundCoverMask.Num() != Profile->GroundCoverMaskWidth * Profile->GroundCoverMaskWidth))
        return false;
    for (const auto &Placement : Profile->Placements)
        if (Placement.Layer == EDaocMeshLayer::Visible && !Profile->ImportedNifs.ContainsByPredicate([&](const FDaocImportedNif &N) { return N.NifName.IsEqual(Placement.NifName, ENameCase::IgnoreCase) && N.VisibleMesh; }))
        {
            UE_LOG(LogTemp, Warning, TEXT("Zone %d is not prepared: missing %s"), Profile->ZoneId, *Placement.NifName.ToString());
            return false;
        }
    const int32 ExportId = ZoneIdFromExportDirectory(Profile->ExportDirectory);
    if (ExportId >= 0 && ExportId != Profile->ZoneId)
    {
        UE_LOG(LogTemp, Error, TEXT("Zone profile %s ID %d contains export for zone %d"), *Profile->GetPathName(), Profile->ZoneId, ExportId);
        return false;
    }
    SelectedZoneProfile = Profile;
    bHasTerrain = Profile->bHasTerrain;
    ZoneId = Profile->ZoneId;
    ZoneExportDirectory = Profile->ExportDirectory;
    EnhancementZoneDirectory = Profile->ExportDirectory;
    TerrainWidth = Profile->TerrainWidth;
    TerrainHeight = Profile->TerrainHeight;
    TerrainHeightSamples = Profile->TerrainHeightSamples;
    WaterSurfaces = Profile->WaterSurfaces;
    bWaterDataPrepared = Profile->bWaterDataPrepared;
    bGenerateWater = Profile->bGenerateWater;
    WaterMaterial = Profile->WaterMaterial;
    ImportedNifs = Profile->ImportedNifs;
    Placements = Profile->Placements;
    RequiredNifNames = Profile->RequiredNifNames;
    TerrainDiffuseTexture = Profile->TerrainDiffuseTexture;
    // Never let an untextured profile retain a previous zone's fallback material.
    TerrainMaterial = nullptr;
    RebuildZone();
    return true;
}

bool ADaocZoneActor::CaptureZoneProfile(UDaocZoneProfile *Profile, bool bCaptureAppearance)
{
    if (!Profile || ZoneId < 0 || (bHasTerrain && (TerrainWidth < 2 || TerrainHeight < 2 || int64(TerrainWidth) * TerrainHeight != TerrainHeightSamples.Num())) || !MissingFixtureMeshes.IsEmpty())
        return false;
    Profile->ZoneId = ZoneId;
    Profile->bHasTerrain = bHasTerrain;
    Profile->ExportDirectory = ZoneExportDirectory;
    Profile->TerrainWidth = TerrainWidth;
    Profile->TerrainHeight = TerrainHeight;
    Profile->TerrainHeightSamples = TerrainHeightSamples;
    if (!bWaterDataPrepared && bHasTerrain)
        ImportWaterData(ZoneExportDirectory);
    Profile->WaterSurfaces = WaterSurfaces;
    Profile->bWaterDataPrepared = bWaterDataPrepared;
    Profile->bGenerateWater = bGenerateWater;
    Profile->WaterMaterial = WaterMaterial;
    Profile->ImportedNifs = ImportedNifs;
    Profile->Placements = Placements;
    Profile->RequiredNifNames = RequiredNifNames;
    Profile->TerrainDiffuseTexture = TerrainDiffuseTexture;
    if (bCaptureAppearance)
    {
        Profile->OriginalTerrainMaterial = TerrainTextureMaterial;
        Profile->TerrainRoughness = TerrainRoughness;
        Profile->bCastMaskedSceneryShadows = bCastMaskedSceneryShadows;
        // Capture enhancement only when it belongs to this zone, never stale settings.
        const bool bOwn = (SelectedZoneProfile && SelectedZoneProfile->ZoneId == ZoneId) || (!EnhancementZoneDirectory.IsEmpty() && FPaths::IsSamePath(ZoneExportDirectory, EnhancementZoneDirectory));
        Profile->EnhancedTerrainMaterial = bOwn ? EnhancedTerrainMaterial.Get() : nullptr;
        Profile->SceneryReplacements = bOwn ? SceneryReplacements : TArray<FDaocSceneryReplacement>();
        Profile->GroundCoverMeshes = bOwn ? GroundCoverMeshes : TArray<TObjectPtr<UStaticMesh>>();
        Profile->GroundCoverSpacing = GroundCoverSpacing;
        Profile->GroundCoverFixturePadding = GroundCoverFixturePadding;
        Profile->GroundCoverMinimumGrassWeight = GroundCoverMinimumGrassWeight;
        Profile->GroundCoverFullDensityGrassWeight = GroundCoverFullDensityGrassWeight;
        Profile->GroundCoverScaleMultiplier = GroundCoverScaleMultiplier;
        Profile->GroundCoverTallScaleMultiplier = GroundCoverTallScaleMultiplier;
        Profile->GroundCoverTallPatchChance = GroundCoverTallPatchChance;
        Profile->GroundCoverCullDistance = GroundCoverCullDistance;
        Profile->GroundCoverTint = GroundCoverTint;
        Profile->GroundCoverMaterialOverride = GroundCoverMaterialOverride;
        Profile->bOverrideGroundCover = true;
        Profile->GroundCoverMaskWidth = bOwn ? GroundCoverMaskWidth : 0;
        Profile->GroundCoverMask = bOwn ? GroundCoverMask : TArray<uint8>();
    }
    Profile->MarkPackageDirty();
    return true;
}

bool ADaocZoneActor::SetGroundCoverMask(int32 Width, const TArray<uint8> &Samples, const FString &SourceZone)
{
    if (Width < 8 || Width > 2048 || Samples.Num() != Width * Width || SourceZone.IsEmpty())
        return false;
    GroundCoverMaskWidth = Width;
    GroundCoverMask = Samples;
    EnhancementZoneDirectory = SourceZone;
    return true;
}

void ADaocZoneActor::SetTerrainData(int32 Width, int32 Height, const TArray<float> &HeightSamples)
{
    TerrainWidth = Width;
    TerrainHeight = Height;
    TerrainHeightSamples = HeightSamples;
    RebuildZone();
}

void ADaocZoneActor::SetPreparedPlacements(int32 NewZoneId, const TArray<FDaocZonePlacement> &Prepared, bool bTerrain)
{
    if (ZoneId != NewZoneId)
    {
        WaterSurfaces.Reset();
        bWaterDataPrepared = false;
        WaterMaterial = nullptr;
    }
    ZoneId = NewZoneId;
    bHasTerrain = bTerrain;
    Placements = Prepared;
    RequiredNifNames.Reset();
    for (const auto &Placement : Placements)
        RequiredNifNames.AddUnique(Placement.NifName);
    RebuildZone();
}

void ADaocZoneActor::SetImportedNifs(const TArray<FDaocImportedNif> &Nifs)
{
    ImportedNifs = Nifs;
    RebuildInstances();
    RebuildGroundCover();
}

void ADaocZoneActor::SetPlacements(const TArray<FDaocZonePlacement> &NewPlacements)
{
    Placements = NewPlacements;
    RebuildInstances();
    RebuildGroundCover();
}

bool ADaocZoneActor::LoadZoneExport()
{
    return LoadZoneExportFromDirectory(ZoneExportDirectory);
}
namespace
{
bool LoadTerrainRed(const FString &Directory, const TCHAR *Name, DaocPcx::Image &Out)
{
    TArray<uint8> Bytes;
    const FString PcxPath = FPaths::Combine(Directory, FString(Name) + TEXT(".pcx"));
    if (FPaths::FileExists(PcxPath))
    {
        return FFileHelper::LoadFileToArray(Bytes, *PcxPath) && DaocPcx::Decode(Bytes.GetData(), Bytes.Num(), Out);
    }
    // PNG exports must retain RGB values, not luminance-converted indices.
    const FString PngPath = FPaths::Combine(Directory, FString(Name) + TEXT(".png"));
    if (!FFileHelper::LoadFileToArray(Bytes, *PngPath))
        return false;
    auto &Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    auto Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
    TArray64<uint8> Pixels;
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Wrapper->GetRaw(ERGBFormat::RGBA, 8, Pixels))
        return false;
    const int64 Count = int64(Wrapper->GetWidth()) * Wrapper->GetHeight();
    if (Count < 1 || Count > 64 * 1024 * 1024 || Pixels.Num() != Count * 4)
        return false;
    Out.Width = Wrapper->GetWidth();
    Out.Height = Wrapper->GetHeight();
    Out.Red.resize(static_cast<size_t>(Count));
    for (int64 I = 0; I < Count; ++I)
        Out.Red[I] = Pixels[I * 4];
    return true;
}

bool LoadTerrainFactors(const FString &Directory, int32 &Scale, int32 &Offset)
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Directory, TEXT("sector.dat"))))
        return false;
    bool bTerrain = false, bScale = false, bOffset = false;
    for (FString Line : Lines)
    {
        Line.TrimStartAndEndInline();
        if (Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#")))
            continue;
        if (Line.StartsWith(TEXT("[")))
        {
            bTerrain = Line.Equals(TEXT("[terrain]"), ESearchCase::IgnoreCase);
            continue;
        }
        FString Key, Value;
        if (!bTerrain || !Line.Split(TEXT("="), &Key, &Value))
            continue;
        Key.TrimStartAndEndInline();
        // Strip INI comments before checking the numeric factor.
        int32 Comment;
        if (Value.FindChar(';', Comment))
            Value.LeftInline(Comment);
        Value.TrimStartAndEndInline();
        int32 Factor;
        if (!LexTryParseString(Factor, *Value) || Factor < -32768 || Factor > 32767)
            continue;
        if (Key.Equals(TEXT("scalefactor"), ESearchCase::IgnoreCase))
        {
            Scale = Factor;
            bScale = true;
        }
        if (Key.Equals(TEXT("offsetfactor"), ESearchCase::IgnoreCase))
        {
            Offset = Factor;
            bOffset = true;
        }
    }
    return bScale && bOffset;
}
} // namespace

void ADaocZoneActor::SetMaskedSceneryShadows(bool bEnabled)
{
    bCastMaskedSceneryShadows = bEnabled;
    RebuildInstances();
}

bool ADaocZoneActor::LoadTerrainTextures()
{
    const bool bLoaded = ImportTerrainTextures(ZoneExportDirectory);
    ApplyTerrainMaterial();
    return bLoaded;
}

bool ADaocZoneActor::ImportTerrainTextures(const FString &Directory)
{
    FString Error;
    UTexture2D *ImportedTexture = DaocTerrainTextures::ImportAtlas(Directory, Error);
    if (!ImportedTexture)
    {
        UE_LOG(LogTemp, Warning, TEXT("DAoC terrain textures: %s"), *Error);
        return false;
    }
    TerrainDiffuseTexture = ImportedTexture;
    if (!TerrainTextureMaterial)
    {
        TerrainTextureMaterial = DaocTerrainTextures::DefaultMaterial(ImportedTexture);
    }
    UE_LOG(LogTemp, Display, TEXT("DAoC terrain texture imported from %s"), *Directory);
    return true;
}

void ADaocZoneActor::ApplyTerrainMaterial()
{
    TerrainMaterialInstance = nullptr;
    UMaterialInterface *Parent = IsEnhancementZone() && EnhancedTerrainMaterial ? EnhancedTerrainMaterial.Get() : TerrainTextureMaterial.Get();
    if (TerrainDiffuseTexture && Parent)
    {
        TerrainMaterialInstance = UMaterialInstanceDynamic::Create(Parent, this);
        TerrainMaterialInstance->SetTextureParameterValue(TEXT("ZoneDiffuse"), TerrainDiffuseTexture);
        TerrainMaterialInstance->SetScalarParameterValue(TEXT("ZoneRoughness"), TerrainRoughness);
        if (IsEnhancementZone() && SelectedZoneProfile && SelectedZoneProfile->EnhancedTerrainWeights)
            TerrainMaterialInstance->SetTextureParameterValue(TEXT("GroundWeights"), SelectedZoneProfile->EnhancedTerrainWeights);
        TerrainMesh->SetMaterial(0, TerrainMaterialInstance);
    }
    else
    {
        TerrainMesh->SetMaterial(0, TerrainMaterial);
    }
}
bool ADaocZoneActor::LoadZoneExportFromDirectory(const FString &ExportDirectory)
{
    DaocPcx::Image Terrain, Offset;
    int32 ScaleFactor = 0, OffsetFactor = 0;
    if (!LoadTerrainFactors(ExportDirectory, ScaleFactor, OffsetFactor) || !LoadTerrainRed(ExportDirectory, TEXT("terrain"), Terrain) || !LoadTerrainRed(ExportDirectory, TEXT("offset"), Offset) || Terrain.Width < 2 || Terrain.Height < 2 || Terrain.Width != Offset.Width || Terrain.Height != Offset.Height)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid terrain export in %s: require sector.dat [terrain] factors and matching terrain/offset PCX or PNG images"), *ExportDirectory);
        return false;
    }
    const int32 Width = Terrain.Width;
    const int32 Height = Terrain.Height;
    bHasTerrain = true;
    TArray<float> ImportedHeightSamples;
    ImportedHeightSamples.SetNum(Width * Height);
    for (int32 I = 0; I < ImportedHeightSamples.Num(); ++I)
    {
        ImportedHeightSamples[I] = DaocTerrain::Height(Terrain.Red[I], Offset.Red[I], ScaleFactor, OffsetFactor);
    }
    //---------------------------------------
    // Load NIF Definitions
    //---------------------------------------

    const FString NifsPath = FPaths::Combine(ExportDirectory, TEXT("nifs.csv"));

    TArray<FString> NifLines;

    if (!FFileHelper::LoadFileToStringArray(NifLines, *NifsPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load nifs.csv"));

        return false;
    }

    TMap<int32, FName> NifIdToName;
    TArray<FName> ImportedAssetNames;

    for (const FString &Line : NifLines)
    {
        TArray<FString> Fields;

        Line.ParseIntoArray(Fields, TEXT(","), false);

        if (Fields.Num() < 3)
        {
            continue;
        }

        if (Fields[0].TrimStartAndEnd().StartsWith(TEXT("NIF"), ESearchCase::IgnoreCase) || Fields[0].TrimStartAndEnd().StartsWith(TEXT("Grid Nifs"), ESearchCase::IgnoreCase))
            continue;
        int32 NifId;
        if (!LexTryParseString(NifId, *Fields[0].TrimStartAndEnd()))
            continue;

        if (NifId < 0)
        {
            continue;
        }

        const FString FileName = Fields[2].TrimStartAndEnd();

        if (FileName.IsEmpty())
        {
            continue;
        }

        const FString BaseName = FPaths::GetBaseFilename(FileName);

        const FName MeshName(*BaseName);

        NifIdToName.Add(NifId, MeshName);

        ImportedAssetNames.AddUnique(MeshName);
    }

    //---------------------------------------
    // Load Fixtures
    //---------------------------------------

    const FString FixturesPath = FPaths::Combine(ExportDirectory, TEXT("fixtures.csv"));

    TArray<FString> FixtureLines;

    if (!FFileHelper::LoadFileToStringArray(FixtureLines, *FixturesPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load fixtures.csv"));

        return false;
    }

    TArray<FDaocZonePlacement> ImportedPlacements;

    for (const FString &Line : FixtureLines)
    {
        TArray<FString> Fields;

        Line.ParseIntoArray(Fields, TEXT(","), false);

        if (Fields.Num() < 13)
        {
            continue;
        }

        int32 FixtureId;
        if (!LexTryParseString(FixtureId, *Fields[0].TrimStartAndEnd()))
            continue;

        if (FixtureId < 0)
        {
            continue;
        }

        const int32 NifId = FCString::Atoi(*Fields[1]);

        const FName *NifName = NifIdToName.Find(NifId);

        if (!NifName)
        {
            continue;
        }

        FDaocZonePlacement &Placement = ImportedPlacements.AddDefaulted_GetRef();

        Placement.NifName = *NifName;
        Placement.FixtureId = FCString::Atoi(*Fields[0]);
        Placement.NifId = NifId;

        Placement.GamePosition = FVector(FCString::Atof(*Fields[3]), FCString::Atof(*Fields[4]), FCString::Atof(*Fields[5]));

        const float AngleDegrees = FCString::Atof(*Fields[6]);
        Placement.RotationAngleRadians = FMath::DegreesToRadians(AngleDegrees > 180.0f ? 360.0f - AngleDegrees : AngleDegrees);
        Placement.RotationAxis = AngleDegrees > 180.0f ? FVector::UpVector : -FVector::UpVector;
        if (Fields.Num() > 18)
        {
            Placement.RotationAngleRadians = FCString::Atof(*Fields[15]);
            Placement.RotationAxis = FVector(FCString::Atof(*Fields[16]), FCString::Atof(*Fields[17]), FCString::Atof(*Fields[18]));
        }
        Placement.GeometryScale = FCString::Atof(*Fields[7]) / 100.0f;

        Placement.bOnGround = FCString::Atoi(*Fields[11]) > 0;

        Placement.bFlip = FCString::Atoi(*Fields[12]) != 0;

        Placement.PositionOffset = FVector::ZeroVector;

        Placement.PlacementKind = NAME_None;

        Placement.Layer = EDaocMeshLayer::Visible;
    }

    //---------------------------------------
    // Apply Imported Data
    //---------------------------------------

    TerrainWidth = Width;
    TerrainHeight = Height;

    TerrainHeightSamples = MoveTemp(ImportedHeightSamples);

    RequiredNifNames = MoveTemp(ImportedAssetNames);

    Placements = MoveTemp(ImportedPlacements);

    ZoneExportDirectory = ExportDirectory;
    WaterSurfaces.Reset();
    bWaterDataPrepared = false;
    WaterMaterial = nullptr;
    bGenerateWater = true;
    ImportWaterData(ExportDirectory);
    if (SelectedZoneProfile)
    {
        EnhancedTerrainMaterial = nullptr;
        TerrainTextureMaterial = nullptr;
        SceneryReplacements.Reset();
        GroundCoverMeshes.Reset();
        GroundCoverMask.Reset();
        GroundCoverMaskWidth = 0;
        EnhancementZoneDirectory.Reset();
    }
    SelectedZoneProfile = nullptr;
    ZoneId = ZoneIdFromExportDirectory(ExportDirectory);
    if (bAutoLoadTerrainTextures)
    {
        // Changing zones must not leave the previous zone's diffuse texture.
        TerrainDiffuseTexture = nullptr;
        ImportTerrainTextures(ExportDirectory);
    }

    UE_LOG(LogTemp, Display, TEXT("Zone loaded: %dx%d terrain, %d placements, %d assets"), TerrainWidth, TerrainHeight, Placements.Num(), RequiredNifNames.Num());

    RebuildZone();

    return true;
}

void ADaocZoneActor::RebuildTerrain()
{
    if (!bHasTerrain)
    {
        TerrainMesh->ClearAllMeshSections();
        return;
    }
    TerrainMesh->ClearAllMeshSections();
    if (TerrainWidth < 2 || TerrainHeight < 2 || static_cast<int64>(TerrainWidth) * TerrainHeight != TerrainHeightSamples.Num())
    {
        return;
    }

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    const int32 VertexCount = TerrainWidth * TerrainHeight;
    Vertices.Reserve(VertexCount);
    Normals.Init(FVector::ZeroVector, VertexCount);
    UVs.Reserve(VertexCount);
    Colors.Init(FLinearColor::White, VertexCount);

    for (int32 Y = 0; Y < TerrainHeight; ++Y)
    {
        for (int32 X = 0; X < TerrainWidth; ++X)
        {
            const int32 Index = Y * TerrainWidth + X;
            Vertices.Emplace(X * WorldUnitsPerDaocUnit, Y * WorldUnitsPerDaocUnit, TerrainHeightSamples[Index] * WorldUnitsPerDaocUnit);
            UVs.Emplace(static_cast<float>(X) / (TerrainWidth - 1), static_cast<float>(Y) / (TerrainHeight - 1));
        }
    }

    for (int32 Y = 0; Y < TerrainHeight - 1; ++Y)
    {
        for (int32 X = 0; X < TerrainWidth - 1; ++X)
        {
            const int32 TopLeft = Y * TerrainWidth + X;
            const int32 TopRight = TopLeft + 1;
            const int32 BottomLeft = TopLeft + TerrainWidth;
            const int32 BottomRight = BottomLeft + 1;

            int CellIndices[6];
            DaocTerrain::CellTriangles(TopLeft, TopRight, BottomLeft, BottomRight, CellIndices);
            for (int Index : CellIndices)
                Triangles.Add(Index);

            // UE's clockwise normal is Cross(C-A, B-A), so these remain upward.
            const FVector NormalA = FVector::CrossProduct(Vertices[TopLeft] - Vertices[BottomRight], Vertices[TopRight] - Vertices[BottomRight]).GetSafeNormal();
            const FVector NormalB = FVector::CrossProduct(Vertices[BottomLeft] - Vertices[BottomRight], Vertices[TopLeft] - Vertices[BottomRight]).GetSafeNormal();
            Normals[BottomRight] += NormalA + NormalB;
            Normals[TopLeft] += NormalA + NormalB;
            Normals[TopRight] += NormalA;
            Normals[BottomLeft] += NormalB;
        }
    }

    for (FVector &Normal : Normals)
    {
        Normal = Normal.GetSafeNormal(SMALL_NUMBER, FVector::UpVector);
    }

    TerrainMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
    ApplyTerrainMaterial();
}

void ADaocZoneActor::RebuildInstances()
{
    FixturePicks.Reset();
    RestoreFixtureIds();
    VisibleFixtureCount = 0;
    MissingFixtureMeshes.Reset();
    if (bAutoLoadObjectAssets)
    {
        for (FName Name : RequiredNifNames)
        {
            if (FindImportedNif(Name))
                continue;
            const FString Path = DaocObjectAssets::ObjectPath(Name.ToString());
            if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path)))
                continue;
            if (UStaticMesh *Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn))
            {
                FDaocImportedNif Imported;
                Imported.NifName = Name;
                Imported.VisibleMesh = Mesh;
                Imported.bDaocZUp = true;
                ImportedNifs.Add(Imported);
            }
        }
    }
    TArray<UInstancedStaticMeshComponent *> PreviousComponents;
    GetComponents(PreviousComponents);
    for (UInstancedStaticMeshComponent *Component : PreviousComponents)
    {
        if (IsValid(Component) && (GeneratedInstances.Contains(Component) || Component->ComponentTags.Contains(TEXT("DAoCGeneratedFixture"))))
        {
            Component->DestroyComponent();
        }
    }
    GeneratedInstances.Reset();

    TMap<FString, UInstancedStaticMeshComponent *> ComponentsByKey;
    for (const FDaocZonePlacement &Placement : Placements)
    {
        if ((Placement.Layer == EDaocMeshLayer::Collidee && !bShowCollideeLayer) || (Placement.Layer == EDaocMeshLayer::Pickee && !bShowPickeeLayer))
        {
            continue;
        }

        const FDaocImportedNif *Imported = FindImportedNif(Placement.NifName);
        UStaticMesh *StaticMesh = Imported ? MeshForLayer(*Imported, Placement.Layer) : nullptr;
        if (!StaticMesh)
        {
            if (Placement.Layer == EDaocMeshLayer::Visible)
                MissingFixtureMeshes.AddUnique(Placement.NifName);
            continue;
        }

        UStaticMesh *OriginalMesh = StaticMesh;
        bool bReplacement = false;
        bool bForceLod0 = false;
        float HeightMultiplier = 1.0f, WidthMultiplier = 1.0f;
        float GroundPlacementOffset = 0.0f;
        if (IsEnhancementZone() && Placement.Layer == EDaocMeshLayer::Visible)
        {
            const auto *Replacement = SceneryReplacements.FindByPredicate([&](const FDaocSceneryReplacement &R) { return FPaths::GetBaseFilename(R.NifName.ToString()).Equals(FPaths::GetBaseFilename(Placement.NifName.ToString()), ESearchCase::IgnoreCase); });
            if (Replacement && Replacement->Mesh)
            {
                StaticMesh = Replacement->Mesh;
                bReplacement = true;
                bForceLod0 = Replacement->bForceLod0;
                HeightMultiplier = FMath::Clamp(Replacement->HeightMultiplier, .25f, 2.0f);
                WidthMultiplier = FMath::Clamp(Replacement->WidthMultiplier, .25f, 2.0f);
                GroundPlacementOffset = FMath::IsFinite(Replacement->GroundPlacementOffset) ? Replacement->GroundPlacementOffset : 0.0f;
            }
        }

        const FString ComponentKey = StaticMesh->GetPathName() + TEXT("_") + FString::FromInt(static_cast<uint8>(Placement.Layer)) + (bForceLod0 ? TEXT("_LOD0") : TEXT("_AutoLOD"));
        UInstancedStaticMeshComponent *&Component = ComponentsByKey.FindOrAdd(ComponentKey);
        if (!Component)
        {
            auto *Hierarchy = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transactional | RF_Transient);
            Hierarchy->bAutoRebuildTreeOnInstanceChanges = false;
            Component = Hierarchy;
            Component->ComponentTags.Add(TEXT("DAoCGeneratedFixture"));
            Component->SetMobility(EComponentMobility::Static);
            Component->SetupAttachment(RootComponent);
            Component->SetStaticMesh(StaticMesh);
            if (bForceLod0)
                Component->SetForcedLodModel(1);
            const bool bHasMaskedMaterial = HasMaskedMaterial(StaticMesh);
            Component->SetCastShadow(Placement.Layer == EDaocMeshLayer::Visible && (!bHasMaskedMaterial || bCastMaskedSceneryShadows || bReplacement));
            if (bReplacement)
            {
                // The pack's full wind deforms our height-matched ISM trees.
                // Disable animation through per-component material instances,
                // preserving other WPO uses such as far-LOD impostor facing.
                // Cover every slot, including trunk, decoration and impostors.
                for (int32 Slot = 0; Slot < StaticMesh->GetStaticMaterials().Num(); ++Slot)
                {
                    UMaterialInterface *Parent = StaticMesh->GetMaterial(Slot);
                    if (!Parent)
                        continue;
                    auto *Material = UMaterialInstanceDynamic::Create(Parent, Component);
                    for (const TCHAR *Parameter : {TEXT("Wind Animation Strength"), TEXT("Wind Noise Strength"), TEXT("Additional Wind Animation Noise Strength"), TEXT("Local Wind Strength 1"), TEXT("Local Wind Strength 2"), TEXT("Local Wind Strength 3"), TEXT("Local Wind Strength 4")})
                    {
                        float OriginalValue;
                        if (Parent->GetScalarParameterValue(FMaterialParameterInfo(Parameter), OriginalValue))
                            Material->SetScalarParameterValue(Parameter, 0.0f);
                    }
                    Component->SetMaterial(Slot, Material);
                }
                // These imported replacements are static scenery. Pack interaction
                // WPO expects a different placement/scale system and can stretch trunks.
                if (!FParse::Param(FCommandLine::Get(), TEXT("DaocKeepTreeWPO")))
                {
                    Component->SetEvaluateWorldPositionOffset(false);
                    // WPO is disabled for these static replacements. Cache their
                    // virtual shadows on both Nanite and conventional paths.
                    Component->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
                }
            }
            Component->SetCollisionEnabled(Placement.Layer == EDaocMeshLayer::Pickee || bHasMaskedMaterial || bReplacement ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
            AddInstanceComponent(Component);
            Component->RegisterComponent();
            GeneratedInstances.Add(Component);
        }

        const float Height = Placement.bOnGround ? SampleTerrainHeight(Placement.GamePosition.X, Placement.GamePosition.Y) : Placement.GamePosition.Z * DaocUnitFactor;
        const FVector Location((Placement.GamePosition.X + Placement.PositionOffset.X) * DaocUnitFactor * WorldUnitsPerDaocUnit, (Placement.GamePosition.Y + Placement.PositionOffset.Y) * DaocUnitFactor * WorldUnitsPerDaocUnit, Height * WorldUnitsPerDaocUnit + Placement.PositionOffset.Z * DaocUnitFactor * WorldUnitsPerDaocUnit + GroundPlacementOffset);
        float AssetScale = Placement.GeometryScale * DaocUnitFactor * WorldUnitsPerDaocUnit;
        if (bReplacement)
            AssetScale *= DaocZoneRendering::GeometryBounds(*OriginalMesh).BoxExtent.Z / FMath::Max(1.0, DaocZoneRendering::GeometryBounds(*StaticMesh).BoxExtent.Z);
        const float FlipScale = Placement.bFlip ? -1.0f : 1.0f;
        const FVector RotationAxis = Placement.RotationAxis.IsNearlyZero() ? FVector::UpVector : Placement.RotationAxis.GetSafeNormal();
        const FVector AssetRotationAxis = Imported->bDaocZUp ? DaocObjectAssets::ImportedRotationAxis(RotationAxis) : RotationAxis;
        const FQuat NifRotation(AssetRotationAxis, Placement.RotationAngleRadians);
        const FQuat NifToUnrealBasis(FVector::ForwardVector, -HALF_PI);
        const FTransform InstanceTransform((Imported->bDaocZUp ? NifRotation : NifToUnrealBasis * NifRotation).Rotator(), Location, FVector(AssetScale * FlipScale * WidthMultiplier, AssetScale * WidthMultiplier, AssetScale * HeightMultiplier));
        const int32 InstanceIndex = Component->AddInstance(InstanceTransform, false);
        if (Placement.Layer == EDaocMeshLayer::Visible)
            FixturePicks.Add({Component, InstanceIndex, int32(&Placement - Placements.GetData())});
        if (Placement.Layer == EDaocMeshLayer::Visible)
            ++VisibleFixtureCount;
    }
    for (const auto &Pair : ComponentsByKey)
        if (auto *Hierarchy = Cast<UHierarchicalInstancedStaticMeshComponent>(Pair.Value))
            Hierarchy->BuildTreeIfOutdated(false, true);
    if (!MissingFixtureMeshes.IsEmpty())
        UE_LOG(LogTemp, Warning, TEXT("DAoC scenery: %d visible fixtures; %d model assets missing. See Missing Fixture Meshes on the actor."), VisibleFixtureCount, MissingFixtureMeshes.Num());
}

void ADaocZoneActor::RebuildGroundCover()
{
    TArray<UHierarchicalInstancedStaticMeshComponent *> Previous;
    GetComponents(Previous);
    for (auto *Component : Previous)
        if (Component->ComponentTags.Contains(TEXT("DAoCGroundCover")))
            Component->DestroyComponent();
    GroundCoverInstanceCount = 0;
    if (!IsEnhancementZone() || GroundCoverMeshes.IsEmpty() || TerrainWidth < 2 || TerrainHeight < 2 || TerrainHeightSamples.Num() != TerrainWidth * TerrainHeight || GroundCoverMask.Num() != GroundCoverMaskWidth * GroundCoverMaskWidth || GroundCoverMaskWidth < 8)
        return;

    // Exclude solid fixture footprints in actor-local space, including bridges and houses.
    const float FixturePadding = FMath::Max(0.f, GroundCoverFixturePadding);
    TArray<FBox2D> Exclusions;
    TArray<UInstancedStaticMeshComponent *> Fixtures;
    GetComponents(Fixtures);
    for (auto *Component : Fixtures)
    {
        if (!Component->ComponentTags.Contains(TEXT("DAoCGeneratedFixture")) || HasMaskedMaterial(Component->GetStaticMesh()))
            continue;
        for (int32 I = 0; I < Component->GetInstanceCount(); ++I)
        {
            FTransform Transform;
            Component->GetInstanceTransform(I, Transform, false);
            const FBox Box = Component->GetStaticMesh()->GetBoundingBox().TransformBy(Transform);
            Exclusions.Add(FBox2D(FVector2D(Box.Min.X - FixturePadding, Box.Min.Y - FixturePadding), FVector2D(Box.Max.X + FixturePadding, Box.Max.Y + FixturePadding)));
        }
    }
    TArray<UHierarchicalInstancedStaticMeshComponent *> Cover;
    for (UStaticMesh *Mesh : GroundCoverMeshes)
    {
        if (!Mesh)
            continue;
        auto *Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transactional | RF_Transient);
        Component->bAutoRebuildTreeOnInstanceChanges = false;
        Component->ComponentTags.Add(TEXT("DAoCGroundCover"));
        Component->SetMobility(EComponentMobility::Static);
        Component->SetupAttachment(RootComponent);
        Component->SetStaticMesh(Mesh);
        for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
        {
            UMaterialInterface *Parent = GroundCoverMaterialOverride ? GroundCoverMaterialOverride.Get() : Mesh->GetMaterial(Slot);
            if (!Parent)
                continue;
            auto *Material = UMaterialInstanceDynamic::Create(Parent, Component);
            Material->SetVectorParameterValue(TEXT("DaocGrassTint"), GroundCoverTint);
            Component->SetMaterial(Slot, Material);
        }
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCastShadow(false);
        Component->SetVisibleInRayTracing(false);
        Component->SetAffectDistanceFieldLighting(false);
        // Ground cover is close-range detail. Avoid the pack's flat billboard
        // LODs; its fade-aware material removes it before the distant terrain.
        Component->SetForcedLodModel(1);
        Component->SetCullDistances(FMath::Max(100.f, GroundCoverCullDistance * .6f), FMath::Max(100.f, GroundCoverCullDistance));
        AddInstanceComponent(Component);
        Component->RegisterComponent();
        Cover.Add(Component);
    }
    if (Cover.IsEmpty())
        return;
    const float SizeX = (TerrainWidth - 1) * WorldUnitsPerDaocUnit;
    const float SizeY = (TerrainHeight - 1) * WorldUnitsPerDaocUnit;
    const float Spacing = FMath::Max(40.f, GroundCoverSpacing);
    // Hard bound prevents accidental density/scale changes from exhausting memory.
    const int32 NX = FMath::Min(700, FMath::CeilToInt(SizeX / Spacing));
    const int32 NY = FMath::Min(700, FMath::CeilToInt(SizeY / Spacing));
    const float MinimumWeight = FMath::Clamp(GroundCoverMinimumGrassWeight, 0.f, 1.f);
    const float FullWeight = FMath::Clamp(GroundCoverFullDensityGrassWeight, MinimumWeight, 1.f);
    int32 MaskRejected = 0, FixtureRejected = 0, SlopeRejected = 0;
    FRandomStream Random(173);
    for (int32 Y = 0; Y < NY; ++Y)
        for (int32 X = 0; X < NX; ++X)
        {
            const float PX = (X + Random.FRandRange(.12f, .88f)) * SizeX / NX;
            const float PY = (Y + Random.FRandRange(.12f, .88f)) * SizeY / NY;
            const int32 MX = FMath::Clamp(int32(PX / SizeX * GroundCoverMaskWidth), 0, GroundCoverMaskWidth - 1);
            const int32 MY = FMath::Clamp(int32(PY / SizeY * GroundCoverMaskWidth), 0, GroundCoverMaskWidth - 1);
            const float Weight = GroundCoverMask[MY * GroundCoverMaskWidth + MX] / 255.f;
            // Original zones blend grass into soil very differently. A fixed 72% cutoff
            // removed almost all of Mularn's cover even with Albion's spacing/meshes.
            const float Probability = FullWeight > MinimumWeight ? FMath::Clamp((Weight - MinimumWeight) / (FullWeight - MinimumWeight), 0.f, 1.f) : 1.f;
            if (Weight <= 0.f || Weight < MinimumWeight || Random.FRand() >= Probability)
            {
                ++MaskRejected;
                continue;
            }
            const FVector2D Point(PX, PY);
            if (Exclusions.ContainsByPredicate([&](const FBox2D &Box) { return Box.IsInside(Point); }))
            {
                ++FixtureRejected;
                continue;
            }
            const float GX = PX / (WorldUnitsPerDaocUnit * DaocUnitFactor);
            const float GY = PY / (WorldUnitsPerDaocUnit * DaocUnitFactor);
            const float Z = SampleTerrainHeight(GX, GY) * WorldUnitsPerDaocUnit;
            if (IsBelowWater(PX, PY, Z))
                continue;
            float DX, DY;
            if (!DaocTerrain::Gradient(TerrainHeightSamples.GetData(), TerrainWidth, TerrainHeight, GX * DaocUnitFactor, GY * DaocUnitFactor, DX, DY))
                continue;
            if (FVector2D(DX, DY).Size() > .65f)
            {
                ++SlopeRejected;
                continue;
            }
            // A separate spatial seed keeps the density, mesh choices and positions unchanged.
            // Neighboring clumps share taller patches rather than isolated oversized blades.
            const int32 PatchX = FMath::FloorToInt(PX / 600.f), PatchY = FMath::FloorToInt(PY / 600.f);
            FRandomStream PatchRandom(HashCombine(GetTypeHash(PatchX), GetTypeHash(PatchY)));
            const bool Tall = PatchRandom.FRand() < FMath::Clamp(GroundCoverTallPatchChance, 0.f, 1.f);
            const float Multiplier = Tall ? GroundCoverTallScaleMultiplier : GroundCoverScaleMultiplier;
            const float Scale = Random.FRandRange(.65f, 1.05f) * FMath::Max(.01f, Multiplier);
            auto *Component = Cover[Random.RandRange(0, Cover.Num() - 1)];
            const FVector Normal = FVector(-DX, -DY, 1).GetSafeNormal();
            const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal) * FQuat(FVector::UpVector, FMath::DegreesToRadians(Random.FRandRange(0, 360)));
            const FBox Bounds = Component->GetStaticMesh()->GetBoundingBox();
            // Align the base plane, rather than assuming every mesh's pivot is its root.
            FTransform Transform(Rotation, FVector(PX, PY, Z) - Normal * (Bounds.Min.Z * Scale + 2), FVector(Scale));
            float Clearance = 0;
            // A clump can cross triangle edges. Seat its entire base footprint to avoid
            // suspended uphill corners where adjacent triangles have different slopes.
            for (int IX = 0; IX < 3; ++IX)
                for (int IY = 0; IY < 3; ++IY)
                {
                    const FVector Base = Transform.TransformPosition(FVector(FMath::Lerp(Bounds.Min.X, Bounds.Max.X, IX * .5f), FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, IY * .5f), Bounds.Min.Z));
                    if (Base.X < 0 || Base.Y < 0 || Base.X > SizeX || Base.Y > SizeY)
                        continue;
                    const float Surface = SampleTerrainHeight(Base.X / (WorldUnitsPerDaocUnit * DaocUnitFactor), Base.Y / (WorldUnitsPerDaocUnit * DaocUnitFactor)) * WorldUnitsPerDaocUnit;
                    Clearance = FMath::Max(Clearance, float(Base.Z) - Surface);
                }
            Transform.AddToTranslation(FVector(0, 0, -Clearance));
            Component->AddInstance(Transform, false);
            ++GroundCoverInstanceCount;
        }
    for (auto *Component : Cover)
        Component->BuildTreeIfOutdated(false, true);
    UE_LOG(LogTemp, Log, TEXT("DAoC ground cover zone %d: %d instances, %d candidates, mask rejected %d, fixtures %d, slopes %d; spacing %.1f, mask minimum %.2f/full %.2f"), ZoneId, GroundCoverInstanceCount, NX * NY, MaskRejected, FixtureRejected, SlopeRejected, Spacing, MinimumWeight, FullWeight);
}

void ADaocZoneActor::BeginPlay()
{
    Super::BeginPlay();
    // Generated geometry is transient; reconstruct it from the saved data and mesh references.
    RebuildZone();
    // Levels saved before networking did not populate ZoneId.
    if (ZoneId == INDEX_NONE)
        ZoneId = ZoneIdFromExportDirectory(ZoneExportDirectory);
}

float ADaocZoneActor::SampleTerrainHeight(float GameX, float GameY) const
{
    if (TerrainWidth < 2 || TerrainHeight < 2 || static_cast<int64>(TerrainWidth) * TerrainHeight != TerrainHeightSamples.Num())
        return 0;
    return DaocTerrain::Sample(TerrainHeightSamples.GetData(), TerrainWidth, TerrainHeight, GameX * DaocUnitFactor, GameY * DaocUnitFactor);
}
bool ADaocZoneActor::FindTerrainSurface(FVector WorldPosition, FVector &Surface) const
{
    if (TerrainWidth < 2 || TerrainHeight < 2 || WorldUnitsPerDaocUnit <= 0 || TerrainHeightSamples.Num() != int64(TerrainWidth) * TerrainHeight)
        return false;
    FVector Local = GetActorTransform().InverseTransformPosition(WorldPosition);
    if (Local.X < 0 || Local.Y < 0 || Local.X > (TerrainWidth - 1) * WorldUnitsPerDaocUnit || Local.Y > (TerrainHeight - 1) * WorldUnitsPerDaocUnit)
        return false;
    // Match the rendered/collision triangles, split along top-left/bottom-right.
    // Bilinear heights can sit below a triangle on a non-planar terrain cell.
    const float GX = Local.X / WorldUnitsPerDaocUnit, GY = Local.Y / WorldUnitsPerDaocUnit;
    const int32 X = FMath::Min(FMath::FloorToInt(GX), TerrainWidth - 2);
    const int32 Y = FMath::Min(FMath::FloorToInt(GY), TerrainHeight - 2);
    const float FX = GX - X, FY = GY - Y;
    const float H00 = TerrainHeightSamples[Y * TerrainWidth + X];
    const float H10 = TerrainHeightSamples[Y * TerrainWidth + X + 1];
    const float H01 = TerrainHeightSamples[(Y + 1) * TerrainWidth + X];
    const float H11 = TerrainHeightSamples[(Y + 1) * TerrainWidth + X + 1];
    Local.Z = (FY <= FX ? H00 + (H10 - H00) * FX + (H11 - H10) * FY : H00 + (H11 - H01) * FX + (H01 - H00) * FY) * WorldUnitsPerDaocUnit;
    Surface = GetActorTransform().TransformPosition(Local);
    return true;
}
const FDaocImportedNif *ADaocZoneActor::FindImportedNif(FName NifName) const
{
    return ImportedNifs.FindByPredicate([NifName](const FDaocImportedNif &Imported) { return Imported.NifName.IsEqual(NifName, ENameCase::IgnoreCase); });
}

void ADaocZoneActor::SetEnhancementEnabled(bool bEnabled)
{
    bEnableEnhancement = bEnabled;
    RebuildZone();
}
void ADaocZoneActor::RestoreFixtureIds()
{
    if (!Placements.ContainsByPredicate([](const FDaocZonePlacement &P) { return P.FixtureId == INDEX_NONE; }))
        return;
    TArray<FString> Lines;
    const FString Folder = SelectedZoneProfile ? SelectedZoneProfile->ExportDirectory : ZoneExportDirectory;
    if (!FFileHelper::LoadFileToStringArray(Lines, *(Folder / TEXT("fixtures.csv"))))
        return;
    // Old profiles have no IDs. Match their original coordinates and NIF identity,
    // never manufacture an ID from an instance index (HISM reorders rendering).
    TMap<FString, TArray<int32>> ByPosition;
    auto Key = [](const FVector &V) { return FString::Printf(TEXT("%.3f:%.3f:%.3f"), V.X, V.Y, V.Z); };
    for (int32 I = 0; I < Placements.Num(); ++I)
        if (Placements[I].FixtureId == INDEX_NONE)
            ByPosition.FindOrAdd(Key(Placements[I].GamePosition)).Add(I);
    TMap<int32, FName> Names;
    TArray<FString> Nifs;
    FFileHelper::LoadFileToStringArray(Nifs, *(Folder / TEXT("nifs.csv")));
    for (const auto &Line : Nifs)
    {
        TArray<FString> F;
        Line.ParseIntoArray(F, TEXT(","), false);
        if (F.Num() > 2 && F[0].IsNumeric())
            Names.Add(FCString::Atoi(*F[0]), FName(*FPaths::GetBaseFilename(F[2].TrimStartAndEnd().TrimQuotes())));
    }
    for (const auto &Line : Lines)
    {
        TArray<FString> F;
        Line.ParseIntoArray(F, TEXT(","), false);
        if (F.Num() < 6 || !F[0].IsNumeric())
            continue;
        const int32 NifId = FCString::Atoi(*F[1]);
        const FName *Name = Names.Find(NifId);
        if (!Name)
            continue;
        if (const auto *Indices = ByPosition.Find(Key(FVector(FCString::Atod(*F[3]), FCString::Atod(*F[4]), FCString::Atod(*F[5])))))
            for (int32 I : *Indices)
                if (FPaths::GetBaseFilename(Placements[I].NifName.ToString()).Equals(Name->ToString(), ESearchCase::IgnoreCase))
                {
                    Placements[I].FixtureId = FCString::Atoi(*F[0]);
                    Placements[I].NifId = NifId;
                }
    }
}
bool ADaocZoneActor::InspectFixture(const FVector &Start, const FVector &End, FVector &Hit, FString &Information, FName &Nif) const
{
    double Nearest = 1.;
    const FFixturePick *Best = nullptr;
    FTransform BestTransform;
    for (const auto &Pick : FixturePicks)
    {
        auto *Component = Pick.Component.Get();
        if (!Component || !Component->GetStaticMesh() || !Placements.IsValidIndex(Pick.Placement))
            continue;
        FTransform Transform;
        if (!Component->GetInstanceTransform(Pick.Instance, Transform, true))
            continue;
        const FVector A = Transform.InverseTransformPosition(Start), D = Transform.InverseTransformPosition(End) - A;
        const FBox Bounds = Component->GetStaticMesh()->GetBoundingBox();
        double Near = 0, Far = Nearest;
        bool Intersects = true;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (FMath::Abs(D[Axis]) < 1.e-9)
            {
                if (A[Axis] < Bounds.Min[Axis] || A[Axis] > Bounds.Max[Axis])
                {
                    Intersects = false;
                    break;
                }
            }
            else
            {
                double T0 = (Bounds.Min[Axis] - A[Axis]) / D[Axis], T1 = (Bounds.Max[Axis] - A[Axis]) / D[Axis];
                if (T0 > T1)
                    Swap(T0, T1);
                Near = FMath::Max(Near, T0);
                Far = FMath::Min(Far, T1);
                if (Near > Far)
                {
                    Intersects = false;
                    break;
                }
            }
        }
        if (Intersects && Near < Nearest)
        {
            Nearest = Near;
            Best = &Pick;
            BestTransform = Transform;
        }
    }
    if (!Best)
        return false;
    const auto &P = Placements[Best->Placement];
    auto *C = Best->Component.Get();
    UStaticMesh *Mesh = C->GetStaticMesh();
    const auto *Original = FindImportedNif(P.NifName);
    const bool Replaced = Original && Original->VisibleMesh != Mesh;
    Hit = FMath::Lerp(Start, End, Nearest);
    Nif = P.NifName;
    FString Materials;
    for (int32 I = 0; I < Mesh->GetStaticMaterials().Num(); ++I)
        if (auto *M = C->GetMaterial(I))
            Materials += M->GetName() + TEXT(" ");
    Information = FString::Printf(TEXT("Zone %d | fixture %s | NIF id %s | placement %d\nNIF: %s\n%s: %s\nDAoC position: %s\nWorld scale: %s | LODs: %d | Nanite: %s\nMaterials: %s"), ZoneId, P.FixtureId == INDEX_NONE ? TEXT("unknown (refresh profile)") : *FString::FromInt(P.FixtureId), P.NifId == INDEX_NONE ? TEXT("unknown") : *FString::FromInt(P.NifId), Best->Placement, *P.NifName.ToString(), Replaced ? TEXT("Replacement") : TEXT("Classic mesh"), *Mesh->GetPathName(), *P.GamePosition.ToString(), *BestTransform.GetScale3D().ToString(), Mesh->GetNumLODs(), UsesNanite(Mesh) ? TEXT("yes") : TEXT("no"), *Materials);
    return true;
}
