#include "DaocAnimationsCommandlet.h"
#include "Components/SkeletalMeshComponent.h"
#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

#endif
UDaocAnimationsCommandlet::UDaocAnimationsCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}
void UDaocAnimationsCommandlet::EvaluatePreviewPose(USkeletalMeshComponent *Component, float Time)
{
#if WITH_EDITOR
    if (Component && FMath::IsFinite(Time))
    {
        Component->SetPosition(Time, false);
        Component->TickAnimation(0.f, false);
        Component->RefreshBoneTransforms();
        Component->UpdateComponentToWorld();
        Component->MarkRenderDynamicDataDirty();
    }
#endif
}
int32 UDaocAnimationsCommandlet::Main(const FString &Params)
{
#if !WITH_EDITOR
    return 1;
#else
    FString Input, Text;
    FParse::Value(*Params, TEXT("Input="), Input);
    TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text, *Input) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json))
        return 1;
    const int32 Model = Json->GetIntegerField(TEXT("model"));
    auto *Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Model, Model));
    if (!Mesh || !Mesh->GetSkeleton())
        return 1;
    for (const auto &Value : Json->GetArrayField(TEXT("clips")))
    {
        auto Clip = Value->AsObject();
        const FString Action = Clip->GetStringField(TEXT("action"));
        if (Action.IsEmpty() || !Action.GetCharArray().ContainsByPredicate([](TCHAR C) { return FChar::IsAlpha(C); }))
            return 1;
        for (TCHAR C : Action)
            if (!FChar::IsAlnum(C) && C != TEXT('_'))
                return 1;
        const FString Name = FString::Printf(TEXT("A_daoc_%d_%s"), Model, *Action), Package = TEXT("/Game/DAoC/Animations/") + Name;
        auto *Existing = LoadObject<UAnimSequence>(nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn);
        if (Existing && !FParse::Param(*Params, TEXT("Reimport")))
            continue;
        if (Existing && Existing->GetSkeleton() != Mesh->GetSkeleton())
            return 1;
        const int32 FPS = Clip->GetIntegerField(TEXT("fps")), Frames = Clip->GetIntegerField(TEXT("frames"));
        if (FPS < 1 || FPS > 120 || Frames < 1 || Frames > 36000)
            return 1;
        // Validate all samples before creating an asset or changing its data model.
        TSet<FName> Names;
        for (const auto &TrackValue : Clip->GetArrayField(TEXT("tracks")))
        {
            auto Track = TrackValue->AsObject();
            const FName Bone(*Track->GetStringField(TEXT("bone")));
            if (Names.Contains(Bone) || Mesh->GetRefSkeleton().FindBoneIndex(Bone) == INDEX_NONE)
                return 1;
            Names.Add(Bone);
            const auto &Keys = Track->GetArrayField(TEXT("keys"));
            if (Keys.Num() != Frames + 1)
                return 1;
            for (const auto &Key : Keys)
            {
                if (Key->AsArray().Num() != 10)
                    return 1;
                for (const auto &V : Key->AsArray())
                    if (!FMath::IsFinite(V->AsNumber()))
                        return 1;
            }
        }
        auto *Sequence = Existing ? Existing : NewObject<UAnimSequence>(CreatePackage(*Package), *Name, RF_Public | RF_Standalone);
        if (Existing)
            Sequence->ResetAnimation();
        Sequence->SetSkeleton(Mesh->GetSkeleton());
        Sequence->SetPreviewMesh(Mesh);
        Sequence->bEnableRootMotion = false;
        Sequence->bLoop = Clip->GetBoolField(TEXT("loop"));
        auto &Controller = Sequence->GetController();
        Controller.InitializeModel();
        Controller.OpenBracket(FText::FromString(TEXT("Import DAoC KFA")), false);
        Controller.SetFrameRate(FFrameRate(FPS, 1), false);
        Controller.SetNumberOfFrames(FFrameNumber(Frames), false);
        for (const auto &TrackValue : Clip->GetArrayField(TEXT("tracks")))
        {
            auto Track = TrackValue->AsObject();
            const FName Bone(*Track->GetStringField(TEXT("bone")));
            TArray<FVector3f> Positions, Scales;
            TArray<FQuat4f> Rotations;
            for (const auto &Key : Track->GetArrayField(TEXT("keys")))
            {
                const auto &K = Key->AsArray();
                Positions.Emplace(K[0]->AsNumber(), K[1]->AsNumber(), K[2]->AsNumber());
                FQuat4f Q(K[3]->AsNumber(), K[4]->AsNumber(), K[5]->AsNumber(), K[6]->AsNumber());
                if (Q.SizeSquared() < 0.99f || Q.SizeSquared() > 1.01f)
                    return 1;
                Q.Normalize();
                Rotations.Add(Q);
                Scales.Emplace(K[7]->AsNumber(), K[8]->AsNumber(), K[9]->AsNumber());
            }
            if (!Controller.AddBoneCurve(Bone, false) || !Controller.SetBoneTrackKeys(Bone, Positions, Rotations, Scales, false))
                return 1;
        }
        Controller.NotifyPopulated();
        Controller.CloseBracket(false);
        Sequence->PostEditChange();
        FAssetRegistryModule::AssetCreated(Sequence);
        Sequence->MarkPackageDirty();
        FSavePackageArgs Save;
        Save.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Sequence->GetOutermost(), Sequence, *FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()), Save))
            return 1;
        UE_LOG(LogTemp, Display, TEXT("DAoC animation %s: %d tracks, %.3f seconds"), *Name, Names.Num(), double(Frames) / FPS);
    }
    return 0;
#endif
}
bool UDaocAnimationsCommandlet::ImportManifest(const FString &File, bool bReimport)
{
    if (File.Contains(TEXT("\"")))
        return false;
    auto *Commandlet = NewObject<UDaocAnimationsCommandlet>();
    return Commandlet->Main(FString::Printf(TEXT("-Input=\"%s\" %s"), *File, bReimport ? TEXT("-Reimport") : TEXT(""))) == 0;
}
