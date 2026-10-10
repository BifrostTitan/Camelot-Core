#pragma once
#include "CoreMinimal.h"

class UTexture2D;
class UMaterialInterface;

namespace DaocTerrainTextures
{
/// texXX-YY: first coordinate is X, second is Y; decoded rows start at the top.
bool LoadAtlasPixels(const FString &Directory, int32 &Width, int32 &Height, TArray<FColor> &Pixels, FString &Error);
/// Imports the decoded terrain atlas as a texture; reports failure through Error.
UTexture2D *ImportAtlas(const FString &Directory, FString &Error);
/// Resolves or prepares the terrain material using the supplied atlas texture.
UMaterialInterface *DefaultMaterial(UTexture2D *DefaultTexture);
#if WITH_DEV_AUTOMATION_TESTS
/// Checks supported atlas decoding against prepared fixtures and reports errors.
bool RunAtlasTests(FString &Error);
#endif
} // namespace DaocTerrainTextures
