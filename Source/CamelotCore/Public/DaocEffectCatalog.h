#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DaocEffectCatalog.generated.h"
class UNiagaraSystem;

/// One classic speffects.csv binding to a prepared particle system.
USTRUCT(BlueprintType)
struct FDaocVisualEffect
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UNiagaraSystem> System;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Socket;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Duration = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Tail = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float PlaybackRate = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 ParticleCost = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bAutoScale = false;
};

/// Visual stages remain separate from spell, ability and icon identifiers.
USTRUCT(BlueprintType)
struct FDaocSpellVisuals
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> Gather;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> Release;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> Projectile;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> Hit;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> Miss;
};

/// Host-owned, preconverted effects; runtime never opens client archives or NIFs.
UCLASS(BlueprintType)
class CAMELOTCORE_API UDaocEffectCatalog : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, FDaocVisualEffect> Effects;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<int32, FDaocSpellVisuals> Spells;
};
