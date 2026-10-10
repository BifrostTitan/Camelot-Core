#pragma once
#include "CoreMinimal.h"
class SWidget;
class UDaocDeveloperComponent;
/// Constructs the administration widget bound to an authorized developer component.
TSharedRef<SWidget> CreateDaocAdminPanel(UDaocDeveloperComponent *Developer);
