#include "DaocTerrainTexturesCommandlet.h"
#include "DaocTerrainTextures.h"
#include "DaocZoneProfile.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#endif

UDaocTerrainTexturesCommandlet::UDaocTerrainTexturesCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}
bool UDaocTerrainTexturesCommandlet::SetProfileGroundCoverMask(UDaocZoneProfile *Profile, int32 Width, const TArray<uint8> &Samples)
{
#if WITH_EDITOR
    if (!Profile || Width < 8 || Width > 2048 || int64(Width) * Width != Samples.Num())
        return false;
    Profile->Modify();
    Profile->GroundCoverMaskWidth = Width;
    Profile->GroundCoverMask = Samples;
    Profile->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

int32 UDaocTerrainTexturesCommandlet::RemapFoliageReferences(UObject *Target, const FString &LibraryRoot)
{
#if WITH_EDITOR
    if (!Target || LibraryRoot != TEXT("/Game/DAoC/Foliage/Library"))
        return -1;
    const FString Package = Target->GetOutermost()->GetName();
    if (!Package.StartsWith(TEXT("/Game/DAoC/")) && Package != TEXT("/Game/CamelotHills"))
        return -1;
    auto &Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FName> Dependencies;
    Registry.GetDependencies(FName(*Package), Dependencies);
    TMap<UObject *, UObject *> Mapping;
    for (FName Dependency : Dependencies)
    {
        const FString Source = Dependency.ToString();
        if (!Source.StartsWith(TEXT("/Game/")) || Source.StartsWith(LibraryRoot))
            continue;
        const FString Destination = LibraryRoot + TEXT("/") + Source.Mid(6);
        if (!FPackageName::DoesPackageExist(Destination))
            continue;
        UObject *From = LoadObject<UObject>(nullptr, *(Source + TEXT(".") + FPaths::GetBaseFilename(Source)));
        UObject *To = LoadObject<UObject>(nullptr, *(Destination + TEXT(".") + FPaths::GetBaseFilename(Destination)));
        if (!From || !To || From->GetClass() != To->GetClass())
            return -1;
        Mapping.Add(From, To);
    }
    Target->Modify();
    FArchiveReplaceObjectRef<UObject> Replace(Target, Mapping, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
    int32 Count = int32(Replace.GetCount());
    if (auto *Material = Cast<UMaterialInterface>(Target))
    {
        // Streaming metadata contains soft paths even for obsolete textures that
        // no longer participate in the material. Object remapping cannot resolve
        // those missing paths, so repair this cached table explicitly.
        auto Data = Material->GetTextureStreamingData();
        Count += Data.RemoveAll([](const FMaterialTextureInfo &Info) { return !Info.TextureReference.IsNull() && !FPackageName::DoesPackageExist(Info.TextureReference.GetLongPackageName()); });
        for (auto &Info : Data)
        {
            const FString Source = Info.TextureReference.GetLongPackageName();
            if (!Source.StartsWith(TEXT("/Game/")) || Source.StartsWith(LibraryRoot))
                continue;
            const FString Destination = LibraryRoot + TEXT("/") + Source.Mid(6);
            if (FPackageName::DoesPackageExist(Destination))
            {
                Info.TextureReference = FSoftObjectPath(Destination + TEXT(".") + FPaths::GetBaseFilename(Destination));
                ++Count;
            }
        }
        if (Count)
            Material->SetTextureStreamingData(Data);
    }
    if (Count)
    {
        Target->PostEditChange();
        Target->MarkPackageDirty();
    }
    return Count;
#else
    return -1;
#endif
}

bool UDaocTerrainTexturesCommandlet::CopyFoliagePackages(const TArray<FString> &Packages, const FString &DestinationRoot)
{
#if WITH_EDITOR
    if (!DestinationRoot.StartsWith(TEXT("/Game/DAoC/Foliage/")) || DestinationRoot.Contains(TEXT("..")))
        return false;
    auto &Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FString> Pending = Packages;
    TMap<FString, FString> Mapping;
    for (int32 I = 0; I < Pending.Num(); ++I)
    {
        const FString Source = Pending[I];
        if (!Source.StartsWith(TEXT("/Game/")) || Source.StartsWith(DestinationRoot) || Mapping.Contains(Source))
            continue;
        if (!FPackageName::DoesPackageExist(Source))
        {
            if (Packages.Contains(Source))
            {
                UE_LOG(LogTemp, Error, TEXT("Foliage copy missing root package: %s"), *Source);
                return false;
            }
            // Some packs retain stale references to their author's template
            // folder. Loading/resaving drops unresolved optional defaults; the
            // caller audits the resulting graph and material-instance overrides.
            UE_LOG(LogTemp, Warning, TEXT("Foliage source has an unresolved dependency: %s"), *Source);
            continue;
        }
        Mapping.Add(Source, DestinationRoot + TEXT("/") + Source.Mid(6));
        TArray<FName> Dependencies;
        Registry.GetDependencies(FName(*Source), Dependencies);
        for (FName Dependency : Dependencies)
            Pending.Add(Dependency.ToString());
    }
    return !Mapping.IsEmpty() && FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().AdvancedCopyPackages(Mapping, true, true);
#else
    return false;
#endif
}
bool UDaocTerrainTexturesCommandlet::SetProfileTerrainTexture(UDaocZoneProfile *Profile, UTexture2D *Texture)
{
#if WITH_EDITOR
    if (!Profile || !Profile->bHasTerrain || !Texture)
        return false;
    Profile->Modify();
    Profile->TerrainDiffuseTexture = Texture;
    Profile->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

bool UDaocTerrainTexturesCommandlet::ClearRuntimeLevelScript(UWorld *World)
{
#if WITH_EDITOR
    if (!World || World->GetName() != TEXT("DAoCGameplay") || !World->PersistentLevel)
        return false;
    auto *Blueprint = World->PersistentLevel->GetLevelScriptBlueprint(true);
    if (!Blueprint)
        return false;
    TArray<UEdGraph *> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (auto *Graph : Graphs)
        FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    World->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

void UDaocTerrainTexturesCommandlet::FinishAssetCompilation()
{
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
#endif
}

int32 UDaocTerrainTexturesCommandlet::Main(const FString &Params)
{
#if WITH_EDITOR
#if WITH_DEV_AUTOMATION_TESTS
    if (FParse::Param(*Params, TEXT("SelfTest")))
    {
        FString TestError;
        if (!DaocTerrainTextures::RunAtlasTests(TestError))
        {
            UE_LOG(LogTemp, Error, TEXT("Atlas regression failed: %s"), *TestError);
            return 1;
        }
        UE_LOG(LogTemp, Display, TEXT("Atlas orientation/channel/alpha/failure regressions passed."));
    }
#endif
    FString Directory;
    if (!FParse::Value(*Params, TEXT("ZoneDirectory="), Directory))
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=DaocTerrainTextures -ZoneDirectory=\"path to extracted zone\""));
        return 1;
    }
    FString Error;
    UTexture2D *Texture = DaocTerrainTextures::ImportAtlas(Directory, Error);
    if (!Texture)
    {
        UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
        return 1;
    }
    UMaterialInterface *Material = DaocTerrainTextures::DefaultMaterial(Texture);
    if (!Material)
        return 1;
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
    for (UObject *Asset : {static_cast<UObject *>(Texture), static_cast<UObject *>(Material)})
    {
        UPackage *Package = Asset->GetOutermost();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Package, Asset, *Filename, Args))
            return 1;
        UE_LOG(LogTemp, Display, TEXT("DAoC texture asset saved: %s"), *Filename);
    }
    UE_LOG(LogTemp, Display, TEXT("DAoC terrain atlas: %dx%d"), Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
    return 0;
#else
    UE_LOG(LogTemp, Error, TEXT("Terrain texture asset import requires an editor build."));
    return 1;
#endif
}
