#include "DaocLoginGameMode.h"
#include "DaocFrontendHUD.h"
#include "GameFramework/SpectatorPawn.h"
ADaocLoginGameMode::ADaocLoginGameMode()
{
    DefaultPawnClass = ASpectatorPawn::StaticClass();
    HUDClass = ADaocFrontendHUD::StaticClass();
}
