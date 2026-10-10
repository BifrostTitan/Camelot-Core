#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DaocLiveCrowdCommandlet.generated.h"
/// Editor commandlet preparing and saving live crowd animation providers.
UCLASS()
class UDaocLiveCrowdCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocLiveCrowdCommandlet();
    /// Runs this commandlet with explicit parameters and returns its process status.
    virtual int32 Main(const FString &Params) override;
};
