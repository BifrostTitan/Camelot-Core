#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DaocWeatherAssets.generated.h"
class UNiagaraSystem;
class UMaterialInterface;
class UMaterialParameterCollection;
class UMaterialFunctionInterface;

/// Hard references keep the owned particle systems and their dependencies in cooked clients.
UCLASS(BlueprintType)
class CAMELOTCORE_API UDaocWeatherAssets : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
    TObjectPtr<UNiagaraSystem> Rain;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
    TObjectPtr<UNiagaraSystem> Snow;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
    TObjectPtr<UMaterialInterface> RainMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
    TObjectPtr<UMaterialInterface> SnowMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
    TObjectPtr<UMaterialParameterCollection> Wetness;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
    TObjectPtr<UMaterialFunctionInterface> SnowBuildup;
};
