#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DaocZoneActor.h"
#include "DaocWeatherTypes.h"
#include "DaocZoneProfile.generated.h"

/// One prepared outdoor zone, with appearance choices isolated from other zones.
UCLASS(BlueprintType)
class CAMELOTCORE_API UDaocZoneProfile : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
    int32 ZoneId = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
    bool bHasTerrain = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
    FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
    FString ExportDirectory;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water")
    bool bGenerateWater = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water")
    TObjectPtr<UMaterialInterface> WaterMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    TArray<FDaocZoneWaterSurface> WaterSurfaces;
    UPROPERTY()
    bool bWaterDataPrepared = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
    FDaocZoneWeatherSettings Weather;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
    TObjectPtr<UMaterialInterface> OriginalTerrainMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
    TObjectPtr<UMaterialInterface> EnhancedTerrainMaterial;
    /// Per-zone semantic weights for a shared grass/soil/rock/sand terrain parent.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
    TObjectPtr<UTexture2D> EnhancedTerrainWeights;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enhancement Defaults")
    bool bUseDefaultEnhancement = true;
    /// Empty local lists inherit defaults. Enable this to disable ground cover or override scalar settings.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover")
    bool bOverrideGroundCover = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover")
    FLinearColor GroundCoverTint = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover")
    TObjectPtr<UMaterialInterface> GroundCoverMaterialOverride;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
    TArray<FDaocSceneryReplacement> SceneryReplacements;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover")
    TArray<TObjectPtr<UStaticMesh>> GroundCoverMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover", meta = (ClampMin = "40"))
    float GroundCoverSpacing = 90;
    /// Zone-local mask response: allow mixed grass/soil while keeping zero-grass surfaces bare.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover", meta = (ClampMin = "0", ClampMax = "1"))
    float GroundCoverMinimumGrassWeight = .15f;
    /// Grass mask weight at which every candidate is accepted (before slope/fixture checks).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover", meta = (ClampMin = "0", ClampMax = "1"))
    float GroundCoverFullDensityGrassWeight = .5f;

    /// Extra clearance beyond a solid fixture's footprint, in actor-local Unreal units.
    /// This is always a per-zone setting, independent of shared grass mesh overrides.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover", meta = (ClampMin = "0", UIMin = "0", UIMax = "100"))
    float GroundCoverFixturePadding = 5.f;
    /// Multipliers on the existing randomized clump size. Taller clumps form occasional patches.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover", meta = (ClampMin = "0.01"))
    float GroundCoverScaleMultiplier = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover", meta = (ClampMin = "0.01"))
    float GroundCoverTallScaleMultiplier = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover", meta = (ClampMin = "0", ClampMax = "1"))
    float GroundCoverTallPatchChance = .12f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Cover", meta = (ClampMin = "100"))
    float GroundCoverCullDistance = 2500;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0", ClampMax = "1"))
    float TerrainRoughness = .9f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
    bool bCastMaskedSceneryShadows = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    TObjectPtr<UTexture2D> TerrainDiffuseTexture;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    int32 TerrainWidth = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    int32 TerrainHeight = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    TArray<float> TerrainHeightSamples;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    TArray<FDaocImportedNif> ImportedNifs;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    TArray<FDaocZonePlacement> Placements;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    TArray<FName> RequiredNifNames;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Imported Data")
    int32 GroundCoverMaskWidth = 0;
    UPROPERTY()
    TArray<uint8> GroundCoverMask;
};
