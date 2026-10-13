#include "DaocEffectsCommandlet.h"
#include "DaocEffectCatalog.h"
#include "NiagaraSystem.h"
#if WITH_EDITOR
#include "DaocObjectsCommandlet.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystemFactoryNew.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_ApplyOwnerScaleToAttributes.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleColor.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_SolveVelocitiesAndForces.h"
#include "Stateless/Modules/NiagaraStatelessModule_SpriteRotationRate.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
bool SaveEffectAsset(UObject *Asset)
{
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Asset->MarkPackageDirty();
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
}
template <typename T>
T *EffectAsset(const FString &Name)
{
    const FString Package = TEXT("/Game/DAoC/Effects/") + Name;
    if (auto *Found = LoadObject<T>(nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn))
        return Found;
    return NewObject<T>(CreatePackage(*Package), *Name, RF_Public | RF_Standalone);
}
float Number(const TSharedPtr<FJsonObject> &J, const TCHAR *Key, float Fallback = 0)
{
    double V = Fallback;
    J->TryGetNumberField(Key, V);
    return FMath::IsFinite(V) ? float(V) : Fallback;
}
FVector3f Vector(const TSharedPtr<FJsonObject> &J, const TCHAR *Key)
{
    const auto &A = J->GetArrayField(Key);
    return FVector3f(A[0]->AsNumber(), -A[1]->AsNumber(), A[2]->AsNumber());
}
FLinearColor Color(const TArray<TSharedPtr<FJsonValue>> &A)
{
    return FLinearColor(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber());
}
template <typename T>
void SetStruct(UObject *Object, FName Name, const T &Value)
{
    auto *Property = FindFProperty<FStructProperty>(Object->GetClass(), Name);
    check(Property && Property->ElementSize == sizeof(T));
    Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Object), &Value);
}
void SetBool(UObject *Object, FName Name, bool Value)
{
    auto *Property = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
    check(Property);
    Property->SetPropertyValue_InContainer(Object, Value);
}
UMaterial *SpriteMaterial(UTexture2D *Texture, bool Additive)
{
    const FString Name = TEXT("M_") + Texture->GetName() + (Additive ? TEXT("_Add") : TEXT("_Alpha"));
    auto *M = EffectAsset<UMaterial>(Name);
    if (!M->GetExpressionCollection().Expressions.IsEmpty())
        return M;
    M->BlendMode = Additive ? BLEND_Additive : BLEND_Translucent;
    M->SetShadingModel(MSM_Unlit);
    M->TwoSided = true;
    M->SetMaterialUsage(MATUSAGE_NiagaraSprites);
    auto *T = NewObject<UMaterialExpressionTextureSample>(M);
    T->Texture = Texture;
    T->SamplerType = SAMPLERTYPE_Color;
    auto *P = NewObject<UMaterialExpressionParticleColor>(M);
    auto *RGB = NewObject<UMaterialExpressionMultiply>(M);
    RGB->A.Connect(0, T);
    RGB->B.Connect(0, P);
    auto *Alpha = NewObject<UMaterialExpressionMultiply>(M);
    Alpha->A.Connect(4, T);
    Alpha->B.Connect(4, P);
    for (auto *Expression : TArray<UMaterialExpression *>{T, P, RGB, Alpha})
        M->GetExpressionCollection().AddExpression(Expression);
    M->GetEditorOnlyData()->EmissiveColor.Connect(0, RGB);
    M->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
    M->PostEditChange();
    return SaveEffectAsset(M) ? M : nullptr;
}
bool ConfigureEmitter(UNiagaraStatelessEmitter *Emitter, const TSharedPtr<FJsonObject> &J, UMaterial *Material)
{
    if (auto *Mask = FindFProperty<FUInt32Property>(Emitter->GetClass(), TEXT("AllowedFeatureMask")))
        Mask->SetPropertyValue_InContainer(Emitter, uint32(ENiagaraStatelessFeatureMask::ExecuteGPU));
    constexpr float Scale = 2.54f;
    const float Life = FMath::Clamp(Number(J, TEXT("life")), .05f, 10.f);
    const float Variance = FMath::Max(0.f, Number(J, TEXT("lifeRandom")));
    const float LifeMax = FMath::Clamp(Life + Variance, .05f, 10.f);
    const float Rate = FMath::Clamp(Number(J, TEXT("rate")), 0.f, FMath::Max(1.f, Number(J, TEXT("capacity"))) / LifeMax);
    if (Rate <= 0)
        return false;
    for (const auto &Module : Emitter->GetModules())
        SetBool(Module.Get(), TEXT("bModuleEnabled"), false);
    const auto Enable = [&](UClass *Class) { auto *M = Emitter->GetModule(Class); if (M) SetBool(M, TEXT("bModuleEnabled"), true); return M; };
    Enable(UNiagaraStatelessModule_ApplyOwnerScaleToAttributes::StaticClass());
    auto *Initialize = Cast<UNiagaraStatelessModule_InitializeParticle>(Enable(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
    auto *Velocity = Cast<UNiagaraStatelessModule_AddVelocity>(Enable(UNiagaraStatelessModule_AddVelocity::StaticClass()));
    auto *Shape = Cast<UNiagaraStatelessModule_ShapeLocation>(Enable(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
    auto *Size = Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Enable(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass()));
    auto *Colors = Cast<UNiagaraStatelessModule_ScaleColor>(Enable(UNiagaraStatelessModule_ScaleColor::StaticClass()));
    if (!Initialize || !Velocity || !Shape || !Size || !Colors || !Enable(UNiagaraStatelessModule_SolveVelocitiesAndForces::StaticClass()))
    {
        UE_LOG(LogTemp, Error, TEXT("Lightweight template lacks required effect modules: initialize %d velocity %d shape %d size %d color %d"), Initialize != nullptr, Velocity != nullptr, Shape != nullptr, Size != nullptr, Colors != nullptr);
        return false;
    }
    Initialize->LifetimeDistribution = FNiagaraDistributionRangeFloat(Life, LifeMax);
    Initialize->SpriteSizeDistribution.InitConstant(FVector2f(FMath::Max(.01f, Number(J, TEXT("radius"))) * 2 * Scale));
    const auto MaterialColor = Color(J->GetArrayField(TEXT("color")));
    Initialize->ColorDistribution = FNiagaraDistributionColor(MaterialColor * Color(J->GetArrayField(TEXT("initialColor"))));
    Colors->ScaleDistribution.InitConstant(FLinearColor::White);
    Shape->ShapePrimitive = ENSM_ShapePrimitive::Box;
    Shape->BoxSize.InitConstant(Vector(J, TEXT("box")).GetAbs() * Scale);
    Shape->TransformOffset = FNiagaraDistributionPosition(Vector(J, TEXT("emitterOffset")) * Scale);
    Velocity->VelocityType = ENSM_VelocityType::InCone;
    const float Speed = FMath::Max(0.f, Number(J, TEXT("speed"))) * Scale;
    const float SpeedRandom = FMath::Max(0.f, Number(J, TEXT("speedRandom"))) * Scale * .5f;
    Velocity->ConeVelocityDistribution = FNiagaraDistributionRangeFloat(FMath::Max(0.f, Speed - SpeedRandom), Speed + SpeedRandom);
    Velocity->ConeRotationType = ENSM_ConeRotationType::Direction;
    Velocity->ConeVelocityScale.InitConstant(1);
    Velocity->InnerCone = 0;
    Velocity->bSpeedFalloffFromConeAxisEnabled = false;
    const float Theta = Number(J, TEXT("verticalDirection")), Phi = Number(J, TEXT("horizontalDirection"));
    Velocity->ConeDirection.InitConstant(FVector3f(FMath::Sin(Theta) * FMath::Cos(Phi), -FMath::Sin(Theta) * FMath::Sin(Phi), FMath::Cos(Theta)));
    Velocity->ConeAngle = FMath::Clamp(2 * FMath::RadiansToDegrees(Number(J, TEXT("verticalAngle"))), 0.f, 360.f);
    float Grow = 0, Fade = 0;
    FVector3f Gravity = FVector3f::ZeroVector;
    for (const auto &Value : J->GetArrayField(TEXT("modifiers")))
    {
        const auto M = Value->AsObject();
        const FString Type = M->GetStringField(TEXT("type"));
        if (Type == TEXT("growFade"))
        {
            Grow = Number(M, TEXT("grow"));
            Fade = Number(M, TEXT("fade"));
        }
        if (Type == TEXT("gravity"))
            Gravity += Vector(M, TEXT("direction")).GetSafeNormal() * Number(M, TEXT("force")) * Scale;
        if (Type == TEXT("rotation"))
            if (auto *Rotation = Cast<UNiagaraStatelessModule_SpriteRotationRate>(Enable(UNiagaraStatelessModule_SpriteRotationRate::StaticClass())))
            {
                Rotation->RotationRateDistribution = FNiagaraDistributionRangeFloat(FMath::RadiansToDegrees(Number(M, TEXT("speed"))));
                Rotation->bUseRateScale = false;
            }
        if (Type == TEXT("color"))
        {
            // The legacy color modifier replaces initial particle color rather than multiplying it.
            Initialize->ColorDistribution = FNiagaraDistributionColor(MaterialColor);
            Colors->ScaleDistribution.Mode = ENiagaraDistributionMode::NonUniformCurve;
            Colors->ScaleDistribution.ChannelCurves.SetNum(4);
            for (const auto &K : M->GetArrayField(TEXT("keys")))
            {
                const auto &Key = K->AsArray();
                const auto &Channels = Key[1]->AsArray();
                for (int32 C = 0; C < 4; ++C)
                    Colors->ScaleDistribution.ChannelCurves[C].AddKey(Key[0]->AsNumber(), Channels[C]->AsNumber());
            }
            Colors->ScaleDistribution.UpdateValuesFromDistribution();
        }
    }
    if (auto *G = Cast<UNiagaraStatelessModule_GravityForce>(Enable(UNiagaraStatelessModule_GravityForce::StaticClass())))
        G->GravityDistribution.InitConstant(Gravity);
    Size->ScaleDistribution.Mode = ENiagaraDistributionMode::UniformCurve;
    Size->ScaleDistribution.ChannelCurves.SetNum(1);
    for (int32 I = 0; I <= 32; ++I)
    {
        const float Age = float(I) / 32;
        const float S = (Grow > 0 ? FMath::Min(1.f, Age * Life / Grow) : 1.f) * (Fade > 0 ? FMath::Min(1.f, (1 - Age) * Life / Fade) : 1.f);
        Size->ScaleDistribution.ChannelCurves[0].AddKey(Age, S);
    }
    Size->ScaleDistribution.UpdateValuesFromDistribution();
    const float Start = FMath::Clamp(Number(J, TEXT("start")), 0.f, 10.f);
    const float Stop = FMath::Clamp(Number(J, TEXT("stop")), Start + .01f, 10.f);
    FNiagaraEmitterStateData State;
    State.LoopBehavior = ENiagaraLoopBehavior::Once;
    State.LoopDuration = FNiagaraDistributionRangeFloat(Stop - Start);
    State.bLoopDelayEnabled = Start > 0;
    State.LoopDelay = FNiagaraDistributionRangeFloat(Start);
    SetStruct(Emitter, TEXT("EmitterState"), State);
    while (Emitter->GetNumSpawnInfos())
    {
        auto Id = Emitter->GetSpawnInfoByIndex(0)->SourceId;
        Emitter->RemoveSpawnInfoBySourceId(Id);
    }
    auto &Spawn = Emitter->AddSpawnInfo();
    Spawn.Type = ENiagaraStatelessSpawnInfoType::Rate;
    Spawn.Rate = FNiagaraDistributionRangeFloat(Rate);
    const float Radius = Vector(J, TEXT("box")).GetAbsMax() * Scale + Vector(J, TEXT("emitterOffset")).Size() * Scale + (Speed + SpeedRandom) * LifeMax + .5f * Gravity.Size() * LifeMax * LifeMax + Number(J, TEXT("radius")) * 2 * Scale;
    SetStruct(Emitter, TEXT("FixedBounds"), FBox(FVector(-Radius), FVector(Radius)));
    for (auto *Renderer : Emitter->GetRenderers())
        if (auto *Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
        {
            Sprite->Material = Material;
            Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
            Sprite->Alignment = ENiagaraSpriteAlignment::Unaligned;
            Sprite->SortMode = Material->BlendMode == BLEND_Additive ? ENiagaraSortMode::None : ENiagaraSortMode::ViewDepth;
            Sprite->bEnableCameraDistanceCulling = true;
            Sprite->MaxCameraDistance = 7000;
        }
    Emitter->PostEditChange();
    return true;
}
} // namespace
#endif

UDaocEffectsCommandlet::UDaocEffectsCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}
int32 UDaocEffectsCommandlet::Main(const FString &Params)
{
#if WITH_EDITOR
    FString Input, Text;
    FParse::Value(*Params, TEXT("Input="), Input);
    TSharedPtr<FJsonObject> Manifest;
    if (!FFileHelper::LoadFileToString(Text, *Input) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest))
        return 1;
    auto *Template = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight"));
    if (!Template || Template->GetEmitterHandles().IsEmpty() || !Template->GetEmitterHandles()[0].GetStatelessEmitter())
        return 1;
    auto *Catalog = EffectAsset<UDaocEffectCatalog>(TEXT("DA_Effects"));
    Catalog->Effects.Reset();
    Catalog->Spells.Reset();
    TMap<FString, UNiagaraSystem *> Systems;
    const FString Directory = FPaths::GetPath(Input);
    for (const auto &Value : Manifest->GetArrayField(TEXT("systems")))
    {
        const auto J = Value->AsObject();
        const FString Name = J->GetStringField(TEXT("asset"));
        auto *System = EffectAsset<UNiagaraSystem>(Name);
        TSet<FGuid> OldHandles;
        for (const auto &Handle : System->GetEmitterHandles())
            OldHandles.Add(Handle.GetId());
        if (!OldHandles.IsEmpty())
            System->RemoveEmitterHandlesById(OldHandles);
        UNiagaraSystemFactoryNew::InitializeSystem(System, true);
        int32 Index = 0;
        for (const auto &E : J->GetArrayField(TEXT("emitters")))
        {
            const auto Recipe = E->AsObject();
            auto *Texture = UDaocObjectsCommandlet::ImportTextureAsset(Directory, Recipe->GetStringField(TEXT("texturePng")));
            auto *Material = Texture ? SpriteMaterial(Texture, Recipe->GetBoolField(TEXT("additive"))) : nullptr;
            if (!Material)
                return 1;
            auto *Emitter = DuplicateObject<UNiagaraStatelessEmitter>(Template->GetEmitterHandles()[0].GetStatelessEmitter(), System, *FString::Printf(TEXT("Particles_%d"), Index++));
            Emitter->SetUniqueEmitterName(Emitter->GetName());
            if (!ConfigureEmitter(Emitter, Recipe, Material))
                return 1;
            FNiagaraEmitterHandle Handle(*Emitter);
            System->AddEmitterHandleDirect(Handle);
        }
        System->bFixedBounds = false;
        System->RequestCompile(true);
        System->WaitForCompilationComplete();
        if (!System->IsReadyToRun() || !SaveEffectAsset(System))
        {
            UE_LOG(LogTemp, Error, TEXT("Prepared effect %s could not compile or save; ready=%d emitters=%d"), *Name, System->IsReadyToRun(), System->GetEmitterHandles().Num());
            return 1;
        }
        Systems.Add(Name, System);
    }
    for (const auto &Value : Manifest->GetArrayField(TEXT("bindings")))
    {
        const auto J = Value->AsObject();
        FDaocVisualEffect E;
        E.System = Systems.FindRef(J->GetStringField(TEXT("asset")));
        E.Socket = FName(*J->GetStringField(TEXT("socket")));
        E.Duration = FMath::Clamp(Number(J, TEXT("duration"), 1), .01f, 60.f);
        E.Tail = FMath::Clamp(Number(J, TEXT("tail"), 1), 0.f, 10.f);
        E.PlaybackRate = FMath::Clamp(Number(J, TEXT("playbackRate"), 1), .1f, 4.f);
        E.ParticleCost = FMath::Max(1, int32(Number(J, TEXT("cost"))));
        E.bAutoScale = Number(J, TEXT("autoScale")) != 0;
        Catalog->Effects.Add(int32(Number(J, TEXT("id"))), E);
    }
    for (const auto &Value : Manifest->GetArrayField(TEXT("spells")))
    {
        const auto J = Value->AsObject();
        FDaocSpellVisuals S;
        const auto Read = [&](const TCHAR *Key, TArray<int32> &Out) { for (const auto &Id : J->GetArrayField(Key)) if (Catalog->Effects.Contains(int32(Id->AsNumber()))) Out.AddUnique(int32(Id->AsNumber())); };
        Read(TEXT("gather"), S.Gather);
        Read(TEXT("release"), S.Release);
        Read(TEXT("projectile"), S.Projectile);
        Read(TEXT("hit"), S.Hit);
        Read(TEXT("miss"), S.Miss);
        Catalog->Spells.Add(int32(Number(J, TEXT("id"))), S);
    }
    if (!SaveEffectAsset(Catalog))
        return 1;
    UE_LOG(LogTemp, Display, TEXT("DAoC effects: %d lightweight systems, %d bindings, %d spell mappings"), Systems.Num(), Catalog->Effects.Num(), Catalog->Spells.Num());
    return 0;
#else
    return 1;
#endif
}
bool UDaocEffectsCommandlet::ImportManifest(const FString &Path)
{
    return !Path.Contains(TEXT("\"")) && NewObject<UDaocEffectsCommandlet>()->Main(FString::Printf(TEXT("-Input=\"%s\""), *Path)) == 0;
}
