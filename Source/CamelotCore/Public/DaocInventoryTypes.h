#pragma once
#include "CoreMinimal.h"
#include "DaocInventoryTypes.generated.h"
/// Confirmed owned item metadata associated with a classic inventory slot.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocInventoryItem
{
    GENERATED_BODY()
    /// Classic server inventory slot ID, rather than the position in the Items array.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Slot = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Level = 0;
    /// Decoded stack count; non-stackable equipment normally has a count of one.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Count = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ObjectType = 0;
    /// Server item appearance model ID used to resolve presentation assets.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Model = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Extension = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ColorOrEmblem = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Effect = 0;
    /// Server item weight in tenths of a pound.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Weight = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Condition = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Durability = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Quality = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Bonus = 0;
};
/// Revisioned equipment/backpack snapshot and confirmed item-detail response.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocInventoryState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TArray<FDaocInventoryItem> Items;
    /// Snapshot identity required by inventory requests; stale views cannot move items.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int64 Revision = 0;
    /// Equipment contents have been received, including confirmed empty slots.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bEquipmentKnown = false;
    /// Backpack contents have been received, including confirmed empty slots.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bBackpackKnown = false;
    /// Classic slot associated with DetailText, or INDEX_NONE when no detail is selected.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 DetailSlot = INDEX_NONE;
    /// Server-provided item detail text associated with DetailSlot.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString DetailText;
};
