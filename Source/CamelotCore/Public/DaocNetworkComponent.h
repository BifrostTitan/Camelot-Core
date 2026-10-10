#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DaocNetworkComponent.generated.h"
/// Inert compatibility type for components serialized in older levels.
UCLASS(NotBlueprintable)
class CAMELOTCORE_API UDaocNetworkComponent : public UActorComponent
{
    GENERATED_BODY()
};
