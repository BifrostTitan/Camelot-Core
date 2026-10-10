#include "DaocGameMode.h"
#include "DaocFrontendHUD.h"
#include "DaocPlayerCharacter.h"
#include "DaocZoneActor.h"
#include "ProceduralMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
ADaocGameMode::ADaocGameMode()
{
    DefaultPawnClass = ADaocPlayerCharacter::StaticClass();
    HUDClass = ADaocFrontendHUD::StaticClass();
}
APawn *ADaocGameMode::SpawnDefaultPawnAtTransform_Implementation(AController *Player, const FTransform &Transform)
{
    FTransform Spawn = Transform;
    // Ground the authored PlayerStart on the zone terrain, not on tree canopies.
    for (TActorIterator<ADaocZoneActor> It(GetWorld()); It; ++It)
    {
        FVector Surface;
        if (It->FindTerrainSurface(Spawn.GetLocation(), Surface))
        {
            const auto *Character = DefaultPawnClass->GetDefaultObject<ACharacter>();
            Spawn.SetLocation(Surface + FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5));
        }
        break;
    }
    return Super::SpawnDefaultPawnAtTransform_Implementation(Player, Spawn);
}
