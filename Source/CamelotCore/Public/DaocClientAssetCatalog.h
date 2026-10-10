#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DaocClientAssetCatalog.generated.h"
class USkeletalMesh;
class UDaocZoneProfile;
class UDaocWeatherAssets;
/// Generated classic assets. Explicit settings override these soft references.
UCLASS(BlueprintType)
class CAMELOTCORE_API UDaocClientAssetCatalog : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Catalog")
    TMap<int32, TSoftObjectPtr<USkeletalMesh>> Characters;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Catalog")
    TMap<int32, TSoftObjectPtr<UDaocZoneProfile>> ClassicZones;
    /// Authored appearances are separate from the immutable imported classic reference.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    TMap<int32, TSoftObjectPtr<UDaocZoneProfile>> ZoneEnhancementProfiles;
    /// Appearance-only profile (ZoneId=-1); local zone choices take precedence.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    TObjectPtr<UDaocZoneProfile> DefaultEnhancementSettings;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Catalog")
    FString SourceVersion;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Weather")
    TObjectPtr<UDaocWeatherAssets> WeatherAssets;
    /// Hard catalog reference makes the prepared live providers reachable by the cooker.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Crowd")
    TObjectPtr<class UCamelotLiveCrowdCatalog> LiveCrowdCatalog;
    /// Python's soft-object map conversion loads objects. These setters retain
    /// paths directly so batch imports can release meshes between jobs.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Catalog")
    void SetCharacter(int32 Model, const FString &Path)
    {
        Characters.Add(Model, TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Path)));
    }
    /// Associates a zone ID with its saved classic profile reference.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Catalog")
    void SetClassicZone(int32 Zone, const FString &Path)
    {
        ClassicZones.Add(Zone, TSoftObjectPtr<UDaocZoneProfile>(FSoftObjectPath(Path)));
    }
};
