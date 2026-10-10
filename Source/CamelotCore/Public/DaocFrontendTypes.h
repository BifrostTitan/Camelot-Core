#pragma once
#include "CoreMinimal.h"
#include "DaocCharacterAppearance.h"
#include "DaocFrontendTypes.generated.h"
/// Server roster entry available for character selection.
USTRUCT(BlueprintType)
struct FDaocCharacterSlot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int32 Slot = 0;
    UPROPERTY(BlueprintReadOnly)
    FDaocCharacterAppearance Appearance;
    UPROPERTY(BlueprintReadOnly)
    FString Location;
    UPROPERTY(BlueprintReadOnly)
    FString ClassName;
    UPROPERTY(BlueprintReadOnly)
    FString RaceName;
};
/// Character-creation draft sent for authoritative server validation.
USTRUCT(BlueprintType)
struct FDaocCreationRequest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDaocCharacterAppearance Appearance;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<int32> Stats;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Slot = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Region = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsles = false;
};
