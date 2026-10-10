#include "DaocWeatherSubsystem.h"
#include "DaocWeatherAssets.h"
#include "DaocNetworkSubsystem.h"
#include "DaocClientAssetCatalog.h"
#include "DaocZoneActor.h"
#include "DaocZoneProfile.h"
#include "NiagaraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollectionInstance.h"

bool UDaocWeatherSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}
TStatId UDaocWeatherSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UDaocWeatherSubsystem, STATGROUP_Tickables);
}
void UDaocWeatherSubsystem::SetWeatherOverride(EDaocWeatherType Type, float Intensity)
{
    bOverride = true;
    OverrideType = Type;
    OverrideIntensity = FMath::Clamp(Intensity, 0.f, 1.f);
}
void UDaocWeatherSubsystem::ClearWeatherOverride()
{
    bOverride = false;
}
void UDaocWeatherSubsystem::Deinitialize()
{
    ResetWeather();
    if (Host)
        Host->Destroy();
    Host = nullptr;
    Assets = nullptr;
    Super::Deinitialize();
}
void UDaocWeatherSubsystem::RestoreScene()
{
    for (const auto &Pair : OriginalSunIntensity)
        if (auto *Light = Pair.Key.Get())
            Light->SetIntensity(Pair.Value);
    OriginalSunIntensity.Reset();
    if (auto *F = Fog.Get())
    {
        if (bOwnFog)
            F->SetVisibility(false);
        else
            F->SetFogDensity(OriginalFogDensity);
    }
    Fog.Reset();
    bOwnFog = false;
}
void UDaocWeatherSubsystem::ResetWeather()
{
    if (RainParticles && RainParticles->IsActive())
        RainParticles->DeactivateImmediate();
    if (SnowParticles && SnowParticles->IsActive())
        SnowParticles->DeactivateImmediate();
    UpdateCollection(0, 0, 0);
    RestoreScene();
    RainAmount = SnowAmount = CurrentIntensity = 0;
    bInsideServerStorm = false;
    CurrentType = EDaocWeatherType::Sunny;
    LastTarget = -1;
    Zone.Reset();
    LastWorldRevision = INDEX_NONE;
    NextShelterCheck = 0;
    bSheltered = false;
}
bool UDaocWeatherSubsystem::EnsureAssets(UDaocNetworkSubsystem *Network)
{
    if (Assets)
        return true;
    if (bAssetLoadAttempted)
        return false;
    bAssetLoadAttempted = true;
    Assets = Network->ClientAssetCatalog ? Network->ClientAssetCatalog->WeatherAssets.Get() : nullptr;
    if (!Assets)
        Assets = LoadObject<UDaocWeatherAssets>(nullptr, TEXT("/Game/DAoC/Weather/DA_WeatherAssets.DA_WeatherAssets"), nullptr, LOAD_NoWarn);
    if (!Assets || !Assets->Rain || !Assets->Snow)
    {
        UE_LOG(LogTemp, Warning, TEXT("DAoC weather: prepare the weather assets in Window > DAoC Zone Toolbox before testing precipitation."));
        Assets = nullptr;
        return false;
    }
    FActorSpawnParameters Params;
    Params.ObjectFlags |= RF_Transient;
    Host = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
    if (!Host)
    {
        Assets = nullptr;
        return false;
    }
    auto *Root = NewObject<USceneComponent>(Host, TEXT("WeatherRoot"));
    Root->SetMobility(EComponentMobility::Movable);
    Host->SetRootComponent(Root);
    Host->AddInstanceComponent(Root);
    Root->RegisterComponent();
    auto Create = [&](const TCHAR *Name, UNiagaraSystem *System) {
        auto *Component = NewObject<UNiagaraComponent>(Host, Name);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetAutoActivate(false);
        Component->SetupAttachment(Root);
        Component->SetAsset(System);
        Component->SetCastShadow(false);
        Host->AddInstanceComponent(Component);
        Component->RegisterComponent();
        return Component;
    };
    RainParticles = Create(TEXT("Rain"), Assets->Rain);
    SnowParticles = Create(TEXT("Snow"), Assets->Snow);
    if (Assets->RainMaterial)
        RainMaterial = UMaterialInstanceDynamic::Create(Assets->RainMaterial, Host);
    if (Assets->SnowMaterial)
        SnowMaterial = UMaterialInstanceDynamic::Create(Assets->SnowMaterial, Host);
    return true;
}
void UDaocWeatherSubsystem::UpdateParticles(UNiagaraComponent *Component, UMaterialInstanceDynamic *Material, float Amount, float Rate, FVector Anchor)
{
    if (!Component)
        return;
    Component->SetWorldLocation(Anchor);
    Component->SetVariableFloat(TEXT("User.WeatherSpawnRate"), Rate * Amount);
    if (Material)
    {
        Material->SetScalarParameterValue(TEXT("WeatherIntensity"), Amount);
        Component->SetVariableMaterial(TEXT("User.WeatherMaterial"), Material);
    }
    if (Amount > .005f)
    {
        if (!Component->IsActive())
            Component->Activate(true);
    }
    else if (Component->IsActive())
        Component->DeactivateImmediate();
}
void UDaocWeatherSubsystem::UpdateCollection(float Rain, float Snow, float SnowSurfaceAmount)
{
    if (!Assets || !Assets->Wetness || !GetWorld())
        return;
    auto *MPC = GetWorld()->GetParameterCollectionInstance(Assets->Wetness);
    if (!MPC)
        return;
    // Names match the source Tropical collection, including its original spelling.
    MPC->SetScalarParameterValue(TEXT("Darkening"), Rain * .2f);
    MPC->SetScalarParameterValue(TEXT("RoughnessWetmess"), Rain * .5f);
    MPC->SetScalarParameterValue(TEXT("DripsIntensity"), Rain);
    MPC->SetScalarParameterValue(TEXT("RippleIntesnity"), Rain);
    MPC->SetScalarParameterValue(TEXT("SplashIntensity"), Rain);
    MPC->SetScalarParameterValue(TEXT("Weather Snow : Amount"), Snow * FMath::Clamp(SnowSurfaceAmount, 0.f, 1.f));
    MPC->SetScalarParameterValue(TEXT("Sky : Overcast"), FMath::Max(Rain, Snow));
}
void UDaocWeatherSubsystem::UpdateScene(const FDaocZoneWeatherSettings &S, float Amount, int32 FogDiffusion)
{
    if (!S.bAffectLighting || Amount <= .001f)
    {
        RestoreScene();
        return;
    }
    if (OriginalSunIntensity.IsEmpty())
        for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
            if (auto *Light = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
                OriginalSunIntensity.Add(TWeakObjectPtr<UDirectionalLightComponent>(Light), Light->Intensity);
    for (const auto &Pair : OriginalSunIntensity)
        if (auto *Light = Pair.Key.Get())
            Light->SetIntensity(Pair.Value * FMath::Lerp(1.f, FMath::Clamp(S.StormSunMultiplier, 0.f, 1.f), Amount));
    if (!Fog.IsValid())
    {
        for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
        {
            Fog = It->GetComponent();
            break;
        }
        if (!Fog.IsValid() && Host)
        {
            // Reuse our component after clear weather rather than accumulating fog components.
            auto *Own = Host->FindComponentByClass<UExponentialHeightFogComponent>();
            if (!Own)
            {
                Own = NewObject<UExponentialHeightFogComponent>(Host, TEXT("WeatherFog"));
                Own->SetupAttachment(Host->GetRootComponent());
                Host->AddInstanceComponent(Own);
                Own->RegisterComponent();
            }
            Fog = Own;
            bOwnFog = true;
        }
        if (auto *F = Fog.Get())
        {
            OriginalFogDensity = bOwnFog ? 0.f : F->FogDensity;
            F->SetVisibility(true);
        }
    }
    // DOL diffusion is a distance: a shorter diffusion means denser fog.
    const float DiffusionScale = FogDiffusion > 0 ? FMath::Clamp(24000.f / FogDiffusion, .5f, 1.5f) : 1.f;
    if (auto *F = Fog.Get())
        F->SetFogDensity(OriginalFogDensity + FMath::Clamp(S.AddedFogDensity, 0.f, .1f) * Amount * DiffusionScale);
}
void UDaocWeatherSubsystem::Tick(float DeltaTime)
{
    if (!GetWorld() || !GetWorld()->HasBegunPlay())
        return;
    auto *Network = UDaocNetworkSubsystem::Get(this);
    if (!Network || !Network->bWorldReady)
    {
        if (LastWorldRevision != INDEX_NONE)
            ResetWeather();
        return; // Login stage and zone loading have no weather.
    }
    if (!Zone.IsValid() || LastWorldRevision != Network->WorldRevision)
    {
        ResetWeather();
        for (TActorIterator<ADaocZoneActor> It(GetWorld()); It; ++It)
            if (It->GetZoneId() == Network->ActiveZoneId)
            {
                Zone = *It;
                break;
            }
        LastWorldRevision = Network->WorldRevision;
    }
    auto *Z = Zone.Get();
    auto *P = Z ? Z->SelectedZoneProfile.Get() : nullptr;
    if (!P || !P->bHasTerrain)
        return;
    FDaocZoneWeatherSettings Settings = P->Weather;
    if (Settings.bUseZoneDefaults)
    {
        Settings.Policy = P->ZoneId == 0 ? EDaocWeatherPolicy::AlwaysSunny : P->ZoneId == 100 ? EDaocWeatherPolicy::Persistent
                                                                                              : EDaocWeatherPolicy::Server;
        Settings.PersistentType = EDaocWeatherType::Snow;
        Settings.ServerPrecipitation = P->ZoneId == 100 ? EDaocWeatherType::Snow : EDaocWeatherType::Rain;
    }
    auto Type = EDaocWeatherType::Sunny;
    float Target = 0;
    const auto W = Network->GetServerWeather();
    float PlayerX = Network->LastSubmittedPosition.X;
    if (auto *Pawn = Network->LocalPlayerPawn.Get())
        PlayerX = Network->WorldToServer(Pawn->GetActorLocation()).X;
    // Weather sweeps east across the entire region, rather than starting at every zone boundary.
    bInsideServerStorm = W.bKnown && W.Width > 0 && W.Intensity > 0 && PlayerX <= W.FrontX && PlayerX >= W.FrontX - W.Width;
    if (Settings.Policy == EDaocWeatherPolicy::Persistent)
    {
        Type = Settings.PersistentType;
        Target = Settings.Intensity;
        if (bInsideServerStorm)
            Target = FMath::Max(Target, W.Intensity);
    }
    else if (Settings.Policy == EDaocWeatherPolicy::Server || Settings.Policy == EDaocWeatherPolicy::ServerAndCycle)
    {
        if (bInsideServerStorm)
        {
            Type = Settings.ServerPrecipitation;
            Target = W.Intensity;
        }
        else if (Settings.Policy == EDaocWeatherPolicy::ServerAndCycle && !Settings.CyclePatterns.IsEmpty())
        {
            const int64 Slot = int64(FMath::FloorToDouble(double(FDateTime::UtcNow().ToUnixTimestamp()) / FMath::Max(30.f, Settings.CycleSeconds)));
            FRandomStream Random(HashCombine(GetTypeHash(P->ZoneId), HashCombine(GetTypeHash(Slot), GetTypeHash(Settings.CycleSeed))));
            Type = Settings.CyclePatterns[Random.RandRange(0, Settings.CyclePatterns.Num() - 1)];
            Target = Settings.Intensity;
        }
    }
    if (bOverride)
    {
        Type = OverrideType;
        Target = OverrideIntensity;
    }
    Target = Type == EDaocWeatherType::Sunny ? 0.f : FMath::Clamp(Target, 0.f, 1.f);
    if (CurrentType != Type || !FMath::IsNearlyEqual(LastTarget, Target))
    {
        CurrentType = Type;
        LastTarget = Target;
        OnWeatherChanged.Broadcast(Type, Target);
        UE_LOG(LogTemp, Log, TEXT("DAoC weather zone %d: type %d, intensity %.2f, server front %s"), P->ZoneId, int32(Type), Target, bInsideServerStorm ? TEXT("inside") : TEXT("outside"));
    }
    const float Step = 1.f / FMath::Max(.1f, Settings.TransitionSeconds);
    RainAmount = FMath::FInterpConstantTo(RainAmount, Type == EDaocWeatherType::Rain ? Target : 0.f, DeltaTime, Step);
    SnowAmount = FMath::FInterpConstantTo(SnowAmount, Type == EDaocWeatherType::Snow ? Target : 0.f, DeltaTime, Step);
    CurrentIntensity = FMath::Max(RainAmount, SnowAmount);
    if (CurrentIntensity > .001f && !EnsureAssets(Network))
        return;
    auto *Controller = UGameplayStatics::GetPlayerController(this, 0);
    auto *Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
    if (!Camera)
        return;
    const FVector Location = Camera->GetCameraLocation();
    const double Now = FPlatformTime::Seconds();
    if (Settings.bSuppressParticlesUnderShelter && CurrentIntensity > .001f && Now >= NextShelterCheck)
    {
        NextShelterCheck = Now + .25;
        const auto *Pawn = Network->LocalPlayerPawn.Get();
        const FVector Start = Pawn ? Pawn->GetActorLocation() + FVector(0, 0, 30) : Location;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DaocWeatherShelter), false, Pawn);
        if (Host)
            Query.AddIgnoredActor(Host);
        FHitResult Hit;
        bSheltered = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + FVector(0, 0, 2000), ECC_Visibility, Query);
    }
    const float Exposure = Settings.bSuppressParticlesUnderShelter && bSheltered ? 0.f : 1.f;
    const FVector Anchor = Location + FVector(0, 0, 350);
    UpdateParticles(RainParticles, RainMaterial, RainAmount * Exposure, 1200, Anchor);
    UpdateParticles(SnowParticles, SnowMaterial, SnowAmount * Exposure, 200, Anchor);
    UpdateCollection(RainAmount, SnowAmount, Settings.SnowSurfaceAmount);
    UpdateScene(Settings, CurrentIntensity, bInsideServerStorm ? W.FogDiffusion : 0);
}
