#pragma once
#include "Commandlets/Commandlet.h"
#include "DaocCharactersCommandlet.generated.h"
/// Editor commandlet importing prepared skeletal character manifests.
UCLASS()
class UDaocCharactersCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocCharactersCommandlet();
    /// Imports an explicit prepared manifest in the editor; reimporting is opt-in.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Import")
    static bool ImportManifest(const FString &File, bool bReimport = false);
    /// Runs this commandlet with explicit parameters and returns its process status.
    virtual int32 Main(const FString &Params) override;
};
