#pragma once
#include "Commandlets/Commandlet.h"
#include "DaocAnimationsCommandlet.generated.h"
class USkeletalMeshComponent;
/// Editor commandlet importing prepared animation manifests.
UCLASS()
class UDaocAnimationsCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocAnimationsCommandlet();
    /// Runs this commandlet with explicit parameters and returns its process status.
    virtual int32 Main(const FString &Params) override;
    /// Imports an explicit prepared manifest in the editor; reimporting is opt-in.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Import")
    static bool ImportManifest(const FString &File, bool bReimport = false);
    /// Editor capture tooling can evaluate a precise frame without a running PIE
    /// world.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Animation Preview")
    static void EvaluatePreviewPose(USkeletalMeshComponent *Component, float Time);
};
