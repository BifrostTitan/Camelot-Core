#include "DaocWeatherAssetBuilder.h"
#if WITH_EDITOR
#include "DaocWeatherAssets.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraScript.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Misc/Paths.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
namespace
{
const FString Root = TEXT("/Game/DAoC/Weather");
bool Save(UObject *Object)
{
    if (!Object)
        return false;
    Object->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    const FString File = FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    return UPackage::SavePackage(Object->GetOutermost(), Object, *File, Args);
}
template <typename T>
T *Existing(const FString &Name)
{
    const FString Path = Root / Name;
    if (!FPackageName::DoesPackageExist(Path))
        return nullptr;
    return LoadObject<T>(nullptr, *(Path + TEXT(".") + Name));
}
template <typename T>
T *Create(const FString &Name)
{
    auto *Object = NewObject<T>(CreatePackage(*(Root / Name)), *Name, RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Object);
    return Object;
}
template <typename T>
T *Expression(UMaterial *Material)
{
    auto *Result = NewObject<T>(Material);
    Material->GetExpressionCollection().AddExpression(Result);
    return Result;
}
UMaterial *MakeMaterial(bool Snow, UMaterialInterface *Source)
{
    const FString Name = Snow ? TEXT("M_Snowflakes") : TEXT("M_Rain");
    if (auto *Found = Existing<UMaterial>(Name))
        return Found;
    auto *M = Snow ? Create<UMaterial>(Name) : DuplicateObject<UMaterial>(Source->GetMaterial(), CreatePackage(*(Root / Name)), *Name);
    if (!Snow)
    {
        M->SetFlags(RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(M);
        // Preserve the rain pack instance's authored look on the new fade-aware parent.
        for (UMaterialExpression *E : M->GetExpressionCollection().Expressions)
        {
            if (auto *Scalar = Cast<UMaterialExpressionScalarParameter>(E))
            {
                float Value;
                if (Source->GetScalarParameterValue(FMaterialParameterInfo(Scalar->ParameterName), Value))
                    Scalar->DefaultValue = Value;
            }
            if (auto *Vector = Cast<UMaterialExpressionVectorParameter>(E))
            {
                FLinearColor Value;
                if (Source->GetVectorParameterValue(FMaterialParameterInfo(Vector->ParameterName), Value))
                    Vector->DefaultValue = Value;
            }
        }
    }
    auto *Intensity = Expression<UMaterialExpressionScalarParameter>(M);
    Intensity->ParameterName = TEXT("WeatherIntensity");
    Intensity->DefaultValue = 1.f;
    auto *Multiply = Expression<UMaterialExpressionMultiply>(M);
    Multiply->B.Connect(0, Intensity);
    if (Snow)
    {
        M->BlendMode = BLEND_Translucent;
        M->SetShadingModel(MSM_Unlit);
        M->TwoSided = true;
        auto *UV = Expression<UMaterialExpressionTextureCoordinate>(M);
        auto *Shape = Expression<UMaterialExpressionCustom>(M);
        Shape->Code = TEXT("float2 p=UV-0.5; return saturate((0.25-dot(p,p))*16.0);");
        Shape->OutputType = CMOT_Float1;
        FCustomInput Input;
        Input.InputName = TEXT("UV");
        Input.Input.Connect(0, UV);
        Shape->Inputs.Reset();
        Shape->Inputs.Add(Input);
        auto *Fade = Expression<UMaterialExpressionDepthFade>(M);
        Fade->FadeDistanceDefault = 35.f;
        Fade->InOpacity.Connect(0, Shape);
        Multiply->A.Connect(0, Fade);
        auto *Color = Expression<UMaterialExpressionConstant3Vector>(M);
        Color->Constant = FLinearColor(.85f, .9f, 1.f);
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
    }
    else
        Multiply->A = M->GetEditorOnlyData()->Opacity;
    M->GetEditorOnlyData()->Opacity.Connect(0, Multiply);
    M->SetMaterialUsage(MATUSAGE_NiagaraSprites);
    M->PostEditChange();
    return Save(M) ? M : nullptr;
}
// Preserve source graphs, but expose spawn rate so intensity actually controls particle count.
bool BindSpawnRate(UNiagaraSystem *System, FVersionedNiagaraEmitterData *Data)
{
    const FNiagaraVariable Rate(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.WeatherSpawnRate"));
    System->GetExposedParameters().SetParameterValue(200.f, Rate, true);
    struct FBindingTarget
    {
        TWeakObjectPtr<UNiagaraNodeFunctionCall> Node;
        TWeakObjectPtr<UNiagaraScriptSource> Source;
    };
    TArray<FBindingTarget> Targets;
    TSet<UNiagaraNodeFunctionCall *> Seen;
    TArray<UNiagaraScript *> Scripts;
    Data->GetScripts(Scripts);
    // Collect first: the override/link helpers add nodes and can reallocate Graph.Nodes.
    // Editing inside a range-for over that live array invalidates its iterator.
    for (auto *Script : Scripts)
    {
        auto *Source = Script ? Cast<UNiagaraScriptSource>(Script->GetLatestSource()) : nullptr;
        if (!Source || !Source->NodeGraph)
            continue;
        for (UEdGraphNode *Base : Source->NodeGraph->Nodes)
        {
            auto *Node = Cast<UNiagaraNodeFunctionCall>(Base);
            if (!Node || !Node->FunctionScript || !Node->FunctionScript->GetPathName().Contains(TEXT("/SpawnRate.SpawnRate")) || Seen.Contains(Node))
                continue;
            Seen.Add(Node);
            Targets.Add({TWeakObjectPtr<UNiagaraNodeFunctionCall>(Node), TWeakObjectPtr<UNiagaraScriptSource>(Source)});
        }
    }
    bool Bound = false;
    for (const auto &Target : Targets)
    {
        auto *Node = Target.Node.Get();
        auto *Source = Target.Source.Get();
        if (!IsValid(Node) || !IsValid(Source))
            continue;
        const auto Handle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(TEXT("Module.SpawnRate"), FName(*Node->GetFunctionName()));
        auto &Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Node, Handle, Rate.GetType(), FGuid(), FGuid());
        FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, Rate, TSet<FNiagaraVariableBase>{Rate});
        // The final editor subclass does not export this method; dispatch through its runtime base.
        static_cast<UNiagaraScriptSourceBase *>(Source)->MarkNotSynchronized(TEXT("DAoC weather spawn rate binding"));
        Bound = true;
    }
    return Bound;
}
void TuneEmitter(FVersionedNiagaraEmitterData *Data, bool Snow)
{
    Data->bLocalSpace = false;
    Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Dynamic;
    TArray<UNiagaraScript *> Scripts;
    Data->GetScripts(Scripts);
    for (auto *Script : Scripts)
    {
        if (!Script)
            continue;
        TArray<FNiagaraVariable> Parameters;
        Script->RapidIterationParameters.GetParameters(Parameters);
        for (const auto &P : Parameters)
        {
            const FString N = P.GetName().ToString();
            if (P.GetType() == FNiagaraTypeDefinition::GetFloatDef())
            {
                float Value = 0;
                bool Change = true;
                if (N.EndsWith(TEXT(".Lifetime Min")))
                    Value = Snow ? 6.f : 1.f;
                else if (N.EndsWith(TEXT(".Lifetime Max")))
                    Value = Snow ? 9.f : 2.f;
                else if (N.EndsWith(TEXT(".Sphere Radius")))
                    Value = 1100.f;
                else if (N.EndsWith(TEXT(".Spawn Probability")))
                    Value = 1.f;
                else if (N.EndsWith(TEXT(".Drag.Drag")))
                    Value = Snow ? .3f : 0.f;
                else
                    Change = false;
                if (Change)
                    Script->RapidIterationParameters.SetParameterValue(Value, P);
            }
            else if (P.GetType() == FNiagaraTypeDefinition::GetVec3Def())
            {
                FVector3f Value = FVector3f::ZeroVector;
                bool Change = true;
                if (N.EndsWith(TEXT(".GravityForce.Gravity")))
                    Value = FVector3f(0, 0, Snow ? -15.f : -980.f);
                else if (N.EndsWith(TEXT(".AddVelocity.Velocity")))
                    Value = FVector3f(Snow ? 20.f : 0.f, 0, Snow ? -65.f : -1400.f);
                else if (N.EndsWith(TEXT(".Position Offset")) || N.EndsWith(TEXT(".SphereLocation.Offset")))
                    Value = FVector3f::ZeroVector;
                else
                    Change = false;
                if (Change)
                    Script->RapidIterationParameters.SetParameterValue(Value, P);
            }
            else if (P.GetType() == FNiagaraTypeDefinition::GetVec2Def() && N.EndsWith(TEXT(".Sprite Size")))
                Script->RapidIterationParameters.SetParameterValue(Snow ? FVector2f(2.5f, 2.5f) : FVector2f(1.f, 35.f), P);
        }
    }
}
UNiagaraSystem *MakeSystem(const TCHAR *Name, UNiagaraEmitter *Source, UMaterial *Material, bool Snow, FString &Error)
{
    if (auto *Found = Existing<UNiagaraSystem>(Name))
        return Found;
    auto *System = Create<UNiagaraSystem>(Name);
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Source, Source->GetExposedVersion().VersionGuid);
    bool Bound = false;
    for (auto &Handle : System->GetEmitterHandles())
    {
        auto *Data = Handle.GetEmitterData();
        if (!Data)
            continue;
        TuneEmitter(Data, Snow);
        Bound |= BindSpawnRate(System, Data);
        const FNiagaraVariable Binding(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()), TEXT("User.WeatherMaterial"));
        System->GetExposedParameters().AddParameter(Binding);
        System->GetExposedParameters().SetUObject(Material, Binding);
        for (auto *Renderer : Data->GetRenderers())
            if (auto *Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
            {
                Sprite->Material = Material;
                Sprite->MaterialUserParamBinding.Parameter = Binding;
                if (Snow)
                {
                    Sprite->Alignment = ENiagaraSpriteAlignment::Unaligned;
                    Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
                }
                Sprite->bEnableCameraDistanceCulling = false;
            }
    }
    if (!Bound)
    {
        Error = TEXT("Could not bind the Tropical SpawnRate module; no weather system was saved.");
        return nullptr;
    }
    System->bFixedBounds = true;
    System->SetFixedBounds(FBox(FVector(-3000), FVector(3000)));
    System->RequestCompile(true);
    System->WaitForCompilationComplete();
    if (!System->IsReadyToRun())
    {
        Error = TEXT("Weather Niagara compilation failed. See Output Log.");
        return nullptr;
    }
    if (!Save(System))
    {
        Error = TEXT("Could not save weather particle system.");
        return nullptr;
    }
    return System;
}
} // namespace
UDaocWeatherAssets *DaocPrepareWeatherAssets(UNiagaraEmitter *RainEmitter, UMaterialParameterCollection *Wetness, UMaterialFunctionInterface *SnowBuildup, FString &Error)
{
    if (!RainEmitter || !Wetness || !SnowBuildup)
    {
        Error = TEXT("The migrated Tropical weather dependencies are missing.");
        return nullptr;
    }
    if (auto *ExistingLibrary = Existing<UDaocWeatherAssets>(TEXT("DA_WeatherAssets")))
        if (ExistingLibrary->Rain && ExistingLibrary->Snow && ExistingLibrary->RainMaterial && ExistingLibrary->SnowMaterial && ExistingLibrary->Wetness)
            return ExistingLibrary;
    auto *Data = RainEmitter->GetLatestEmitterData();
    UMaterialInterface *RainSource = nullptr;
    if (Data)
        for (auto *Renderer : Data->GetRenderers())
            if (auto *Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
                if (Sprite->Material)
                {
                    RainSource = Sprite->Material.Get();
                    break;
                }
    if (!RainSource)
    {
        Error = TEXT("Tropical rain emitter has no sprite material.");
        return nullptr;
    }
    auto *RainMaterial = MakeMaterial(false, RainSource);
    auto *SnowMaterial = MakeMaterial(true, nullptr);
    if (!RainMaterial || !SnowMaterial)
    {
        Error = TEXT("Could not save precipitation materials.");
        return nullptr;
    }
    auto *Rain = MakeSystem(TEXT("NS_Rain"), RainEmitter, RainMaterial, false, Error);
    if (!Rain)
        return nullptr;
    auto *Snow = MakeSystem(TEXT("NS_Snow"), RainEmitter, SnowMaterial, true, Error);
    if (!Snow)
        return nullptr;
    auto *Library = Existing<UDaocWeatherAssets>(TEXT("DA_WeatherAssets"));
    if (!Library)
        Library = Create<UDaocWeatherAssets>(TEXT("DA_WeatherAssets"));
    Library->Rain = Rain;
    Library->Snow = Snow;
    Library->RainMaterial = RainMaterial;
    Library->SnowMaterial = SnowMaterial;
    Library->Wetness = Wetness;
    Library->SnowBuildup = SnowBuildup;
    if (!Save(Library))
    {
        Error = TEXT("Could not save weather asset library.");
        return nullptr;
    }
    return Library;
}
#endif
