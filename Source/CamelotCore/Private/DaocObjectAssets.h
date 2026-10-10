#pragma once
#include "CoreMinimal.h"
#include "Misc/Paths.h"
namespace DaocObjectAssets
{
/// Converts the NIF axial vector through the reflected import coordinate basis.
inline FVector ImportedRotationAxis(const FVector &NifAxis)
{
    // An axial vector gains the determinant sign under reflection B=(X,-Y,Z).
    return FVector(-NifAxis.X, NifAxis.Y, -NifAxis.Z);
}
/// Normalizes a source NIF basename into a deterministic imported mesh asset name.
inline FString AssetName(const FString &NifName)
{
    FString Name = FPaths::GetBaseFilename(NifName).ToLower();
    for (TCHAR &C : Name)
        if (!FChar::IsAlnum(C) && C != '_')
            C = '_';
    return TEXT("SM_daoc_") + Name;
}
/// Builds the saved object path for an imported source NIF basename.
inline FString ObjectPath(const FString &NifName)
{
    const FString Name = AssetName(NifName);
    return TEXT("/Game/DAoC/Nifs/") + Name + TEXT(".") + Name;
}
} // namespace DaocObjectAssets
