#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DaocFrontendTheme.generated.h"
class UTexture2D;
class UStaticMesh;
/// Enhanced themes inherit missing pieces from the classic theme; behavior stays shared.
UCLASS(BlueprintType)
class CAMELOTCORE_API UDaocFrontendTheme : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<FName, TSoftObjectPtr<UTexture2D>> Images;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<int32, TSoftObjectPtr<UStaticMesh>> PreviewStages;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UDaocFrontendTheme> Fallback;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLinearColor TextColor = FLinearColor(.88f, .78f, .5f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CreationRulesJson;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bBlackPreviewStage = false;
    /// Resolves a named theme image; returns nullptr when the key has no loaded texture.
    UTexture2D *Image(FName Name) const;
    /// Assigns a saved texture reference to a semantic frontend image key.
    UFUNCTION(BlueprintCallable)
    void SetImage(FName Name, const FString &Path)
    {
        Images.Add(Name, TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Path)));
    }
};
