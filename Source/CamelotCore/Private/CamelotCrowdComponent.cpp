#include "CamelotCrowdComponent.h"
#include "CamelotCharacterAnimInstance.h"
#include "ProfilingDebugging/CsvProfiler.h"
CSV_DEFINE_CATEGORY(CamelotCrowd, true);
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#endif

namespace
{
double CrowdTime(const UWorld *World)
{
    // Match the native animation-provider clock (including its diagnostic time scale).
    static const auto *TimeScale = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.AnimSequenceTransformProvider.TimeScale"));
    return World ? World->GetTimeSeconds() * (TimeScale ? TimeScale->GetValueOnGameThread() : 1.f) : FApp::GetCurrentTime();
}
TArray<USkeletalMeshComponent *> GatherParts(USkeletalMeshComponent *Body)
{
    TArray<USkeletalMeshComponent *> Result;
    if (!Body || !Body->GetSkeletalMeshAsset())
        return Result;
    Result.Add(Body);
    TArray<USceneComponent *> Children;
    Body->GetChildrenComponents(true, Children);
    for (USceneComponent *Child : Children)
        if (auto *Part = Cast<USkeletalMeshComponent>(Child))
            if (Part->GetSkeletalMeshAsset() && Part->IsVisible())
                Result.Add(Part);
    return Result;
}
void CopyMaterials(UMeshComponent *Source, UMeshComponent *Destination)
{
    TMap<UMaterialInterface *, UMaterialInterface *> Copies;
    for (int32 M = 0; M < Source->GetNumMaterials(); ++M)
    {
        auto *Material = Source->GetMaterial(M);
        if (!Copies.Contains(Material))
        {
            auto *Copy = Material;
            if (auto *Dynamic = Cast<UMaterialInstanceDynamic>(Material))
            {
                auto *Owned = UMaterialInstanceDynamic::Create(Dynamic->Parent, Destination);
                Owned->CopyParameterOverrides(Dynamic);
                Copy = Owned;
            }
            Copies.Add(Material, Copy);
        }
        Destination->SetMaterial(M, Copies.FindChecked(Material));
    }
}
} // namespace

UCamelotCrowdAnimationSet *UCamelotCrowdComponent::PrepareAnimationSet(USkeletalMeshComponent *Template, const TArray<UAnimSequence *> &Sequences, const FString &PackagePath)
{
#if WITH_EDITOR
    if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath) || FPackageName::DoesPackageExist(PackagePath) || FindObject<UObject>(nullptr, *(PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath))))
        return nullptr;
    auto *Package = CreatePackage(*PackagePath);
    auto *Set = NewObject<UCamelotCrowdAnimationSet>(Package, *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone);
    for (auto *Provider : PrepareProviders(Template, Sequences, Set))
        Set->Providers.Add(Provider);
    if (Set->Providers.IsEmpty())
        return nullptr;
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    const FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    if (!UPackage::SavePackage(Package, Set, *Filename, Args))
        return nullptr;
    FAssetRegistryModule::AssetCreated(Set);
    return Set;
#else
    return nullptr;
#endif
}

bool UCamelotCrowdComponent::GetCharacterSocketTransform(int32 ObjectId, FName Socket, FTransform &Transform) const
{
    const auto *Character = Characters.Find(ObjectId);
    if (!Character || Weapons.IsEmpty() || !Poses.IsValidIndex(Character->Track) || !Poses[Character->Track])
        return false;
    const auto *Pose = Poses[Character->Track].Get();
    if (!Pose->DoesSocketExist(Socket))
        return false;
    Transform = Pose->GetSocketTransform(Socket, RTS_Component) * Character->Transform;
    return true;
}
UCamelotCrowdComponent::UCamelotCrowdComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

bool UCamelotCrowdComponent::PrepareTemplateForInstancing(USkeletalMeshComponent *Template, bool bSavePackages)
{
#if WITH_EDITOR
    const auto Sources = GatherParts(Template);
    if (Sources.IsEmpty())
        return false;
    TSet<UPackage *> Packages;
    const auto PrepareMaterial = [&](UMaterialInterface *Material, EMaterialUsage Usage) {
        if (!Material)
            return false;
        auto *Base = Material->GetMaterial();
        if (!Base)
            return false;
        if (Base->bUsedAsSpecialEngineMaterial)
            return true;
        if (!Base->GetUsageByFlag(Usage))
        {
            Base->SetUsageByFlag(Usage, true);
            Base->PostEditChange();
            Packages.Add(Base->GetOutermost());
        }
        return Material->CheckMaterialUsage(Usage);
    };
    for (auto *Source : Sources)
    {
        auto *Mesh = Source->GetSkeletalMeshAsset();
        bool bRebuild = false;
        for (int32 L = 0; L < Mesh->GetLODNum(); ++L)
            if (!Mesh->GetLODInfo(L)->BuildSettings.bOptimizeForInstancing)
            {
                Mesh->GetLODInfo(L)->BuildSettings.bOptimizeForInstancing = true;
                bRebuild = true;
            }
        if (bRebuild)
        {
            Mesh->Build();
            Mesh->MarkPackageDirty();
            Packages.Add(Mesh->GetOutermost());
        }
        for (int32 M = 0; M < Source->GetNumMaterials(); ++M)
            if (auto *Material = Source->GetMaterial(M))
            {
                if (!PrepareMaterial(Material, MATUSAGE_InstancedSkinnedMesh))
                    return false;
            }
    }
    TArray<USceneComponent *> Children;
    Template->GetChildrenComponents(true, Children);
    for (auto *Child : Children)
        if (auto *Weapon = Cast<UStaticMeshComponent>(Child))
            for (int32 M = 0; M < Weapon->GetNumMaterials(); ++M)
                if (auto *Material = Weapon->GetMaterial(M))
                {
                    if (!PrepareMaterial(Material, MATUSAGE_InstancedStaticMeshes))
                        return false;
                }
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (bSavePackages)
    {
        const FString Backup = FPaths::ProjectSavedDir() / TEXT("CrowdPreparationBackups") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
        for (auto *Package : Packages)
        {
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
            if (!UPackage::SavePackage(Package, nullptr, *Filename, Args))
                return false;
        }
    }
    return true;
#else
    return false;
#endif
}

TArray<UAnimSequenceTransformProviderData *> UCamelotCrowdComponent::PrepareProviders(USkeletalMeshComponent *Template, const TArray<UAnimSequence *> &Sequences, UObject *Outer)
{
    TArray<UAnimSequenceTransformProviderData *> Result;
#if WITH_EDITOR
    if (!Template || !Outer || Sequences.IsEmpty())
        return Result;
    TArray<FAnimSequenceTransformProviderSequence> Entries;
    for (auto *Clip : Sequences)
    {
        if (!Clip)
            return {};
        FAnimSequenceTransformProviderSequence E;
        E.Sequence = Clip;
        Entries.Add(E);
    }
    for (auto *Source : GatherParts(Template))
    {
        auto *Provider = NewObject<UAnimSequenceTransformProviderData>(Outer);
        // UE 5.8 exposes editor properties but no C++ authoring setters. Like the
        // engine's crowd pipeline, author these once before cache generation/cooking.
        auto *MeshProperty = FindFProperty<FObjectPropertyBase>(Provider->GetClass(), TEXT("SkinnedAsset"));
        auto *SequenceProperty = FindFProperty<FArrayProperty>(Provider->GetClass(), TEXT("Sequences"));
        auto *LayerProperty = FindFProperty<FArrayProperty>(Provider->GetClass(), TEXT("Layers"));
        if (!MeshProperty || !SequenceProperty || !LayerProperty)
            return {};
        MeshProperty->SetObjectPropertyValue_InContainer(Provider, Source->GetSkeletalMeshAsset());
        *SequenceProperty->ContainerPtrToValuePtr<TArray<FAnimSequenceTransformProviderSequence>>(Provider) = Entries;
        LayerProperty->ContainerPtrToValuePtr<TArray<FAnimSequenceTransformProviderLayer>>(Provider)->SetNum(4);
        FPropertyChangedEvent Changed(nullptr);
        Provider->PostEditChangeProperty(Changed);
        Result.Add(Provider);
    }
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
    return Result;
}

bool UCamelotCrowdComponent::PrepareBlendLayers(UAnimSequenceTransformProviderData *Provider)
{
#if WITH_EDITOR
    if (!Provider || (Provider->GetNumLayers() != 1 && Provider->GetNumLayers() != 4))
        return false;
    if (Provider->GetNumLayers() == 4)
        return true;
    const auto &Layer = Provider->GetLayers()[0];
    if (Layer.BlendMode != EAnimSequenceTransformProviderLayerBlendMode::Override || Layer.Weight != 1 || !Layer.BoneMaskProfileName.IsNone() || Layer.bMeshSpaceRotation || Provider->GetLayerStack())
        return false;
    auto *Property = FindFProperty<FArrayProperty>(Provider->GetClass(), TEXT("Layers"));
    if (!Property)
        return false;
    Provider->Modify();
    Property->ContainerPtrToValuePtr<TArray<FAnimSequenceTransformProviderLayer>>(Provider)->SetNum(4);
    FPropertyChangedEvent Changed(Property);
    Provider->PostEditChangeProperty(Changed);
    FAssetCompilingManager::Get().FinishAllCompilation();
    return true;
#else
    return false;
#endif
}
bool UCamelotCrowdComponent::Initialize(USkeletalMeshComponent *Template, const TArray<UAnimSequenceTransformProviderData *> &Providers, int32 TrackCount)
{
    ResetBatch();
    auto Sources = GatherParts(Template);
    if (Sources.IsEmpty() || Sources.Num() != Providers.Num() || TrackCount < 1 || TrackCount > MaxTrackCount)
        return false;
    for (int32 I = 0; I < Sources.Num(); ++I)
    {
        if (!Providers[I] || !Providers[I]->IsEnabled() || Providers[I]->IsCompiling() || Providers[I]->GetSkinnedAsset() != Sources[I]->GetSkeletalMeshAsset() || Providers[I]->GetSequences().IsEmpty() || (Providers[I]->GetNumLayers() != 1 && Providers[I]->GetNumLayers() != 4) || Providers[I]->GetNumLayers() != Providers[0]->GetNumLayers())
            return false;
        const auto *RenderData = Sources[I]->GetSkeletalMeshAsset()->GetResourceForRendering();
        for (const auto &Layer : Providers[I]->GetLayers())
            if (Layer.BlendMode != EAnimSequenceTransformProviderLayerBlendMode::Override || Layer.Weight != 1 || !Layer.BoneMaskProfileName.IsNone() || Layer.bMeshSpaceRotation)
                return false;
        if (!RenderData || !RenderData->HasUnifiedBoneMap())
            return false;
        for (int32 M = 0; M < Sources[I]->GetNumMaterials(); ++M)
            if (auto *Material = Sources[I]->GetMaterial(M))
            {
                auto *Base = Material->GetMaterial();
                if (!Base || (!Base->bUsedAsSpecialEngineMaterial && !Base->GetUsageByFlag(MATUSAGE_InstancedSkinnedMesh)))
                    return false;
            }
        if (I > 0 && (Sources[I]->GetAttachParent() != Template || !Sources[I]->GetRelativeTransform().Equals(FTransform::Identity) || !Sources[I]->GetAttachSocketName().IsNone()))
            return false;
        const auto &A = Providers[0]->GetSequences();
        const auto &B = Providers[I]->GetSequences();
        if (A.Num() != B.Num())
            return false;
        for (int32 J = 0; J < A.Num(); ++J)
            if (!A[J].Sequence || A[J].Sequence != B[J].Sequence)
                return false;
    }
    TArray<USceneComponent *> RigidChildren;
    Template->GetChildrenComponents(true, RigidChildren);
    for (auto *Child : RigidChildren)
        if (auto *Weapon = Cast<UStaticMeshComponent>(Child))
            if (Weapon->GetStaticMesh() && Weapon->GetVisibleFlag())
            {
                if (Weapon->GetAttachParent() != Template || Weapon->GetAttachSocketName().IsNone())
                    return false;
                for (int32 M = 0; M < Weapon->GetNumMaterials(); ++M)
                    if (auto *Material = Weapon->GetMaterial(M))
                    {
                        auto *Base = Material->GetMaterial();
                        if (!Base || (!Base->bUsedAsSpecialEngineMaterial && !Base->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes)))
                            return false;
                    }
            }
    for (const auto &Entry : Providers[0]->GetSequences())
        Clips.Add(Entry.Sequence);
    for (int32 I = 0; I < Sources.Num(); ++I)
    {
        auto *Source = Sources[I];
        auto *Batch = NewObject<UInstancedSkinnedMeshComponent>(GetOwner());
        Batch->SetSkinnedAssetAndUpdate(Source->GetSkeletalMeshAsset());
        Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch->SetCanEverAffectNavigation(false);
        // Modular pieces must not independently stop animating at different sizes.
        Batch->SetAnimationMinScreenSize(-1.f);
        Batch->SetHasPerInstancePrevTransforms(true);
        CopyMaterials(Source, Batch);
        for (int32 L = 0; L < Source->GetSkeletalMeshAsset()->GetLODNum(); ++L)
            for (int32 M = 0; M < Source->GetNumMaterials(); ++M)
                if (!Source->IsMaterialSectionShown(M, L))
                    Batch->ShowMaterialSection(M, INDEX_NONE, false, L);
        auto *Animation = UAnimSequenceTransformProviderDataInstance::CreateAnimSequenceTransformProviderDataInstance(Providers[I], Batch);
        for (int32 T = 0; T < TrackCount; ++T)
            if (Animation->AllocateTrack() != T)
            {
                ResetBatch();
                Batch->DestroyComponent();
                return false;
            }
        Batch->SetTransformProvider(Animation);
        Batch->RegisterComponent();
        Parts.Add(Batch);
        Animations.Add(Animation);
    }
    Playback.SetNum(TrackCount);
    bLayeredBlending = Providers[0]->GetNumLayers() == 4;
    TrackUsers.Init(0, TrackCount);
    HeldActions.Init(false, TrackCount);
    // Only this small, bounded pool evaluates CPU poses, to attach conventional
    // static weapons. Bodies and all modular armor use GPU animation providers.
    for (int32 T = 0; T < TrackCount; ++T)
    {
        auto *Pose = NewObject<USkeletalMeshComponent>(GetOwner());
        Pose->SetSkeletalMesh(Template->GetSkeletalMeshAsset());
        Pose->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Pose->SetVisibility(false);
        Pose->SetCastShadow(false);
        Pose->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Pose->RegisterComponent();
        Pose->SetComponentTickEnabled(false);
        Poses.Add(Pose);
    }
    TArray<USceneComponent *> Children;
    Template->GetChildrenComponents(true, Children);
    for (auto *Child : Children)
        if (auto *Weapon = Cast<UStaticMeshComponent>(Child))
        {
            if (!Weapon->GetStaticMesh() || Weapon->GetAttachParent() != Template || Weapon->GetAttachSocketName().IsNone())
                continue;
            auto *Batch = NewObject<UInstancedStaticMeshComponent>(GetOwner());
            Batch->SetRemoveSwap();
            Batch->SetHasPerInstancePrevTransforms(true);
            Batch->SetStaticMesh(Weapon->GetStaticMesh());
            Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Batch->SetCanEverAffectNavigation(false);
            CopyMaterials(Weapon, Batch);
            Batch->RegisterComponent();
            Weapons.Add(Batch);
            Attachments.Add({Weapon->GetAttachSocketName(), Weapon->GetRelativeTransform()});
        }
    for (int32 T = 0; T < TrackCount; ++T)
        PlayTrack(T, 0, true);
    return true;
}

bool UCamelotCrowdComponent::AddCharacter(int32 ObjectId, const FTransform &Transform, int32 Track)
{
    if (Characters.Contains(ObjectId) || !Poses.IsValidIndex(Track) || Parts.IsEmpty() || Transform.ContainsNaN() || (LoopTrackCount > 0 && Track >= LoopTrackCount))
        return false;
    FCharacter Character{Transform, Track, {}};
    Character.ReturnTrack = Track;
    for (auto Part : Parts)
        Character.Parts.Add(Part->AddInstance(Transform, Track, true));
    for (auto Weapon : Weapons)
        Weapon->AddInstance(Transform, true);
    ++TrackUsers[Track];
    Characters.Add(ObjectId, MoveTemp(Character));
    Order.Add(ObjectId);
    return true;
}
bool UCamelotCrowdComponent::UpdateCharacter(int32 ObjectId, const FTransform &Transform, int32 Track)
{
    auto *Character = Characters.Find(ObjectId);
    if (!Character || !Poses.IsValidIndex(Track) || Transform.ContainsNaN() || (LoopTrackCount > 0 && Track >= LoopTrackCount))
        return false;
    AssignTrack(*Character, Track);
    Character->ReturnTrack = Track;
    Character->bAction = false;
    return MoveCharacter(ObjectId, Transform);
}
bool UCamelotCrowdComponent::MoveCharacter(int32 ObjectId, const FTransform &Transform, bool bTeleport)
{
    auto *Character = Characters.Find(ObjectId);
    if (!Character || Transform.ContainsNaN())
        return false;
    for (int32 I = 0; I < Parts.Num(); ++I)
    {
        // Preserve the transform at the start of this rendered frame if several
        // network/interpolation updates arrive before TickComponent.
        FTransform Previous = Character->Transform;
        if (Character->bMoved)
            Parts[I]->GetInstancePrevTransform(Character->Parts[I], Previous, true);
        Parts[I]->SetInstanceTransforms(Character->Parts[I], Transform, bTeleport ? Transform : Previous, true);
    }
    Character->Transform = Transform;
    Character->bMoved = true;
    Character->bTeleportWeapons |= bTeleport;
    return true;
}
bool UCamelotCrowdComponent::RemoveCharacter(int32 ObjectId)
{
    auto *Character = Characters.Find(ObjectId);
    if (!Character)
        return false;
    --TrackUsers[Character->Track];
    for (int32 I = 0; I < Parts.Num(); ++I)
        Parts[I]->RemoveInstance(Character->Parts[I]);
    const int32 Index = Order.Find(ObjectId);
    for (auto Weapon : Weapons)
        Weapon->RemoveInstance(Index);
    Order.RemoveAtSwap(Index);
    Characters.Remove(ObjectId);
    return true;
}
bool UCamelotCrowdComponent::PlayTrack(int32 Track, int32 Sequence, bool bLoop, float Position, float Rate)
{
    if (LoopTrackCount > 0 && Track >= LoopTrackCount)
        return false;
    return StartTrack(Track, Sequence, bLoop, Position, Rate);
}
bool UCamelotCrowdComponent::StartTrack(int32 Track, int32 Sequence, bool bLoop, float Position, float Rate, float BlendSeconds, int32 SourceTrack)
{
    if (!Playback.IsValidIndex(Track) || !Clips.IsValidIndex(Sequence) || !FMath::IsFinite(Rate) || Rate <= 0 || !FMath::IsFinite(Position))
        return false;
    FPlayback State{Sequence, Position, Rate, bLoop};
    State.Started = CrowdTime(GetWorld());
    State.BlendSeconds = BlendSeconds;
    if (bLayeredBlending)
    {
        if (!StartLayeredTrack(Track, State, SourceTrack))
            return false;
        Playback[Track] = MoveTemp(State);
        UpdateLayerWeights(Track);
        return true;
    }
    if (BlendSeconds > 0 && Playback.IsValidIndex(SourceTrack))
    {
        const auto &Source = Playback[SourceTrack];
        State.SourceSequence = Source.Sequence;
        State.SourceRate = Source.Rate;
        State.bSourceLoop = Source.bLoop;
        State.SourcePosition = Animations[0]->GetPosition(SourceTrack, 0).TargetPosition;
        State.BlendSeconds = BlendSeconds;
        if (Track != SourceTrack)
        {
            FAnimSequenceTrackAutoPlayData Seed;
            Seed.SequenceIndex = Source.Sequence;
            Seed.Position = State.SourcePosition;
            Seed.PlayRate = Source.Rate;
            Seed.LoopMode = Source.bLoop ? EAnimSequenceTrackLoopMode::Loop : EAnimSequenceTrackLoopMode::Clamp;
            // Submit the source before the destination: a pool slot's prior pose may
            // belong to somebody else, and multiple pending patches otherwise coalesce.
            for (auto Animation : Animations)
            {
                if (!Animation->SetAutoPlayData(Track, 0, Seed))
                    return false;
                Animation->SubmitChanges();
            }
        }
    }
    FAnimSequenceTrackAutoPlayData Data;
    Data.SequenceIndex = Sequence;
    Data.Position = Position;
    Data.PlayRate = Rate;
    Data.BlendTime = State.BlendSeconds;
    Data.LoopMode = bLoop ? EAnimSequenceTrackLoopMode::Loop : EAnimSequenceTrackLoopMode::Clamp;
    for (auto Animation : Animations)
        if (!Animation->SetAutoPlayData(Track, 0, Data))
            return false;
    Playback[Track] = State;
    return true;
}
int32 UCamelotCrowdComponent::TargetLayer(int32 Track) const
{
    return bLayeredBlending ? FMath::Max(0, Playback[Track].Samples.Num() - 1) : 0;
}
void UCamelotCrowdComponent::SampleLayers(int32 Track, TArray<FSample, TInlineAllocator<4>> &Samples) const
{
    const auto &State = Playback[Track];
    Samples = State.Samples;
    const float Alpha = BlendWeight(Track);
    for (int32 I = 0; I < Samples.Num(); ++I)
    {
        auto &Sample = Samples[I];
        Sample.Position = Animations[0]->GetPosition(Track, I).TargetPosition;
        Sample.Weight = State.BlendSeconds > 0 ? I == Samples.Num() - 1 ? Alpha : Sample.Weight * (1 - Alpha) : 1.f;
    }
}
bool UCamelotCrowdComponent::StartLayeredTrack(int32 Track, FPlayback &State, int32 SourceTrack)
{
    if (State.BlendSeconds > 0 && Playback.IsValidIndex(SourceTrack))
    {
        SampleLayers(SourceTrack, State.Samples);
        State.Samples.RemoveAll([](const auto &Sample) { return Sample.Weight <= UE_SMALL_NUMBER; });
        if (State.Samples.Num() >= 4)
            return false;
    }
    if (State.Samples.IsEmpty())
        State.BlendSeconds = 0;
    State.Samples.Add({State.Sequence, State.Position, State.Rate, 1, State.bLoop});
    for (auto Animation : Animations)
        for (int32 I = 0; I < State.Samples.Num(); ++I)
        {
            const auto &Sample = State.Samples[I];
            FAnimSequenceTrackAutoPlayData Data;
            Data.SequenceIndex = Sample.Sequence;
            Data.Position = Sample.Position;
            Data.PlayRate = Sample.Rate;
            Data.LoopMode = Sample.bLoop ? EAnimSequenceTrackLoopMode::Loop : EAnimSequenceTrackLoopMode::Clamp;
            if (!Animation->SetAutoPlayData(Track, I, Data))
                return false;
        }
    return true;
}
void UCamelotCrowdComponent::UpdateLayerWeights(int32 Track)
{
    auto &State = Playback[Track];
    const float Alpha = BlendWeight(Track);
    if (State.BlendSeconds > 0 && Alpha >= 1)
    {
        State.Position = Animations[0]->GetPosition(Track, TargetLayer(Track)).TargetPosition;
        State.Started = CrowdTime(GetWorld());
        State.BlendSeconds = 0;
        State.Samples.Reset();
        State.Samples.Add({State.Sequence, State.Position, State.Rate, 1, State.bLoop});
        FAnimSequenceTrackAutoPlayData Data;
        Data.SequenceIndex = State.Sequence;
        Data.Position = State.Position;
        Data.PlayRate = State.Rate;
        Data.LoopMode = State.bLoop ? EAnimSequenceTrackLoopMode::Loop : EAnimSequenceTrackLoopMode::Clamp;
        for (auto Animation : Animations)
            Animation->SetAutoPlayData(Track, 0, Data);
    }
    float Total = 0;
    for (int32 Layer = 0; Layer < 4; ++Layer)
    {
        const float Weight = State.Samples.IsValidIndex(Layer) ? State.BlendSeconds > 0 ? Layer == State.Samples.Num() - 1 ? Alpha : State.Samples[Layer].Weight * (1 - Alpha) : 1.f : 0.f;
        Total += Weight;
        const float Override = Layer == 0 ? 1.f : Total > 0 ? Weight / Total
                                                            : 0.f;
        for (auto Animation : Animations)
            if (!FMath::IsNearlyEqual(Animation->GetLayerWeight(Track, Layer), Override, UE_SMALL_NUMBER))
                Animation->SetLayerWeight(Track, Layer, Override);
    }
}
void UCamelotCrowdComponent::AssignTrack(FCharacter &Character, int32 Track)
{
    if (Character.Track == Track)
        return;
    --TrackUsers[Character.Track];
    ++TrackUsers[Track];
    Character.Track = Track;
    for (int32 I = 0; I < Parts.Num(); ++I)
        Parts[I]->SetInstanceAnimationIndex(Character.Parts[I], Track);
}
bool UCamelotCrowdComponent::ConfigureActionPool(int32 LoopTracks, float Tolerance)
{
    if (LoopTracks < 1 || LoopTracks >= Playback.Num() || !FMath::IsFinite(Tolerance) || Tolerance < 0 || Tolerance > .1f)
        return false;
    for (const auto &Pair : Characters)
        if (Pair.Value.bAction || Pair.Value.Track >= LoopTracks)
            return false;
    LoopTrackCount = LoopTracks;
    PhaseTolerance = Tolerance;
    return true;
}
bool UCamelotCrowdComponent::PlayCharacterAction(int32 ObjectId, int32 Sequence, int32 ReturnTrack, float Position, float Rate, bool bHold, bool bLoop, float BlendSeconds)
{
    auto *Character = Characters.Find(ObjectId);
    if (!Character || LoopTrackCount == 0 || ReturnTrack < 0 || ReturnTrack >= LoopTrackCount || !Clips.IsValidIndex(Sequence) || !FMath::IsFinite(Position) || Position < 0 || Position > Clips[Sequence]->GetPlayLength() || !FMath::IsFinite(Rate) || Rate <= 0 || !FMath::IsFinite(BlendSeconds) || BlendSeconds < 0 || BlendSeconds > 1)
        return false;
    if (!TransitionCharacter(*Character, Sequence, bLoop, Position, Rate, bHold, BlendSeconds))
        return false;
    Character->ReturnTrack = ReturnTrack;
    Character->bAction = true;
    Character->ReturnBlendSeconds = BlendSeconds > 0 ? .14f : 0;
    Character->ActionPosition = Position;
    Character->ActionStarted = CrowdTime(GetWorld());
    return true;
}
bool UCamelotCrowdComponent::TransitionCharacter(FCharacter &Character, int32 Sequence, bool bLoop, float Position, float Rate, bool bHold, float BlendSeconds)
{
    // Retain every contributing pose on interruption. Promote only if the bounded
    // layer budget (or an older single-layer provider) cannot represent the mix.
    TArray<FSample, TInlineAllocator<4>> Sources;
    if (bLayeredBlending)
    {
        SampleLayers(Character.Track, Sources);
        Sources.RemoveAll([](const auto &Sample) { return Sample.Weight <= UE_SMALL_NUMBER; });
    }
    if (BlendSeconds > 0 && (bLayeredBlending ? Sources.Num() >= 4 : BlendWeight(Character.Track) < 1))
        return false;
    const auto &Source = Playback[Character.Track];
    const float SourcePosition = Animations[0]->GetPosition(Character.Track, TargetLayer(Character.Track)).TargetPosition;
    int32 Track = INDEX_NONE, Free = INDEX_NONE;
    for (int32 T = LoopTrackCount; T < Playback.Num(); ++T)
    {
        if (TrackUsers[T] == 0)
        {
            if (Free == INDEX_NONE)
                Free = T;
            continue;
        }
        // Never restart a referenced track. Compatible same-phase events may
        // share it; holding deaths and returning attacks remain separate.
        const auto &Candidate = Playback[T];
        UAnimSequence *TargetClip = nullptr, *SourceClip = nullptr;
        float TargetTime = 0, SourceTime = 0, Weight = 1;
        SampleTrack(T, TargetClip, TargetTime, SourceClip, SourceTime, Weight);
        bool SameSource = BlendSeconds == 0 ? Weight >= 1 : (Candidate.SourceSequence == Source.Sequence && Candidate.SourceRate == Source.Rate && Candidate.bSourceLoop == Source.bLoop && Candidate.BlendSeconds == BlendSeconds && Weight <= .1f && FMath::Abs(SourceTime - SourcePosition) <= PhaseTolerance);
        if (bLayeredBlending && BlendSeconds > 0)
        {
            TArray<FSample, TInlineAllocator<4>> CandidateSamples;
            SampleLayers(T, CandidateSamples);
            SameSource = Weight <= .1f && Candidate.BlendSeconds == BlendSeconds && CandidateSamples.Num() == Sources.Num() + 1;
            for (int32 I = 0; SameSource && I < Sources.Num(); ++I)
                SameSource = CandidateSamples[I].Sequence == Sources[I].Sequence && CandidateSamples[I].Rate == Sources[I].Rate && CandidateSamples[I].bLoop == Sources[I].bLoop && FMath::Abs(CandidateSamples[I].Position - Sources[I].Position) <= PhaseTolerance && FMath::Abs(CandidateSamples[I].Weight / FMath::Max(.001f, 1 - Weight) - Sources[I].Weight) <= .01f;
        }
        if (SameSource && Playback[T].Sequence == Sequence && Playback[T].Rate == Rate && HeldActions[T] == bHold && Playback[T].bLoop == bLoop && FMath::Abs(Animations[0]->GetPosition(T, TargetLayer(T)).TargetPosition - Position) <= PhaseTolerance)
        {
            Track = T;
            break;
        }
    }
    if (Track == INDEX_NONE)
    {
        if (Free == INDEX_NONE)
        {
            // Network actions can arrive before the next batch tick. Completed
            // returns already display their locomotion cohort, so release their
            // temporary slots before requesting a full-renderer fallback.
            for (auto &Pair : Characters)
            {
                auto &Returning = Pair.Value;
                if (&Returning != &Character && !Returning.bAction && Returning.Track >= LoopTrackCount && BlendWeight(Returning.Track) >= 1)
                    AssignTrack(Returning, Returning.ReturnTrack);
            }
            for (int32 T = LoopTrackCount; T < Playback.Num(); ++T)
                if (TrackUsers[T] == 0)
                {
                    Free = T;
                    break;
                }
        }
        // The caller may safely replace its own unshared action under pressure.
        if (Character.Track >= LoopTrackCount && TrackUsers[Character.Track] == 1)
            Free = Character.Track;
        if (Free == INDEX_NONE)
        {
            ++ActionPoolMisses;
            return false;
        }
        if (!StartTrack(Free, Sequence, bLoop, Position, Rate, BlendSeconds, Character.Track))
            return false;
        HeldActions[Free] = bHold;
        Track = Free;
    }
    AssignTrack(Character, Track);
    return true;
}
bool UCamelotCrowdComponent::SetCharacterLocomotion(int32 ObjectId, int32 Track, bool bInterrupt, float BlendSeconds)
{
    auto *Character = Characters.Find(ObjectId);
    if (!Character || LoopTrackCount == 0 || Track < 0 || Track >= LoopTrackCount || !FMath::IsFinite(BlendSeconds) || BlendSeconds < 0 || BlendSeconds > 1)
        return false;
    if ((!Character->bAction && Character->ReturnTrack == Track) || (Character->bAction && !bInterrupt))
    {
        Character->ReturnTrack = Track;
        return true;
    }
    if (BlendSeconds > 0 && Character->Track != Track)
    {
        const auto &Target = Playback[Track];
        if (!TransitionCharacter(*Character, Target.Sequence, true, Animations[0]->GetPosition(Track, TargetLayer(Track)).TargetPosition, Target.Rate, false, BlendSeconds))
            return false;
    }
    else
        AssignTrack(*Character, Track);
    Character->ReturnTrack = Track;
    Character->bAction = false;
    return true;
}
int32 UCamelotCrowdComponent::GetCharacterTrack(int32 ObjectId) const
{
    const auto *Character = Characters.Find(ObjectId);
    return Character ? Character->Track : INDEX_NONE;
}
int32 UCamelotCrowdComponent::GetActivePoseCount() const
{
    int32 Count = 0;
    for (int32 Users : TrackUsers)
        if (Users > 0)
            ++Count;
    return Count;
}
void UCamelotCrowdComponent::TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction *Tick)
{
    CSV_SCOPED_TIMING_STAT(CamelotCrowd, UpdateBatch);
    Super::TickComponent(DeltaTime, Type, Tick);
    if (Characters.IsEmpty())
        return;
    if (bLayeredBlending)
        for (int32 T = 0; T < Playback.Num(); ++T)
            if (Playback[T].BlendSeconds > 0)
                UpdateLayerWeights(T);
    // Once a transition has finished, its source no longer matters. Merge only
    // settled action poses within the consumer's existing phase tolerance, freeing
    // slots for new blends instead of multiplying steady-state CPU weapon poses.
    TArray<int32, TInlineAllocator<MaxTrackCount>> Merge;
    Merge.Init(INDEX_NONE, Playback.Num());
    for (int32 T = LoopTrackCount; LoopTrackCount > 0 && T < Playback.Num(); ++T)
    {
        if (TrackUsers[T] == 0 || BlendWeight(T) < 1)
            continue;
        const auto &State = Playback[T];
        const float Position = Animations[0]->GetPosition(T, TargetLayer(T)).TargetPosition;
        for (int32 Other = LoopTrackCount; Other < T; ++Other)
        {
            if (TrackUsers[Other] == 0 || Merge[Other] != INDEX_NONE || BlendWeight(Other) < 1)
                continue;
            const auto &Candidate = Playback[Other];
            if (State.Sequence == Candidate.Sequence && State.Rate == Candidate.Rate && State.bLoop == Candidate.bLoop && HeldActions[T] == HeldActions[Other] && FMath::Abs(Position - Animations[0]->GetPosition(Other, TargetLayer(Other)).TargetPosition) <= PhaseTolerance)
            {
                Merge[T] = Other;
                break;
            }
        }
    }
    for (auto &Pair : Characters)
    {
        auto &Character = Pair.Value;
        if (Character.bAction && Merge[Character.Track] != INDEX_NONE)
        {
            const int32 Candidate = Merge[Character.Track];
            const auto &State = Playback[Candidate];
            const float Expected = FAnimSequenceTrackPackedData::WrapPosition(State.bLoop ? EAnimSequenceTrackLoopMode::Loop : EAnimSequenceTrackLoopMode::Clamp, Character.ActionPosition + (CrowdTime(GetWorld()) - Character.ActionStarted) * State.Rate, Clips[State.Sequence]->GetPlayLength());
            // Phase tolerance is relative to the confirmed action, so repeated
            // merges cannot accumulate timing drift across a long cast or attack.
            if (FMath::Abs(Expected - Animations[0]->GetPosition(Candidate, TargetLayer(Candidate)).TargetPosition) <= PhaseTolerance)
                AssignTrack(Character, Candidate);
        }
        if (Character.bAction && !HeldActions[Character.Track] && !Playback[Character.Track].bLoop && Animations[0]->GetPosition(Character.Track, TargetLayer(Character.Track)).TargetPosition >= Clips[Playback[Character.Track].Sequence]->GetPlayLength() - KINDA_SMALL_NUMBER)
        {
            SetCharacterLocomotion(Pair.Key, Character.ReturnTrack, true, Character.ReturnBlendSeconds);
        }
        else if (!Character.bAction && Character.Track >= LoopTrackCount && BlendWeight(Character.Track) >= 1)
            AssignTrack(Character, Character.ReturnTrack);
        if (!Character.bMoved)
            for (int32 I = 0; I < Parts.Num(); ++I)
                Parts[I]->SetInstancePrevTransform(Character.Parts[I], Character.Transform, true);
        Character.bMoved = false;
    }
    if (Weapons.IsEmpty())
        return;
    {
        CSV_SCOPED_TIMING_STAT(CamelotCrowd, WeaponPoses);
        for (int32 T = 0; T < Poses.Num(); ++T)
        {
            if (TrackUsers[T] == 0)
                continue;
            // Read the same engine clock as the GPU provider, including its loop/clamp
            // semantics. Frame deltas would put hand attachments one frame out of sync.
            if (bLayeredBlending)
            {
                TArray<FSample, TInlineAllocator<4>> Samples;
                SampleLayers(T, Samples);
                TArray<FCamelotAnimationSample, TInlineAllocator<4>> Pose;
                for (const auto &Sample : Samples)
                    Pose.Add({Clips[Sample.Sequence], Sample.Position, Sample.Weight});
                if (auto *Instance = UCamelotCharacterAnimInstance::Ensure(Poses[T]))
                    Instance->SetCrowdSamples(Pose);
            }
            else
            {
                UAnimSequence *Target = nullptr, *Source = nullptr;
                float TargetPosition = 0, SourcePosition = 0, Weight = 1;
                SampleTrack(T, Target, TargetPosition, Source, SourcePosition, Weight);
                if (auto *Instance = UCamelotCharacterAnimInstance::Ensure(Poses[T]))
                    Instance->SetCrowdPose(Target, TargetPosition, Source, SourcePosition, Weight);
            }
            Poses[T]->TickAnimation(0, false);
            Poses[T]->RefreshBoneTransforms();
        }
    }
    CSV_SCOPED_TIMING_STAT(CamelotCrowd, WeaponTransforms);
    for (int32 W = 0; W < Weapons.Num(); ++W)
    {
        TArray<FTransform> SocketTransforms;
        SocketTransforms.SetNum(Poses.Num());
        for (int32 T = 0; T < Poses.Num(); ++T)
            if (TrackUsers[T] > 0)
                SocketTransforms[T] = Attachments[W].Relative * Poses[T]->GetSocketTransform(Attachments[W].Socket, RTS_Component);
        TArray<FTransform> Transforms;
        Transforms.Reserve(Order.Num());
        TArray<FTransform> Previous;
        Previous.Reserve(Order.Num());
        for (int32 Index = 0; Index < Order.Num(); ++Index)
        {
            const auto &Character = Characters.FindChecked(Order[Index]);
            const FTransform Current = SocketTransforms[Character.Track] * Character.Transform;
            FTransform Before = Current;
            if (!Character.bTeleportWeapons)
                Weapons[W]->GetInstanceTransform(Index, Before, true);
            Transforms.Add(Current);
            Previous.Add(Before);
        }
        // UE 5.8's instance manager queues TransformChanged updates itself.
        // Recreating the entire render state here defeats incremental uploads.
        Weapons[W]->BatchUpdateInstancesTransforms(0, Transforms, Previous, true, false);
    }
    for (auto &Pair : Characters)
        Pair.Value.bTeleportWeapons = false;
}
bool UCamelotCrowdComponent::GetCharacterPlayback(int32 ObjectId, UAnimSequence *&Clip, float &Position, bool &Loop, float &Rate) const
{
    const auto *Character = Characters.Find(ObjectId);
    if (!Character || Animations.IsEmpty())
        return false;
    const auto &State = Playback[Character->Track];
    Clip = Clips[State.Sequence];
    Position = Animations[0]->GetPosition(Character->Track, TargetLayer(Character->Track)).TargetPosition;
    Loop = State.bLoop;
    Rate = State.Rate;
    return true;
}
float UCamelotCrowdComponent::BlendWeight(int32 Track) const
{
    const auto &State = Playback[Track];
    return State.BlendSeconds > 0 ? FMath::Clamp(float(CrowdTime(GetWorld()) - State.Started) / State.BlendSeconds, 0.f, 1.f) : 1.f;
}
void UCamelotCrowdComponent::SampleTrack(int32 Track, UAnimSequence *&Target, float &TargetPosition, UAnimSequence *&Source, float &SourcePosition, float &TargetWeight) const
{
    const auto &State = Playback[Track];
    Target = Clips[State.Sequence];
    TargetPosition = Animations[0]->GetPosition(Track, TargetLayer(Track)).TargetPosition;
    TargetWeight = BlendWeight(Track);
    if (bLayeredBlending)
    {
        TArray<FSample, TInlineAllocator<4>> Samples;
        SampleLayers(Track, Samples);
        Source = Samples.Num() > 1 ? Clips[Samples[0].Sequence].Get() : nullptr;
        SourcePosition = Samples[0].Position;
        return;
    }
    Source = nullptr;
    SourcePosition = TargetPosition;
    if (TargetWeight < 1 && Clips.IsValidIndex(State.SourceSequence))
    {
        Source = Clips[State.SourceSequence];
        const double Position = State.SourcePosition + (CrowdTime(GetWorld()) - State.Started) * State.SourceRate;
        SourcePosition = FAnimSequenceTrackPackedData::WrapPosition(State.bSourceLoop ? EAnimSequenceTrackLoopMode::Loop : EAnimSequenceTrackLoopMode::Clamp, Position, Source->GetPlayLength());
    }
}
bool UCamelotCrowdComponent::GetCharacterBlend(int32 ObjectId, UAnimSequence *&Target, float &TargetPosition, UAnimSequence *&Source, float &SourcePosition, float &TargetWeight) const
{
    const auto *Character = Characters.Find(ObjectId);
    if (!Character || Animations.IsEmpty() || (bLayeredBlending && Playback[Character->Track].Samples.Num() > 2))
        return false;
    SampleTrack(Character->Track, Target, TargetPosition, Source, SourcePosition, TargetWeight);
    return true;
}
bool UCamelotCrowdComponent::GetCharacterPose(int32 ObjectId, TArray<UAnimSequence *> &PoseClips, TArray<float> &Positions, TArray<float> &Weights) const
{
    const auto *Character = Characters.Find(ObjectId);
    if (!Character || Animations.IsEmpty())
        return false;
    PoseClips.Reset();
    Positions.Reset();
    Weights.Reset();
    if (bLayeredBlending)
    {
        TArray<FSample, TInlineAllocator<4>> Samples;
        SampleLayers(Character->Track, Samples);
        for (const auto &Sample : Samples)
        {
            PoseClips.Add(Clips[Sample.Sequence]);
            Positions.Add(Sample.Position);
            Weights.Add(Sample.Weight);
        }
    }
    else
    {
        UAnimSequence *Target = nullptr, *Source = nullptr;
        float TargetPosition = 0, SourcePosition = 0, Weight = 1;
        SampleTrack(Character->Track, Target, TargetPosition, Source, SourcePosition, Weight);
        if (Source && Weight < 1)
        {
            PoseClips.Add(Source);
            Positions.Add(SourcePosition);
            Weights.Add(1 - Weight);
        }
        PoseClips.Add(Target);
        Positions.Add(TargetPosition);
        Weights.Add(Weight);
    }
    return !PoseClips.IsEmpty();
}
void UCamelotCrowdComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    ResetBatch();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}
void UCamelotCrowdComponent::ResetBatch()
{
    for (auto Part : Parts)
        if (Part)
            Part->DestroyComponent();
    for (auto Weapon : Weapons)
        if (Weapon)
            Weapon->DestroyComponent();
    for (auto Pose : Poses)
        if (Pose)
            Pose->DestroyComponent();
    Parts.Reset();
    Animations.Reset();
    Weapons.Reset();
    Poses.Reset();
    Clips.Reset();
    Characters.Reset();
    Order.Reset();
    Attachments.Reset();
    Playback.Reset();
    TrackUsers.Reset();
    HeldActions.Reset();
    LoopTrackCount = 0;
    ActionPoolMisses = 0;
    PhaseTolerance = 0;
    bLayeredBlending = false;
}
void UCamelotCrowdComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ResetBatch();
    Super::EndPlay(Reason);
}
int32 UCamelotCrowdComponent::GetSkinnedInstanceCount() const
{
    int32 Count = 0;
    for (auto Part : Parts)
        Count += Part->GetInstanceCount();
    return Count;
}
int32 UCamelotCrowdComponent::GetWeaponInstanceCount() const
{
    int32 Count = 0;
    for (auto Weapon : Weapons)
        Count += Weapon->GetInstanceCount();
    return Count;
}
int32 UCamelotCrowdComponent::GetRecentlyRenderedBatchCount() const
{
    int32 Count = 0;
    for (auto Part : Parts)
        if (Part->WasRecentlyRendered())
            ++Count;
    for (auto Weapon : Weapons)
        if (Weapon->WasRecentlyRendered())
            ++Count;
    return Count;
}
