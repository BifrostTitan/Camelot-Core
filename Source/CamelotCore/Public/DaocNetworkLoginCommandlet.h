#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DaocNetworkLoginCommandlet.generated.h"
/// Commandlet entry point for checking session login and initialization.
UCLASS()
class UDaocNetworkLoginCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocNetworkLoginCommandlet();
    /// Runs this commandlet with explicit parameters and returns its process status.
    virtual int32 Main(const FString &Params) override;
};
