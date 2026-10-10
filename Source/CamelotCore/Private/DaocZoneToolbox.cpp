#include "DaocZoneToolbox.h"
#include "DaocZoneRendering.h"
#include "DaocWeatherAssetBuilder.h"
#include "DaocWeatherAssets.h"
#include "NiagaraEmitter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialFunctionInterface.h"
#if WITH_EDITOR
#include "DaocZoneToolboxSettings.h"
#include "DaocZoneActor.h"
#include "DaocZoneProfile.h"
#include "DaocClientAssetCatalog.h"
#include "DaocObjectsCommandlet.h"
#include "DaocTerrainTexturesCommandlet.h"
#include "DaocCharactersCommandlet.h"
#include "DaocAnimationsCommandlet.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ToolMenus.h"
#include "Interfaces/IPluginManager.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
namespace
{
const FName TabId(TEXT("DaocZoneToolbox"));
class SDaocZoneToolbox : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SDaocZoneToolbox)
    {
    }
    SLATE_END_ARGS()
    void Construct(const FArguments &)
    {
        Settings.Reset(NewObject<UDaocZoneToolboxSettings>());
        Settings->LoadConfig();
        FDetailsViewArgs Args;
        Args.bAllowSearch = true;
        Args.bHideSelectionTip = true;
        auto &Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        SettingsView = Properties.CreateDetailView(Args);
        ProfileView = Properties.CreateDetailView(Args);
        SettingsView->SetObject(Settings.Get());
        SettingsView->OnFinishedChangingProperties().AddSP(this, &SDaocZoneToolbox::OnReplacementSelectionChanged);
        ChildSlot[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("DAoC outdoor zones: choose an ID, then create a profile from its classic catalog entry. Add replacements by exact source NIF. Imports and saves run only when you press a button. Stop PIE before editing assets.")))] + SScrollBox::Slot()[SNew(SBox).MinDesiredHeight(340)[SettingsView.ToSharedRef()]] + SScrollBox::Slot()[Button(TEXT("Create / open zone profile"), [this] { CreateProfile(); })] + SScrollBox::Slot()[Button(TEXT("Load profile and regenerate preview"), [this] { LoadPreview(); })] + SScrollBox::Slot()[Button(TEXT("Add / update NIF replacement (copy dependencies)"), [this] { AddReplacement(); })] + SScrollBox::Slot()[Button(TEXT("Save profile and regenerate"), [this] { SaveProfile(); })] + SScrollBox::Slot()[Button(TEXT("Use Camelot Hills Meadow LOD0 as default for all zones"), [this] { UseMeadowDefaults(); })] + SScrollBox::Slot()[Button(TEXT("Import outdoor water and save profile"), [this] { PrepareWater(); })] + SScrollBox::Slot()[Button(TEXT("Prepare weather assets (rain / snow)"), [this] { PrepareWeather(); })] + SScrollBox::Slot()[Button(TEXT("Prepare exported zone objects"), [this] { PrepareObjects(); })] + SScrollBox::Slot()[Button(TEXT("Import prepared objects and refresh zone data"), [this] { ImportObjects(); })] + SScrollBox::Slot()[Button(TEXT("Import character manifest"), [this] { ImportCharacters(); })] + SScrollBox::Slot()[Button(TEXT("Import animation manifest"), [this] { ImportAnimations(); })] + SScrollBox::Slot()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Status); })] + SScrollBox::Slot()[SNew(SBox).MinDesiredHeight(500)[ProfileView.ToSharedRef()]]];
    }
    virtual ~SDaocZoneToolbox() override
    {
        if (Ticker.IsValid())
            FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
        if (Process.IsValid())
            FPlatformProcess::CloseProc(Process); // Closing the tab does not kill preparation.
    }

  private:
    TStrongObjectPtr<UDaocZoneToolboxSettings> Settings;
    TSharedPtr<IDetailsView> SettingsView, ProfileView;
    FProcHandle Process;
    FTSTicker::FDelegateHandle Ticker;
    FString Status = TEXT("Ready. New profiles inherit shared enhancement settings. Classic mode retains original foliage.");
    bool Allowed()
    {
        if (GEditor && !GEditor->PlayWorld && !Process.IsValid())
            return true;
        Status = TEXT("Stop PIE or wait for preparation to finish.");
        return false;
    }
    TSharedRef<SWidget> Button(const TCHAR *Label, TFunction<void()> Action)
    {
        return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([this, Action] {
            if (Allowed())
                Action();
            return FReply::Handled();
        });
    }
    UDaocClientAssetCatalog *Catalog()
    {
        return LoadObject<UDaocClientAssetCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_ClientAssets.DA_ClientAssets"));
    }
    ADaocZoneActor *Actor()
    {
        UWorld *World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World)
            return nullptr;
        for (TActorIterator<ADaocZoneActor> It(World); It; ++It)
            return *It;
        auto *Zone = World->SpawnActor<ADaocZoneActor>();
        if (Zone)
            Zone->SetActorLabel(TEXT("DAoC Zone Preview"));
        return Zone;
    }
    FString ProfilePath() const
    {
        const FString Name = Settings->ZoneId == 0 ? TEXT("DA_Zone000_CamelotHills") : Settings->ZoneId == 1 ? TEXT("DA_Zone001_SalisburyPlains")
                                                                                                             : FString::Printf(TEXT("DA_Zone%03d"), Settings->ZoneId);
        return Settings->ProfileRoot / Name;
    }
    FString ExportFolder() const
    {
        return Settings->ExportRoot / FString::Printf(TEXT("zone%03d"), Settings->ZoneId);
    }
    bool Save(UObject *Object)
    {
        if (!Object)
            return false;
        UPackage *Package = Object->GetOutermost();
        Object->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        return UPackage::SavePackage(Package, Object, *File, Args);
    }
    bool PrepareNanite(UStaticMesh *Mesh)
    {
        if (!Mesh || !DaocZoneRendering::CanUseNanite(*Mesh))
        {
            Status = TEXT("Nanite requires surface materials with opaque or masked sections.");
            return false;
        }
        Mesh->Modify();
        for (const FStaticMaterial &Slot : Mesh->GetStaticMaterials())
        {
            UMaterialInterface *Material = Slot.MaterialInterface;
            if (!Material->GetUsageByFlag(MATUSAGE_Nanite))
            {
                // UE 5.8 also tracks usage on material instances. Saving the
                // mesh alone leaves packaged builds with default materials.
                if (!Material->CheckMaterialUsage(MATUSAGE_Nanite) || !Save(Material))
                {
                    Status = TEXT("Could not prepare and save the Nanite material usage.");
                    return false;
                }
            }
        }
        DaocZoneRendering::ConfigureNanite(*Mesh, true);
        Mesh->Build();
        UDaocTerrainTexturesCommandlet::FinishAssetCompilation();
        if (!Save(Mesh))
        {
            Status = TEXT("Could not save the prepared Nanite mesh.");
            return false;
        }
        return true;
    }
    bool Current() const
    {
        return Settings->Profile && Settings->Profile->ZoneId == Settings->ZoneId && Settings->Profile->bHasTerrain;
    }
    void RegisterProfile()
    {
        auto *C = Catalog();
        auto *Zone = Actor();
        auto *P = Settings->Profile.Get();
        if (!C || !Zone || !P)
            return;
        C->ZoneEnhancementProfiles.Add(P->ZoneId, TSoftObjectPtr<UDaocZoneProfile>(P));
        Save(C);
        Zone->Modify();
        Zone->ClientAssetCatalog = C;
        Zone->ZoneProfiles.RemoveAll([P](const TObjectPtr<UDaocZoneProfile> &V) { return V && V->ZoneId == P->ZoneId; });
        Zone->ZoneProfiles.Add(P);
        Zone->SelectedZoneProfile = P;
        Zone->MarkPackageDirty();
        Settings->SaveConfig();
        ProfileView->SetObject(P);
        LoadReplacementFields();
    }
    void CreateProfile()
    {
        auto *C = Catalog();
        if (!C)
        {
            Status = TEXT("Import the client catalog first.");
            return;
        }
        const auto *Reference = C->ClassicZones.Find(Settings->ZoneId);
        auto *Classic = Reference ? Reference->LoadSynchronous() : nullptr;
        if (!Classic || !Classic->bHasTerrain)
        {
            Status = TEXT("No imported outdoor zone with this ID. Dungeons are excluded.");
            return;
        }
        const FString PackageName = ProfilePath();
        if (!FPackageName::IsValidLongPackageName(PackageName) || !PackageName.StartsWith(TEXT("/Game/DAoC/ZoneProfiles/")))
        {
            Status = TEXT("Profile Root must be inside /Game/DAoC/ZoneProfiles.");
            return;
        }
        UDaocZoneProfile *P = nullptr;
        if (const auto *Existing = C->ZoneEnhancementProfiles.Find(Settings->ZoneId))
            P = Existing->LoadSynchronous();
        if (!P)
            if (auto *Zone = Actor())
                for (UDaocZoneProfile *Local : Zone->ZoneProfiles)
                    if (Local && Local->ZoneId == Settings->ZoneId)
                    {
                        P = Local;
                        break;
                    }
        if (!P)
            P = LoadObject<UDaocZoneProfile>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName)), nullptr, LOAD_NoWarn);
        if (P && P->ZoneId != Settings->ZoneId)
        {
            Status = TEXT("Existing profile belongs to another zone; choose another path.");
            return;
        }
        if (!P)
        {
            P = DuplicateObject<UDaocZoneProfile>(Classic, CreatePackage(*PackageName), *FPackageName::GetLongPackageAssetName(PackageName));
            P->SetFlags(RF_Public | RF_Standalone);
            FAssetRegistryModule::AssetCreated(P);
            P->bUseDefaultEnhancement = true;
            P->bOverrideGroundCover = false;
            P->GroundCoverMeshes.Reset();
        }
        Settings->Profile = P;
        if (!Settings->DisplayName.IsEmpty())
            P->DisplayName = Settings->DisplayName;
        // A prepared grass mask is optional; never scatter grass indiscriminately on roads.
        if (P->GroundCoverMask.IsEmpty())
        {
            const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CamelotCore"));
            TArray<uint8> Mask;
            if (Plugin && FFileHelper::LoadFileToArray(Mask, *(Settings->SourcePluginDirectory / FString::Printf(TEXT("tests/build/shared-foliage/masks/%d/grass.bin"), P->ZoneId))))
            {
                int32 Width = FMath::RoundToInt(FMath::Sqrt(float(Mask.Num())));
                if (Width >= 8 && Width * Width == Mask.Num())
                {
                    P->GroundCoverMaskWidth = Width;
                    P->GroundCoverMask = MoveTemp(Mask);
                }
            }
        }
        RegisterProfile();
        Status = Save(P) ? TEXT("Profile ready. Edit below, add NIF replacements, then save and regenerate.") : TEXT("Profile created but save failed; see Output Log.");
    }
    void LoadPreview()
    {
        if (!Current())
        {
            Status = TEXT("Create/open a profile for the selected ID first.");
            return;
        }
        auto *Zone = Actor();
        if (!Zone)
            return;
        RegisterProfile();
        Zone->SetEnhancementEnabled(true);
        Status = Zone->LoadZoneProfile(Settings->Profile) ? TEXT("Preview regenerated. Save the level to retain this preview actor.") : TEXT("Zone load failed. Check missing imports in Output Log.");
    }
    void SaveProfile()
    {
        if (!Current())
        {
            Status = TEXT("Profile ID does not match the selected outdoor zone.");
            return;
        }
        RegisterProfile();
        if (!Save(Settings->Profile))
        {
            Status = TEXT("Profile save failed.");
            return;
        }
        LoadPreview();
    }
    UObject *OwnAsset(UObject *Source, const FString &Root)
    {
        if (!Source)
            return nullptr;
        if (Source->GetOutermost()->GetName().StartsWith(TEXT("/Game/DAoC/")))
            return Source;
        auto &Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
        TSet<FName> Seen;
        TArray<FName> Pending{Source->GetOutermost()->GetFName()};
        TMap<UObject *, UObject *> Copies;
        auto &Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
        for (int32 I = 0; I < Pending.Num(); ++I)
        {
            FName Package = Pending[I];
            const FString Path = Package.ToString();
            if (Seen.Contains(Package) || !Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Game/DAoC/")))
                continue;
            Seen.Add(Package);
            TArray<FName> Dependencies;
            Registry.GetDependencies(Package, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
            Pending.Append(Dependencies);
            TArray<FAssetData> Data;
            Registry.GetAssetsByPackageName(Package, Data);
            for (const auto &D : Data)
            {
                auto *Original = D.GetAsset();
                if (!Original)
                    continue;
                const FString Destination = Root / Path.Mid(6);
                UObject *Copy = LoadObject<UObject>(nullptr, *(Destination + TEXT(".") + Original->GetName()), nullptr, LOAD_NoWarn);
                if (!Copy)
                    Copy = Assets.DuplicateAsset(Original->GetName(), FPackageName::GetLongPackagePath(Destination), Original);
                if (!Copy)
                {
                    Status = TEXT("Dependency copy failed: ") + Path;
                    return nullptr;
                }
                Copies.Add(Original, Copy);
            }
        }
        TMap<FSoftObjectPath, FSoftObjectPath> SoftPaths;
        TArray<UPackage *> Packages;
        for (const auto &Pair : Copies)
        {
            SoftPaths.Add(FSoftObjectPath(Pair.Key), FSoftObjectPath(Pair.Value));
            Packages.AddUnique(Pair.Value->GetOutermost());
        }
        for (const auto &Pair : Copies)
        {
            FArchiveReplaceObjectRef<UObject> Replace(Pair.Value, Copies, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
            Pair.Value->PostEditChange();
        }
        Assets.RenameReferencingSoftObjectPaths(Packages, SoftPaths);
        for (const auto &Pair : Copies)
            if (!Save(Pair.Value))
            {
                Status = TEXT("Dependency save failed.");
                return nullptr;
            }
        return Copies.FindRef(Source);
    }
    void PrepareWeather()
    {
        auto *C = Catalog();
        if (!C)
        {
            Status = TEXT("Import the client catalog first.");
            return;
        }
        const FString Root = TEXT("/Game/DAoC/Weather/Library");
        auto *Source = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Game/Tropical/FX/NiagaraParticles/PS_RainEmitter.PS_RainEmitter"), nullptr, LOAD_NoWarn);
        auto *Wetness = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Tropical/Blueprints/RainSystem/MPC/MPC_GlobalWetness.MPC_GlobalWetness"), nullptr, LOAD_NoWarn);
        auto *Snow = LoadObject<UMaterialFunctionInterface>(nullptr, TEXT("/Game/Tropical/Materials/SnowBuildup/MF_SnowBuildup.MF_SnowBuildup"), nullptr, LOAD_NoWarn);
        if (!Source || !Wetness || !Snow)
        {
            Status = TEXT("Run tools/migrate_weather_assets.py to copy the Tropical weather assets into this project.");
            return;
        }
        // All game dependencies are duplicated and remapped before making the systems.
        Source = Cast<UNiagaraEmitter>(OwnAsset(Source, Root));
        if (!Source)
            return;
        Wetness = Cast<UMaterialParameterCollection>(OwnAsset(Wetness, Root));
        if (!Wetness)
            return;
        Snow = Cast<UMaterialFunctionInterface>(OwnAsset(Snow, Root));
        if (!Snow)
            return;
        FString Error;
        auto *Library = DaocPrepareWeatherAssets(Source, Wetness, Snow, Error);
        if (!Library)
        {
            Status = Error;
            return;
        }
        C->WeatherAssets = Library;
        if (!Save(C))
        {
            Status = TEXT("Could not save the weather catalog reference.");
            return;
        }
        Status = TEXT("Weather assets are ready under /Game/DAoC/Weather. Zone 100 defaults to snowfall, zone 0 stays sunny; other zones follow DOL storms. Disable Use Zone Defaults in a zone profile to choose persistent weather or Server And Cycle.");
    }
    void PrepareWater()
    {
        if (!Current())
        {
            Status = TEXT("Select an outdoor zone profile first.");
            return;
        }
        auto *Zone = Actor();
        auto *P = Settings->Profile.Get();
        if (!Zone || !Zone->LoadZoneProfile(P) || !Zone->RefreshWaterData())
        {
            Status = TEXT("Water import failed. Check sector.dat in the profile export directory and the Output Log.");
            return;
        }
        P->Modify();
        if (!Zone->CaptureZoneProfile(P, false) || !Save(P))
        {
            Status = TEXT("Could not save prepared water data.");
            return;
        }
        ProfileView->SetObject(P);
        Status = FString::Printf(TEXT("Saved %d outdoor water definitions and regenerated the preview."), P->WaterSurfaces.Num());
    }
    void LoadReplacementFields()
    {
        const FString Name = FPaths::GetBaseFilename(Settings->SourceNif.ToString());
        const FDaocSceneryReplacement *Selected = nullptr;
        auto Find = [&](const UDaocZoneProfile *P) -> const FDaocSceneryReplacement * { return P ? P->SceneryReplacements.FindByPredicate([&](const FDaocSceneryReplacement &R) { return FPaths::GetBaseFilename(R.NifName.ToString()).Equals(Name, ESearchCase::IgnoreCase); }) : nullptr; };
        Selected = Find(Settings->Profile.Get());
        if (!Selected && Settings->Profile && Settings->Profile->bUseDefaultEnhancement)
            if (auto *C = Catalog())
                Selected = Find(C->DefaultEnhancementSettings.Get());
        Settings->ReplacementMesh = Selected ? Selected->Mesh.Get() : nullptr;
        Settings->HeightMultiplier = Selected ? Selected->HeightMultiplier : 1.f;
        Settings->WidthMultiplier = Selected ? Selected->WidthMultiplier : 1.f;
        Settings->GroundPlacementOffset = Selected ? Selected->GroundPlacementOffset : 0.f;
        SettingsView->ForceRefresh();
    }
    void OnReplacementSelectionChanged(const FPropertyChangedEvent &Event)
    {
        const FName Property = Event.GetPropertyName();
        if (Property == GET_MEMBER_NAME_CHECKED(UDaocZoneToolboxSettings, SourceNif) || Property == GET_MEMBER_NAME_CHECKED(UDaocZoneToolboxSettings, Profile) || Property == GET_MEMBER_NAME_CHECKED(UDaocZoneToolboxSettings, ZoneId))
            LoadReplacementFields();
    }
    void AddReplacement()
    {
        if (!Current() || Settings->SourceNif.IsNone())
        {
            Status = TEXT("Select a matching profile and source NIF (F10 in game copies it).");
            return;
        }
        const FString Name = FPaths::GetBaseFilename(Settings->SourceNif.ToString());
        const bool Present = Settings->Profile->RequiredNifNames.ContainsByPredicate([&](FName N) { return FPaths::GetBaseFilename(N.ToString()).Equals(Name, ESearchCase::IgnoreCase); });
        if (!Present)
        {
            Status = TEXT("This NIF is not used in this zone. Check the hammer's zone and source name.");
            return;
        }
        UStaticMesh *Mesh = nullptr;
        if (Settings->ReplacementMesh)
        {
            Mesh = Cast<UStaticMesh>(OwnAsset(Settings->ReplacementMesh, TEXT("/Game/DAoC/Foliage/Library")));
            if (!Mesh)
                return;
            // Opt in on the owned replacement only. Masked cards can be slower with Nanite.
            if (Settings->bEnableNaniteForSelectedReplacement)
            {
                if (!PrepareNanite(Mesh))
                    return;
            }
        }
        auto *P = Settings->Profile.Get();
        P->Modify();
        auto *R = P->SceneryReplacements.FindByPredicate([&](const FDaocSceneryReplacement &V) { return FPaths::GetBaseFilename(V.NifName.ToString()).Equals(Name, ESearchCase::IgnoreCase); });
        if (!R)
            R = &P->SceneryReplacements.AddDefaulted_GetRef();
        R->NifName = Settings->SourceNif;
        R->Mesh = Mesh;
        R->HeightMultiplier = Settings->HeightMultiplier;
        R->WidthMultiplier = Settings->WidthMultiplier;
        R->GroundPlacementOffset = Settings->GroundPlacementOffset;
        R->bForceLod0 = false;
        // Null mesh deliberately keeps this source NIF classic even when defaults replace it.
        SaveProfile();
    }
    void UseMeadowDefaults()
    {
        auto *C = Catalog();
        if (!C || !C->DefaultEnhancementSettings)
        {
            Status = TEXT("Shared enhancement settings are missing.");
            return;
        }
        auto *Hills = LoadObject<UDaocZoneProfile>(nullptr, TEXT("/Game/DAoC/ZoneProfiles/DA_Zone000_CamelotHills.DA_Zone000_CamelotHills"), nullptr, LOAD_NoWarn);
        if (!Hills)
        {
            Status = TEXT("Camelot Hills profile is missing.");
            return;
        }
        TStrongObjectPtr<UDaocZoneProfile> CapturedProfile(NewObject<UDaocZoneProfile>());
        auto *Template = CapturedProfile.Get();
        bool Captured = false;
        if (auto *Zone = Actor(); Zone && Zone->GetZoneId() == 0)
            Captured = Zone->CaptureZoneProfile(Template, true);
        if (!Captured)
            Template = Hills;
        TArray<TObjectPtr<UStaticMesh>> Grass;
        for (const TCHAR *Name : {TEXT("SM_Daoc_Grass_01_Var1_LOD0"), TEXT("SM_Daoc_Grass_02_Var1_LOD0")})
        {
            const FString Path = FString(TEXT("/Game/DAoC/Foliage/Library/DAoC/Enhanced/Grass/")) + Name;
            auto *Original = LoadObject<UStaticMesh>(nullptr, *(Path + TEXT(".") + Name), nullptr, LOAD_NoWarn);
            if (!Original)
            {
                Status = TEXT("Owned Meadow mesh missing: ") + Path;
                return;
            }
            const FString Destination = TEXT("/Game/DAoC/Foliage/MeadowFullQuality/") + FString(Name);
            auto *Mesh = LoadObject<UStaticMesh>(nullptr, *(Destination + TEXT(".") + Name), nullptr, LOAD_NoWarn);
            if (!Mesh)
                Mesh = Cast<UStaticMesh>(FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().DuplicateAsset(Name, FPackageName::GetLongPackagePath(Destination), Original));
            if (!Mesh)
            {
                Status = TEXT("Could not create Meadow LOD0 mesh.");
                return;
            }
            Mesh->Modify();
            Mesh->SetNumSourceModels(1);
            if (!PrepareNanite(Mesh))
            {
                return;
            }
            Grass.Add(Mesh);
        }
        Grass.Add(Grass[0]); // Preserve Camelot Hills' two meadow variants and their 2:1 weighting.
        auto *D = C->DefaultEnhancementSettings.Get();
        D->Modify();
        D->GroundCoverMeshes = Grass;
        D->GroundCoverSpacing = Template->GroundCoverSpacing;
        D->GroundCoverScaleMultiplier = Template->GroundCoverScaleMultiplier;
        D->GroundCoverTallScaleMultiplier = Template->GroundCoverTallScaleMultiplier;
        D->GroundCoverTallPatchChance = Template->GroundCoverTallPatchChance;
        D->GroundCoverCullDistance = Template->GroundCoverCullDistance;
        D->GroundCoverTint = Template->GroundCoverTint;
        D->GroundCoverMaterialOverride = Template->GroundCoverMaterialOverride;
        if (!Save(D))
        {
            Status = TEXT("Shared grass settings could not be saved.");
            return;
        }
        TSet<UDaocZoneProfile *> Profiles;
        for (const auto &Pair : C->ClassicZones)
            if (auto *P = Pair.Value.LoadSynchronous())
                Profiles.Add(P);
        for (const auto &Pair : C->ZoneEnhancementProfiles)
            if (auto *P = Pair.Value.LoadSynchronous())
                Profiles.Add(P);
        if (auto *Zone = Actor())
            for (UDaocZoneProfile *P : Zone->ZoneProfiles)
                if (P)
                    Profiles.Add(P);
        if (Settings->Profile)
            Profiles.Add(Settings->Profile);
        FARFilter Filter;
        Filter.PackagePaths.Add(TEXT("/Game/DAoC/ZoneProfiles"));
        Filter.bRecursivePaths = true;
        Filter.ClassPaths.Add(UDaocZoneProfile::StaticClass()->GetClassPathName());
        TArray<FAssetData> Authored;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssets(Filter, Authored);
        for (const auto &Data : Authored)
            if (auto *P = Cast<UDaocZoneProfile>(Data.GetAsset()))
            {
                Profiles.Add(P);
                if (P->ZoneId >= 0 && P->bHasTerrain)
                    C->ZoneEnhancementProfiles.Add(P->ZoneId, TSoftObjectPtr<UDaocZoneProfile>(P));
            }
        if (!Save(C))
        {
            Status = TEXT("Could not save the zone appearance registry.");
            return;
        }
        for (auto *P : Profiles)
            if (P->bHasTerrain)
            {
                P->Modify();
                P->bUseDefaultEnhancement = true;
                P->bOverrideGroundCover = false;
                P->GroundCoverMeshes.Reset();
                if (!Save(P))
                {
                    Status = TEXT("Some zone profiles could not be saved. See Output Log.");
                    return;
                }
            }
        if (auto *Zone = Actor())
            Zone->RebuildZone();
        Status = TEXT("All registered outdoor profiles inherit Camelot Hills grass size/spacing/culling and full Meadow LOD0. Per-zone masks, tint and terrain are retained. Enable Override Ground Cover for later exceptions.");
    }
    static FString Quote(const FString &Value)
    {
        return TEXT("\"") + Value + TEXT("\"");
    }
    void PrepareObjects()
    {
        auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CamelotCore"));
        if (!Plugin)
        {
            Status = TEXT("Plugin path unavailable.");
            return;
        }
        const FString Script = Settings->SourcePluginDirectory / TEXT("tools/prepare_zone_objects.py");
        const FString Converter = Settings->SourcePluginDirectory / TEXT("tests/build/nif-export/Release/daoc_nif_export.exe");
        for (const auto &Value : {Settings->PythonExecutable, ExportFolder(), Settings->ExtractRoot, Converter, Settings->PreparedObjectsDirectory, Settings->ClientDirectory, Script})
            if (Value.Contains(TEXT("\"")) || Value.EndsWith(TEXT("\\")))
            {
                Status = TEXT("Paths must not contain quotes or end in a backslash.");
                return;
            }
        if (!FPaths::FileExists(Converter) || !FPaths::DirectoryExists(ExportFolder()))
        {
            Status = TEXT("Converter or exported zone folder is missing. Use the source plugin toolbox (not an installed tools-only copy).");
            return;
        }
        Settings->SaveConfig();
        const FString Args = Quote(Script) + TEXT(" --zone ") + Quote(ExportFolder()) + TEXT(" --root ") + Quote(Settings->ExtractRoot) + TEXT(" --converter ") + Quote(Converter) + TEXT(" --output ") + Quote(Settings->PreparedObjectsDirectory) + TEXT(" --textures-root ") + Quote(Settings->ClientDirectory);
        Process = FPlatformProcess::CreateProc(*Settings->PythonExecutable, *Args, false, true, true, nullptr, 0, *Settings->SourcePluginDirectory, nullptr);
        if (!Process.IsValid())
        {
            Status = TEXT("Could not start Python preparation.");
            return;
        }
        Status = TEXT("Preparing objects in background. When finished, press Import prepared objects. No tests are launched.");
        Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float) {
                                                          if (FPlatformProcess::IsProcRunning(Process))
                                                              return true;
                                                          int32 Code = -1;
                                                          FPlatformProcess::GetProcReturnCode(Process, &Code);
                                                          FPlatformProcess::CloseProc(Process);
                                                          Process.Reset();
                                                          Status = Code == 0 ? TEXT("Preparation finished. Press Import prepared objects and refresh zone data.") : TEXT("Preparation failed or some models were missing. Inspect manifest.json failures; fix paths before import.");
                                                          Ticker.Reset();
                                                          return false;
                                                      }),
                                                      .5f);
    }
    void ImportObjects()
    {
        if (!FPaths::FileExists(Settings->PreparedObjectsDirectory / TEXT("manifest.json")))
        {
            Status = TEXT("No object manifest in Prepared Objects Directory.");
            return;
        }
        if (!UDaocObjectsCommandlet::ImportManifest(Settings->PreparedObjectsDirectory, Settings->bReimport))
        {
            Status = TEXT("Object import failed. See Output Log.");
            return;
        }
        auto *Zone = Actor();
        if (!Zone)
            return;
        Zone->ClientAssetCatalog = Catalog();
        if (!Zone->LoadZoneExportFromDirectory(ExportFolder()) || Zone->GetZoneId() != Settings->ZoneId)
        {
            Status = TEXT("Exported zone failed to load or ID does not match.");
            return;
        }
        auto *C = Catalog();
        if (!C)
        {
            Status = TEXT("Client catalog is missing.");
            return;
        }
        if (!C->ClassicZones.Contains(Settings->ZoneId))
        {
            const FString ClassicPath = FString::Printf(TEXT("/Game/DAoC/Catalogs/Zones/DA_Zone%03d_Classic"), Settings->ZoneId);
            auto *Classic = NewObject<UDaocZoneProfile>(CreatePackage(*ClassicPath), *FPackageName::GetLongPackageAssetName(ClassicPath), RF_Public | RF_Standalone);
            if (!Zone->CaptureZoneProfile(Classic, false))
            {
                Status = TEXT("Classic zone capture failed; import missing meshes first.");
                return;
            }
            Classic->bUseDefaultEnhancement = true;
            FAssetRegistryModule::AssetCreated(Classic);
            if (!Save(Classic))
            {
                Status = TEXT("Could not save classic zone profile.");
                return;
            }
            C->SetClassicZone(Settings->ZoneId, Classic->GetPathName());
            Save(C);
        }
        if (!Current())
            CreateProfile();
        if (!Current())
            return;
        if (!Zone->CaptureZoneProfile(Settings->Profile, false))
        {
            Status = TEXT("Zone capture failed (missing meshes or terrain). See Output Log.");
            return;
        }
        SaveProfile();
    }
    void ImportCharacters()
    {
        Settings->SaveConfig();
        Status = UDaocCharactersCommandlet::ImportManifest(Settings->CharacterManifest, Settings->bReimport) ? TEXT("Character import completed.") : TEXT("Character import failed. Check manifest path and Output Log.");
    }
    void ImportAnimations()
    {
        Settings->SaveConfig();
        Status = UDaocAnimationsCommandlet::ImportManifest(Settings->AnimationManifest, Settings->bReimport) ? TEXT("Animation import completed.") : TEXT("Animation import failed. Check manifest path and Output Log.");
    }
};
} // namespace
void RegisterDaocZoneToolbox()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs &) { return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SDaocZoneToolbox)]; })).SetDisplayName(FText::FromString(TEXT("DAoC Zone Toolbox"))).SetMenuType(ETabSpawnerMenuType::Hidden);
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([] {
        auto *Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
        Menu->FindOrAddSection(TEXT("DAoC")).AddMenuEntry(TEXT("DAoCZoneToolbox"), FText::FromString(TEXT("DAoC Zone Toolbox")), FText::FromString(TEXT("Create zone profiles, replace NIFs, import assets and regenerate previews.")), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(TabId); })));
    }));
}
void UnregisterDaocZoneToolbox()
{
    if (UToolMenus::IsToolMenuUIEnabled())
        UToolMenus::Get()->RemoveEntry(TEXT("LevelEditor.MainMenu.Window"), TEXT("DAoC"), TEXT("DAoCZoneToolbox"));
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}
#endif
