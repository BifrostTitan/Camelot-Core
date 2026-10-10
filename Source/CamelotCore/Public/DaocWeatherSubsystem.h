#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DaocWeatherTypes.h"
#include "DaocWeatherSubsystem.generated.h"
class ADaocZoneActor;
class UNiagaraComponent;
class UMaterialInstanceDynamic;
class UDaocWeatherAssets;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
/// Publishes the selected visual precipitation and its target blend intensity.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDaocWeatherChanged, EDaocWeatherType, Type, float, TargetIntensity);

// Owns presentation only. Networking remains in the game-instance network subsystem.

/// World-local weather presentation consuming server fronts and zone policy.
UCLASS()
class CAMELOTCORE_API UDaocWeatherSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
  public:
    /// Reports the world types in which weather presentation can operate.
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    /// Releases subsystem bindings and transient resources during shutdown.
    virtual void Deinitialize() override;
    /// Blends weather presentation and updates shelter, particles and scene settings.
    virtual void Tick(float DeltaTime) override;
    /// Returns the Unreal profiling identifier for this tickable object.
    virtual TStatId GetStatId() const override;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Weather")
    EDaocWeatherType CurrentType = EDaocWeatherType::Sunny;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Weather")
    float CurrentIntensity = 0;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Weather")
    bool bInsideServerStorm = false;
    UPROPERTY(BlueprintAssignable, Category = "DAoC|Weather")
    FDaocWeatherChanged OnWeatherChanged;
    /// Local visual preview; does not alter the DOL region or other clients.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Weather")
    void SetWeatherOverride(EDaocWeatherType Type, float Intensity = .55f);
    /// Removes the local override and resumes configured zone weather policy.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Weather")
    void ClearWeatherOverride();

  private:
    /// Resolves prepared weather dependencies from the active host catalog.
    bool EnsureAssets(class UDaocNetworkSubsystem *Network);
    /// Restores original scene settings changed by weather presentation.
    void RestoreScene();
    /// Clears active weather presentation and associated transient state.
    void ResetWeather();
    /// Blends scene lighting and fog using the active zone weather settings.
    void UpdateScene(const FDaocZoneWeatherSettings &Settings, float Amount, int32 FogDiffusion);
    /// Updates camera-local precipitation particles and their material intensity.
    void UpdateParticles(UNiagaraComponent *Component, UMaterialInstanceDynamic *Material, float Amount, float Rate, FVector Anchor);
    /// Publishes rain, snow and surface accumulation to the weather material collection.
    void UpdateCollection(float Rain, float Snow, float SnowSurfaceAmount);
    UPROPERTY(Transient)
    TObjectPtr<AActor> Host;
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> RainParticles;
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> SnowParticles;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> RainMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> SnowMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UDaocWeatherAssets> Assets;
    UPROPERTY(Transient)
    TWeakObjectPtr<ADaocZoneActor> Zone;
    UPROPERTY(Transient)
    TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
    TMap<TWeakObjectPtr<UDirectionalLightComponent>, float> OriginalSunIntensity;
    float OriginalFogDensity = 0, RainAmount = 0, SnowAmount = 0, LastTarget = -1;
    double NextShelterCheck = 0;
    int32 LastWorldRevision = INDEX_NONE;
    bool bSheltered = false, bOwnFog = false, bAssetLoadAttempted = false, bOverride = false;
    EDaocWeatherType OverrideType = EDaocWeatherType::Sunny;
    float OverrideIntensity = .55f;
};
