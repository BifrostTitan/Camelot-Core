#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DaocEffectsCommandlet.generated.h"
/// Offline import of decoded sprite recipes into host-owned lightweight Niagara assets.
UCLASS()
class CAMELOTCORE_API UDaocEffectsCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    UDaocEffectsCommandlet();
    virtual int32 Main(const FString &Params) override;
    /// Imports a prepared manifest; source archives are never accessed at runtime.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Import")
    static bool ImportManifest(const FString &Path);
};
