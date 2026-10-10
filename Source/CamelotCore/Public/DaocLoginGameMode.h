#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DaocLoginGameMode.generated.h"
/// The login map has no gameplay pawn, terrain or world simulation.
UCLASS()
class CAMELOTCORE_API ADaocLoginGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    ADaocLoginGameMode();
};
