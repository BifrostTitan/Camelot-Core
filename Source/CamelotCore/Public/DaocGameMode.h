#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DaocGameMode.generated.h"
/// Gameplay mode selecting the core pawn and HUD for prepared zone entry.
UCLASS()
class CAMELOTCORE_API ADaocGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    ADaocGameMode();
    /// Spawns the gameplay pawn using the prepared zone entry transform.
    virtual APawn *SpawnDefaultPawnAtTransform_Implementation(AController *Player, const FTransform &Transform) override;
};
