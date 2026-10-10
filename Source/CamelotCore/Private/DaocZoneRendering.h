#pragma once
#include "CoreMinimal.h"
class UStaticMesh;

namespace DaocZoneRendering
{
/// Nanite preservation can expand culling bounds beyond the authored geometry.
FBoxSphereBounds GeometryBounds(const UStaticMesh &Mesh);
} // namespace DaocZoneRendering

#if WITH_EDITOR
namespace DaocZoneRendering
{
/// Whole-mesh validation: a single unsupported section requires conventional rendering.
bool CanUseNanite(const UStaticMesh &Mesh);
/// Authoring only. The caller builds and saves the mesh and material usage flags.
bool ConfigureNanite(UStaticMesh &Mesh, bool bPreserveArea);
} // namespace DaocZoneRendering
#endif
