#include "DaocLiveCrowdCommandlet.h"
#include "CamelotLiveCrowd.h"
#include "CamelotCrowdComponent.h"
#include "DaocEquipmentCatalog.h"
#include "DaocCombatAnimationCatalog.h"
#include "DaocClientAssetCatalog.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

UDaocLiveCrowdCommandlet::UDaocLiveCrowdCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}
int32 UDaocLiveCrowdCommandlet::Main(const FString &Params)
{
#if !WITH_EDITOR
    return 1;
#else
    FString ContentRoot;
    const bool Mounted = FParse::Value(*Params, TEXT("ContentRoot="), ContentRoot);
    if (Mounted)
    {
        ContentRoot = FPaths::ConvertRelativePathToFull(ContentRoot);
        FPaths::NormalizeDirectoryName(ContentRoot);
        ContentRoot += TEXT("/");
        FPackageName::RegisterMountPoint(TEXT("/Game/"), ContentRoot);
    }
    ON_SCOPE_EXIT
    {
        if (Mounted)
            FPackageName::UnRegisterMountPoint(TEXT("/Game/"), ContentRoot);
    };
    auto *Equipment = LoadObject<UDaocEquipmentCatalog>(nullptr, TEXT("/Game/DAoC/Equipment/DA_EquipmentCatalog.DA_EquipmentCatalog"));
    auto *Combat = LoadObject<UDaocCombatAnimationCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_CombatAnimations.DA_CombatAnimations"));
    auto *Assets = LoadObject<UDaocClientAssetCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_ClientAssets.DA_ClientAssets"), nullptr, LOAD_NoWarn);
    if (!Equipment || !Combat)
        return 1;
    FString ModelText = TEXT("players,34,35"), Output = TEXT("/Game/DAoC/Crowd");
    FParse::Value(*Params, TEXT("Models="), ModelText, false);
    FParse::Value(*Params, TEXT("Output="), Output);
    if (!Output.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Output))
        return 1;
    TArray<FString> Tokens;
    ModelText.ParseIntoArray(Tokens, TEXT(","), true);
    TSet<int32> Unique;
    for (const auto &Token : Tokens)
        if (Token.Equals(TEXT("players"), ESearchCase::IgnoreCase))
            for (const auto &Figure : Equipment->Figures)
                Unique.Add(Figure.Model);
        else if (Token.IsNumeric() && FCString::Atoi(*Token) > 0)
            Unique.Add(FCString::Atoi(*Token));
    auto Models = Unique.Array();
    Models.Sort();
    UE_LOG(LogTemp, Display, TEXT("Live crowd preparation: %d models, %d equipment figures, %d canonical combat families"), Models.Num(), Equipment->Figures.Num(), Combat->Models.Num());
    if (FParse::Param(*Params, TEXT("ListOnly")))
    {
        for (int32 Model : Models)
            UE_LOG(LogTemp, Display, TEXT("Live crowd model %d"), Model);
        return Models.IsEmpty() ? 1 : 0;
    }
    const FString CatalogPath = Output / TEXT("DA_LiveCrowdCatalog");
    auto *Catalog = LoadObject<UCamelotLiveCrowdCatalog>(nullptr, *CatalogPath, nullptr, LOAD_NoWarn);
    bool NewCatalog = !Catalog;
    if (FParse::Param(*Params, TEXT("UpgradeBlending")))
    {
        if (!Catalog)
            return 1;
        TStrongObjectPtr<UCamelotLiveCrowdCatalog> KeepUpgradeCatalog(Catalog);
        TSet<FSoftObjectPath> Paths;
        TArray<UCamelotCrowdAnimationSet *> Sets;
        for (const auto &Model : Catalog->Models)
            for (const auto &Entry : Model.Value.MeshProviders)
                if (!Paths.Contains(Entry.Value.ToSoftObjectPath()))
                {
                    auto *Set = Entry.Value.LoadSynchronous();
                    if (!Set || !Set->GetOutermost()->GetName().StartsWith(Output / TEXT("Providers/")) || Set->Providers.Num() != 1 || !Set->Providers[0] || (Set->Providers[0]->GetNumLayers() != 1 && Set->Providers[0]->GetNumLayers() != 4))
                        return 1;
                    for (const auto &Layer : Set->Providers[0]->GetLayers())
                        if (Layer.BlendMode != EAnimSequenceTransformProviderLayerBlendMode::Override || Layer.Weight != 1 || !Layer.BoneMaskProfileName.IsNone() || Layer.bMeshSpaceRotation || Set->Providers[0]->GetLayerStack())
                            return 1;
                    Paths.Add(Entry.Value.ToSoftObjectPath());
                    Sets.Add(Set);
                }
        const FString Backup = FPaths::ProjectSavedDir() / TEXT("AnimationBlendAssetsBackup") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
        int32 Updated = 0;
        for (auto *Set : Sets)
        {
            auto *Provider = Set->Providers[0].Get();
            if (Provider->GetNumLayers() == 4)
                continue;
            auto *Package = Set->GetOutermost();
            const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
            for (const TCHAR *Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk")})
            {
                const FString Source = FPaths::ChangeExtension(Filename, Extension);
                if (!IFileManager::Get().FileExists(*Source))
                    continue;
                const FString Destination = Backup / (Package->GetName().RightChop(1) + Extension);
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
                if (IFileManager::Get().Copy(*Destination, *Source) != COPY_OK)
                    return 1;
            }
            if (!UCamelotCrowdComponent::PrepareBlendLayers(Provider))
                return 1;
            FSavePackageArgs Args;
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            Args.SaveFlags = SAVE_NoError;
            if (!UPackage::SavePackage(Package, Set, *Filename, Args))
                return 1;
            ++Updated;
        }
        UE_LOG(LogTemp, Display, TEXT("Crowd blend upgrade: %d updated, %d ready providers; backup %s"), Updated, Sets.Num(), *Backup);
        return 0;
    }
    if (!Catalog)
    {
        auto *Package = CreatePackage(*CatalogPath);
        Catalog = NewObject<UCamelotLiveCrowdCatalog>(Package, TEXT("DA_LiveCrowdCatalog"), RF_Public | RF_Standalone);
    }
    TStrongObjectPtr<UCamelotLiveCrowdCatalog> KeepCatalog(Catalog);
    TStrongObjectPtr<UDaocEquipmentCatalog> KeepEquipment(Equipment);
    TStrongObjectPtr<UDaocCombatAnimationCatalog> KeepCombat(Combat);
    TStrongObjectPtr<UDaocClientAssetCatalog> KeepAssets(Assets);
    auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    auto *Actor = World->SpawnActor<AActor>();
    auto *Part = NewObject<USkeletalMeshComponent>(Actor);
    Actor->SetRootComponent(Part);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->RegisterComponent();
    int32 Completed = 0, ProvidersSaved = 0, Skipped = 0;
    for (int32 Model : Models)
    {
        auto *Body = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Model, Model), nullptr, LOAD_NoWarn);
        if (!Body)
        {
            UE_LOG(LogTemp, Display, TEXT("Live crowd model %d retained on full presenter: no body asset"), Model);
            ++Skipped;
            continue;
        }
        const int32 Family = Combat->ModelAliases.Contains(Model) ? Combat->ModelAliases.FindChecked(Model) : Model;
        FCamelotLiveCrowdModel Prepared;
        TArray<UAnimSequence *> Clips;
        TSoftObjectPtr<UAnimSequence> *Loops[] = {&Prepared.Idle, &Prepared.Walk, &Prepared.Run};
        const TCHAR *Actions[] = {TEXT("Idle"), TEXT("Walk"), TEXT("Run")};
        for (int32 L = 0; L < 3; ++L)
        {
            auto *Clip = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_%s.A_daoc_%d_%s"), Family, Actions[L], Family, Actions[L]), nullptr, LOAD_NoWarn);
            if (!Clip)
                Clip = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_%s.A_daoc_%d_%s"), Model, Actions[L], Model, Actions[L]), nullptr, LOAD_NoWarn);
            if (Clip)
            {
                *Loops[L] = Clip;
                Clips.AddUnique(Clip);
            }
        }
        if (Prepared.Idle.IsNull())
        {
            UE_LOG(LogTemp, Display, TEXT("Live crowd model %d retained on full presenter: no idle asset"), Model);
            ++Skipped;
            continue;
        }
        if (const auto *ClipsForModel = Combat->Models.Find(Family))
        {
            TArray<FString> Keys;
            ClipsForModel->Actions.GetKeys(Keys);
            Keys.Sort();
            for (const auto &Key : Keys)
                if (auto *Clip = ClipsForModel->Actions.FindChecked(Key).LoadSynchronous())
                    Clips.AddUnique(Clip);
        }
        TSet<FSoftObjectPath> Meshes{FSoftObjectPath(Body)};
        const auto *Figure = Equipment->Figures.FindByPredicate([&](const auto &F) { return F.Model == Model; });
        if (Figure)
        {
            for (const auto &Geometry : Figure->Geometry)
                if (!Geometry.Mesh.IsNull())
                    Meshes.Add(Geometry.Mesh.ToSoftObjectPath());
            if (const auto *Custom = Equipment->Customizations.FindByPredicate([&](const auto &C) { return C.Figure == Figure->Figure; }))
                for (const auto &Hair : Custom->Hair)
                    if (!Hair.Mesh.IsNull())
                        Meshes.Add(Hair.Mesh.ToSoftObjectPath());
        }
        UE_LOG(LogTemp, Display, TEXT("Live crowd model %d: family %d, %d clips, %d body/armor/hair meshes"), Model, Family, Clips.Num(), Meshes.Num());
        for (const auto &Path : Meshes)
        {
            auto *Mesh = Cast<USkeletalMesh>(Path.TryLoad());
            if (!Mesh)
                return 1;
            Part->SetSkeletalMesh(Mesh);
            // Prepare every shared material used by the actual equipment presenter.
            for (auto *Material : {Equipment->ArmorMaterial.Get(), Equipment->FaceMaterial.Get(), Equipment->HairMaterial.Get()})
                if (Material)
                {
                    for (int32 M = 0; M < Part->GetNumMaterials(); ++M)
                        Part->SetMaterial(M, Material);
                    if (!UCamelotCrowdComponent::PrepareTemplateForInstancing(Part, true))
                        return 1;
                }
            for (int32 M = 0; M < Mesh->GetMaterials().Num(); ++M)
                Part->SetMaterial(M, Mesh->GetMaterials()[M].MaterialInterface);
            if (!UCamelotCrowdComponent::PrepareTemplateForInstancing(Part, true))
                return 1;
            const FString SetPath = Output / TEXT("Providers") / FString::Printf(TEXT("AS_Live_%d_%s"), Family, *Mesh->GetName());
            auto *Set = LoadObject<UCamelotCrowdAnimationSet>(nullptr, *SetPath, nullptr, LOAD_NoWarn);
            if (!Set)
            {
                Set = UCamelotCrowdComponent::PrepareAnimationSet(Part, Clips, SetPath);
                if (!Set)
                    return 1;
                ++ProvidersSaved;
            }
            if (Set->Providers.Num() != 1 || Set->Providers[0]->GetSkinnedAsset() != Mesh || Set->Providers[0]->GetSequences().Num() != Clips.Num())
            {
                UE_LOG(LogTemp, Error, TEXT("Provider %s differs; use a new Output path"), *SetPath);
                return 1;
            }
            for (int32 I = 0; I < Clips.Num(); ++I)
                if (Set->Providers[0]->GetSequences()[I].Sequence != Clips[I])
                {
                    UE_LOG(LogTemp, Error, TEXT("Provider %s clip order differs; use a new Output path"), *SetPath);
                    return 1;
                }
            Prepared.MeshProviders.Add(Path, TSoftObjectPtr<UCamelotCrowdAnimationSet>(Set));
        }
        Catalog->Models.Add(Model, MoveTemp(Prepared));
        ++Completed;
        if (Completed % 8 == 0)
        {
            Part->SetSkeletalMesh(nullptr);
            CollectGarbage(RF_NoFlags);
        }
    }
    // Rigid weapon/shield materials need their cooked instancing permutations
    // too. Preparing only the humanoid meshes would leave other weapon types
    // permanently on the full presenter in packaged builds.
    if (Completed > 0 && !FParse::Param(*Params, TEXT("SkipRigidMaterials")))
    {
        const int32 *PreparedModel = Models.FindByPredicate([&](int32 M) { return Catalog->Models.Contains(M); });
        if (!PreparedModel)
            return 1;
        const auto &Prepared = Catalog->Models.FindChecked(*PreparedModel);
        if (Prepared.MeshProviders.IsEmpty())
            return 1;
        Part->SetSkeletalMesh(Cast<USkeletalMesh>(Prepared.MeshProviders.CreateConstIterator().Key().TryLoad()));
        auto *Rigid = NewObject<UStaticMeshComponent>(Actor);
        Rigid->SetupAttachment(Part);
        Rigid->RegisterComponent();
        TSet<FSoftObjectPath> RigidMeshes;
        for (const auto &Item : Equipment->Items)
            if (!Item.Mesh.IsNull())
                RigidMeshes.Add(Item.Mesh.ToSoftObjectPath());
        int32 RigidCount = 0;
        for (const auto &Path : RigidMeshes)
        {
            auto *Mesh = Cast<UStaticMesh>(Path.TryLoad());
            if (!Mesh)
            {
                UE_LOG(LogTemp, Error, TEXT("Missing rigid equipment asset %s"), *Path.ToString());
                return 1;
            }
            Rigid->SetStaticMesh(Mesh);
            if (!UCamelotCrowdComponent::PrepareTemplateForInstancing(Part, true))
                return 1;
            ++RigidCount;
            if (RigidCount % 32 == 0)
            {
                Rigid->SetStaticMesh(nullptr);
                CollectGarbage(RF_NoFlags);
            }
        }
        UE_LOG(LogTemp, Display, TEXT("Live crowd rigid material preparation: %d equipment meshes"), RigidCount);
    }
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("LiveCrowdPreparationBackups") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    const auto Save = [&](UObject *Asset) {
        auto *Package = Asset->GetOutermost();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        if (IFileManager::Get().FileExists(*Filename))
        {
            const FString Destination = Backup / Package->GetName().RightChop(1) + TEXT(".uasset");
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
            if (IFileManager::Get().Copy(*Destination, *Filename) != COPY_OK)
                return false;
        }
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        Package->MarkPackageDirty();
        return UPackage::SavePackage(Package, Asset, *Filename, Args);
    };
    if (!Save(Catalog))
        return 1;
    if (NewCatalog)
        FAssetRegistryModule::AssetCreated(Catalog);
    if (Assets)
    {
        Assets->LiveCrowdCatalog = Catalog;
        if (!Save(Assets))
            return 1;
    }
    UE_LOG(LogTemp, Display, TEXT("Live crowd preparation complete: %d models, %d new providers, %d unsupported models retain full presentation, %s"), Completed, ProvidersSaved, Skipped, *Catalog->GetPathName());
    return Completed > 0 ? 0 : 1;
#endif
}
