#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CamelotUITheme.generated.h"
class UTexture2D;

/// Projects can author their own appearance without replacing the HUD behavior.
UCLASS(BlueprintType)
class CAMELOTCORE_API UCamelotUITheme : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Panel = FLinearColor(.025f, .035f, .05f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Recess = FLinearColor(.012f, .018f, .028f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Border = FLinearColor(.20f, .24f, .29f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Accent = FLinearColor(.88f, .69f, .35f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Text = FLinearColor(.92f, .94f, .97f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Muted = FLinearColor(.57f, .64f, .72f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Health = FLinearColor(.72f, .16f, .20f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Mana = FLinearColor(.15f, .42f, .82f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Endurance = FLinearColor(.25f, .64f, .40f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Friendly = FLinearColor(.42f, .87f, .63f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Palette")
    FLinearColor Hostile = FLinearColor(1.f, .40f, .37f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor NameAlly = FLinearColor(.25f, .85f, 1.f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor NameEnemy = FLinearColor(1.f, .12f, .16f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor ConGray = FLinearColor(.55f, .55f, .57f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor ConGreen = FLinearColor(.48f, .90f, .43f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor ConBlue = FLinearColor(.38f, .49f, .83f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor ConYellow = FLinearColor(1.f, .80f, .18f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor ConOrange = FLinearColor(1.f, .43f, .10f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor ConRed = FLinearColor(1.f, .12f, .16f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor ConPurple = FLinearColor(.76f, .38f, 1.f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nameplates")
    FLinearColor ConDarkPurple = FLinearColor(.48f, .20f, .72f, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "10", ClampMax = "24"))
    int32 FontSize = 14;
    /// Explicit project-owned textures; no icon-ID guesses or fabricated abilities.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
    TMap<int32, TSoftObjectPtr<UTexture2D>> AbilityIcons;
    /// Inventory packets supply model IDs, not a universal icon index. Hosts provide the artwork.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
    TMap<int32, TSoftObjectPtr<UTexture2D>> ItemIcons;
};
