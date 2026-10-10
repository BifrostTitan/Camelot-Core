#pragma once
#include "CoreMinimal.h"
#if WITH_EDITOR
class UNiagaraEmitter;
class UMaterialParameterCollection;
class UMaterialFunctionInterface;
class UDaocWeatherAssets;
// Called explicitly by the editor toolbox, never while logging in or generating a zone.
/// Prepares reusable weather assets from host inputs; reports authoring failure through Error.
UDaocWeatherAssets *DaocPrepareWeatherAssets(UNiagaraEmitter *RainEmitter, UMaterialParameterCollection *Wetness, UMaterialFunctionInterface *SnowBuildup, FString &Error);
#endif
