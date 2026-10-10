#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DaocZoneActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UTexture2D;
class UMaterialInstanceDynamic;
class UDaocZoneProfile;
class UDaocClientAssetCatalog;

/// Identifies the imported scenery layer used for rendering and collision.
UENUM(BlueprintType)
enum class EDaocMeshLayer : uint8
{
    Visible,
    Collidee,
    Pickee
};

/// Imported source-model identity and its prepared mesh reference.
USTRUCT(BlueprintType)
struct FDaocImportedNif
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    FName NifName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    TObjectPtr<UStaticMesh> VisibleMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    TObjectPtr<UStaticMesh> CollideeMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    TObjectPtr<UStaticMesh> PickeeMesh;

    /// True for meshes made by the DAoC object importer. Legacy manual assets keep their existing basis.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    bool bDaocZUp = false;
};

/// Prepared water geometry and source metadata for a zone surface.
USTRUCT(BlueprintType)
struct FDaocZoneWaterSurface
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    int32 Id = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    FString Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    FString Type;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    FString Texture;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    FString MultiTexture;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    float Height = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    float Flow = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    int32 SourceColor = 0;
    /// Original sector.dat bank coordinates (256-grid), kept independent of render scale.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    TArray<FVector2D> LeftBank;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
    TArray<FVector2D> RightBank;
};

/// Exact source-name mapping to a host-owned scenery replacement.
USTRUCT(BlueprintType)
struct FDaocSceneryReplacement
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC")
    FName NifName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC")
    TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC", meta = (ClampMin = "0.25", ClampMax = "2.0"))
    float HeightMultiplier = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC", meta = (ClampMin = "0.25", ClampMax = "2.0"))
    float WidthMultiplier = 1.0f;
    /// Vertical placement adjustment in Unreal centimeters, independent of mesh scale.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC", meta = (Units = "cm", ToolTip = "Negative values lower this replacement into the ground; zero preserves placement."))
    float GroundPlacementOffset = 0.0f;
    /// A null mesh explicitly preserves this NIF in a zone override.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC")
    bool bForceLod0 = false;
};

/// One imported scenery placement with its source transform and identity.
USTRUCT(BlueprintType)
struct FDaocZonePlacement
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    FName NifName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    int32 FixtureId = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    int32 NifId = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    EDaocMeshLayer Layer = EDaocMeshLayer::Visible;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    FVector GamePosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    FRotator UnrealRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement", meta = (ClampMin = "0.0"))
    float GeometryScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    float RotationAngleRadians = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    FVector RotationAxis = FVector::UpVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    FVector PositionOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    FName PlacementKind;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    bool bFlip = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    bool bOnGround = false;
};

/// Prepared-zone renderer owning terrain, scenery, water and ground cover.
UCLASS(BlueprintType)
class CAMELOTCORE_API ADaocZoneActor : public AActor
{
    GENERATED_BODY()

  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Catalog")
    TObjectPtr<UDaocClientAssetCatalog> ClientAssetCatalog;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Zone")
    bool bHasTerrain = true;
    /// Assigns already prepared zone placements without importing runtime assets.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Import")
    void SetPreparedPlacements(int32 NewZoneId, const TArray<FDaocZonePlacement> &Prepared, bool bTerrain = true);

    /// Refreshes saved water definitions from the configured zone export.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DAoC|Water")
    bool RefreshWaterData();
    /// Initializes default settings and owned components.
    ADaocZoneActor();
    /// Returns the zone ID represented by this prepared renderer.
    int32 GetZoneId() const
    {
        return ZoneId;
    }
    /// Returns Unreal world units per source game coordinate unit.
    float GetWorldUnitsPerDaocUnit() const
    {
        return WorldUnitsPerDaocUnit;
    }
    /// Bounds-based picking also works for foliage without collision. Returns source NIF data.
    bool InspectFixture(const FVector &Start, const FVector &End, FVector &Hit, FString &Information, FName &Nif) const;
    /// Switches profile-driven scenery/material presentation while retaining zone coordinates.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Enhancement")
    void SetEnhancementEnabled(bool bEnabled);

  private:
    /// Fixture instance metadata retained for developer picking.
    struct FFixturePick
    {
        TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
        int32 Instance = INDEX_NONE;
        int32 Placement = INDEX_NONE;
    };
    TArray<FFixturePick> FixturePicks;
    /// Restores source fixture identities used for inspection and replacement lookup.
    void RestoreFixtureIds();

  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Zone Profiles")
    TArray<TObjectPtr<UDaocZoneProfile>> ZoneProfiles;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Zone Profiles")
    TObjectPtr<UDaocZoneProfile> SelectedZoneProfile;

    /// Loads the profile currently selected on the renderer.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DAoC|Zone Profiles")
    bool LoadSelectedZoneProfile();

    /// Applies prepared geometry and appearance from the supplied saved profile.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Zone Profiles")
    bool LoadZoneProfile(UDaocZoneProfile *Profile);

    /// Resolves a prepared profile by zone ID and loads it without runtime conversion.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Zone Profiles")
    bool LoadZoneById(int32 NewZoneId);

    /// Snapshot imported outdoor data and current appearance into an editor asset.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Zone Profiles")
    bool CaptureZoneProfile(UDaocZoneProfile *Profile, bool bCaptureAppearance = true);

    /// Row-major samples in rendered units: terrainRed/255*scale + offsetRed/255*offset.
    UFUNCTION(BlueprintCallable, Category = "DAoC")
    void SetTerrainData(int32 Width, int32 Height, const TArray<float> &HeightSamples);

    /// Assigns the prepared source-name to mesh mappings used by scenery instances.
    UFUNCTION(BlueprintCallable, Category = "DAoC")
    void SetImportedNifs(const TArray<FDaocImportedNif> &Nifs);

    /// Assigns source scenery placements for the next zone rebuild.
    UFUNCTION(BlueprintCallable, Category = "DAoC")
    void SetPlacements(const TArray<FDaocZonePlacement> &NewPlacements);

    /// Loads source zone data from the explicitly configured export directory.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DAoC")
    bool LoadZoneExport();

    /// Loads prepared terrain textures for the active zone.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DAoC")
    bool LoadTerrainTextures();

    /// Loads source zone data from an explicit authoring directory.
    UFUNCTION(BlueprintCallable, Category = "DAoC")
    bool LoadZoneExportFromDirectory(const FString &ExportDirectory);

    /// Rebuilds terrain, scenery, water and cover from current prepared inputs.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DAoC")
    void RebuildZone();

    /// Uses height samples before collision cooking has completed.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Zone")
    bool FindTerrainSurface(FVector WorldPosition, FVector &Surface) const;

    /// Reports whether the world position lies below a prepared water surface.
    bool IsUnderwater(FVector WorldPosition) const;

    /// Changes masked scenery shadow behavior on this renderer.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Appearance")
    void SetMaskedSceneryShadows(bool bEnabled);

    /// Assigns validated square mask samples tagged with their source zone.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Enhancement")
    bool SetGroundCoverMask(int32 Width, const TArray<uint8> &Samples, const FString &SourceZone);

  protected:
    /// Applies renderer construction settings in the editor or spawned world.
    virtual void OnConstruction(const FTransform &Transform) override;
    /// Binds this object to its world/session when gameplay begins.
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC")
    TObjectPtr<UProceduralMeshComponent> TerrainMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Water")
    TObjectPtr<UProceduralMeshComponent> WaterMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Water")
    bool bGenerateWater = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Water")
    TObjectPtr<UMaterialInterface> WaterMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Water")
    TArray<FDaocZoneWaterSurface> WaterSurfaces;
    UPROPERTY()
    bool bWaterDataPrepared = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Terrain", meta = (ClampMin = "0"))
    int32 TerrainWidth = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Terrain", meta = (ClampMin = "0"))
    int32 TerrainHeight = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Terrain")
    TArray<float> TerrainHeightSamples;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Terrain")
    TObjectPtr<UMaterialInterface> TerrainMaterial;

    /// Optional parent for textured terrain. Requires ZoneDiffuse texture parameter.
    /// When empty, the editor creates /Game/DAoC/Materials/M_DaocTerrain.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Terrain|Textures")
    TObjectPtr<UMaterialInterface> TerrainTextureMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Terrain|Textures")
    TObjectPtr<UTexture2D> TerrainDiffuseTexture;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Terrain|Textures")
    bool bAutoLoadTerrainTextures = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Terrain|Textures", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TerrainRoughness = 0.9f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Scale", meta = (ClampMin = "0.0001"))
    float WorldUnitsPerDaocUnit = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    TArray<FDaocImportedNif> ImportedNifs;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    bool bAutoLoadObjectAssets = true;

    /// Applies to whole mesh groups containing masked materials (canopies,
    /// shrubs, etc.), including their opaque sections. Solid buildings retain shadows.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Appearance")
    bool bCastMaskedSceneryShadows = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    bool bEnableEnhancement = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    TObjectPtr<UMaterialInterface> EnhancedTerrainMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    TArray<FDaocSceneryReplacement> SceneryReplacements;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    TArray<TObjectPtr<UStaticMesh>> GroundCoverMeshes;

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
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement", meta = (ClampMin = "40.0"))
    float GroundCoverSpacing = 90.0f;
    /// Multipliers on the existing randomized clump size. Taller clumps form occasional patches.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement", meta = (ClampMin = "0.01"))
    float GroundCoverScaleMultiplier = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement", meta = (ClampMin = "0.01"))
    float GroundCoverTallScaleMultiplier = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement", meta = (ClampMin = "0", ClampMax = "1"))
    float GroundCoverTallPatchChance = .12f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement", meta = (ClampMin = "100.0"))
    float GroundCoverCullDistance = 2500.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    FLinearColor GroundCoverTint = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    TObjectPtr<UMaterialInterface> GroundCoverMaterialOverride;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Enhancement")
    int32 GroundCoverInstanceCount = 0;

    UPROPERTY()
    FString EnhancementZoneDirectory;

    UPROPERTY()
    int32 GroundCoverMaskWidth = 0;

    UPROPERTY()
    TArray<uint8> GroundCoverMask;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    int32 VisibleFixtureCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    TArray<FName> MissingFixtureMeshes;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Assets")
    TArray<FName> RequiredNifNames;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Zone")
    int32 ZoneId = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Zone", meta = (ContentDir))
    FString ZoneExportDirectory;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Placement")
    TArray<FDaocZonePlacement> Placements;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Layers")
    bool bShowCollideeLayer = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Layers")
    bool bShowPickeeLayer = false;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> GeneratedInstances;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> TerrainMaterialInstance;

  private:
    /// Rebuilds procedural terrain geometry and collision from current height samples.
    void RebuildTerrain();
    /// Rebuilds procedural water sections from prepared shoreline definitions.
    void RebuildWater();
    /// Reads water definitions from the explicit zone export directory.
    bool ImportWaterData(const FString &Directory);
    /// Tests an actor-local point against prepared water geometry.
    bool IsBelowWater(float LocalX, float LocalY, float LocalZ) const;
    /// Rebuilds scenery instances from source placements and profile replacements.
    void RebuildInstances();
    /// Regenerates deterministic cover using masks, slopes and fixture exclusions.
    void RebuildGroundCover();
    /// Reports whether this renderer enables profile-driven enhanced appearance.
    bool IsEnhancementZone() const;
    /// Applies this profile appearance overrides without changing zone geometry identity.
    void ApplyProfileAppearance();
    /// Selects the prepared classic or enhanced terrain material for this renderer.
    void ApplyTerrainMaterial();
    /// Imports terrain material inputs from the explicit prepared directory.
    bool ImportTerrainTextures(const FString &Directory);
    /// Samples terrain height at zone-local game coordinates.
    float SampleTerrainHeight(float GameX, float GameY) const;
    /// Finds a prepared fixture record by source NIF name; returns nullptr when absent.
    const FDaocImportedNif *FindImportedNif(FName NifName) const;
};
