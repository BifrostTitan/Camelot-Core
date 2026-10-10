#pragma once
#include "CoreMinimal.h"
#include "DaocCharacterAppearance.generated.h"
/// Reported equipment slot, model and visual customization identifiers.
USTRUCT(BlueprintType)
struct FDaocEquipmentAppearance
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Slot = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Model = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ColorOrEmblem = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Effect = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Extension = 0;
};
/// Replicated model, customization and equipment snapshot used by presentation.
USTRUCT(BlueprintType)
struct FDaocCharacterAppearance
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Model = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 RawModelCode = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ModelFlags = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Race = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Gender = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Realm = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 Level = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ClassId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 EyeSize = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 LipSize = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 EyeColor = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 HairColor = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 FaceType = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 HairStyle = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 MoodType = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 CustomisationStep = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 RightWeapon = 255;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 LeftWeapon = 255;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bHoodUp = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bEquipmentKnown = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int64 Revision = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TArray<FDaocEquipmentAppearance> Equipment;
};
/// Notifies observers of confirmed appearance changes for a server object ID.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDaocAppearanceChanged, int32, ObjectId, const FDaocCharacterAppearance &, Appearance);
