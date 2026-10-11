#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DaocEquipmentCatalog.generated.h"
class USkeletalMesh;
class UStaticMesh;
class UTexture2D;
class UMaterialInterface;
/// Prepared hair geometry and texture choices for one appearance variant.
USTRUCT(BlueprintType)
struct FDaocHairVariation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Style = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<TSoftObjectPtr<UTexture2D>> Textures;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FLinearColor> Tints;
};
/// Face and hair options associated with an imported figure.
USTRUCT(BlueprintType)
struct FDaocFigureCustomization
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Figure = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<TSoftObjectPtr<UTexture2D>> FaceTextures;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDaocHairVariation> Hair;
};
/// Prepared equipment mesh and texture mapping for a geometry variant.
USTRUCT(BlueprintType)
struct FDaocEquipmentGeometry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Part = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Bit = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<USkeletalMesh> Mesh;
};
/// Body assembly and equipment compatibility metadata for one figure.
USTRUCT(BlueprintType)
struct FDaocEquipmentFigure
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Model = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Figure = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> BaseSectionParts;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDaocEquipmentGeometry> Geometry;
};
/// Equipment model mapping to prepared appearance geometry.
USTRUCT(BlueprintType)
struct FDaocEquipmentItem
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Model = 0;
    /// Zero denotes a rigid item shared by figures; modular armor is figure-specific.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Figure = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString Name;
    /// Nine Catacombs parts; -1 suppresses a part, positive values allow geometry variants.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> PartMasks;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<TSoftObjectPtr<UTexture2D>> PartTextures;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UStaticMesh> Mesh;
    /// Closed helmets suppress hair; open caps and circlets retain the selected hairstyle.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bHideHair = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bShield = false;
};
/// Saved figure, item and customization lookup data for modular assembly.
UCLASS(BlueprintType)
class CAMELOTCORE_API UDaocEquipmentCatalog : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDaocEquipmentFigure> Figures;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDaocEquipmentItem> Items;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, FLinearColor> DyeColors;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UMaterialInterface> ArmorMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDaocFigureCustomization> Customizations;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UMaterialInterface> FaceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UMaterialInterface> HairMaterial;
};
