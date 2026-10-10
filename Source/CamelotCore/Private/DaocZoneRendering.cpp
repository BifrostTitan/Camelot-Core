#include "DaocZoneRendering.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"

FBoxSphereBounds DaocZoneRendering::GeometryBounds(const UStaticMesh &Mesh)
{
    const FStaticMeshRenderData *Data = Mesh.GetRenderData();
    if (Data && Data->LODResources.IsValidIndex(0))
    {
        const FBoxSphereBounds &Bounds = Data->LODResources[0].SourceMeshBounds;
        if (!Bounds.Origin.ContainsNaN() && !Bounds.BoxExtent.ContainsNaN() && Bounds.BoxExtent.GetMin() >= 0.0 && Bounds.BoxExtent.Z > UE_SMALL_NUMBER)
            return Bounds;
    }
    return Mesh.GetBounds();
}

#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

bool DaocZoneRendering::CanUseNanite(const UStaticMesh &Mesh)
{
    if (Mesh.GetStaticMaterials().IsEmpty())
        return false;
    for (const FStaticMaterial &Slot : Mesh.GetStaticMaterials())
    {
        const UMaterialInterface *Material = Slot.MaterialInterface;
        if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface || (Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked) || Material->GetShadingModels().HasShadingModel(MSM_SingleLayerWater))
            return false;
    }
    return true;
}

bool DaocZoneRendering::ConfigureNanite(UStaticMesh &Mesh, bool bPreserveArea)
{
    if (!CanUseNanite(Mesh))
        return false;
    auto Settings = Mesh.GetNaniteSettings();
    Settings.bEnabled = true;
    Settings.bExplicitTangents = true;
    Settings.KeepPercentTriangles = 1.f;
    Settings.TrimRelativeError = 0.f;
    Settings.FallbackTarget = ENaniteFallbackTarget::PercentTriangles;
    Settings.FallbackPercentTriangles = 1.f;
    Settings.FallbackRelativeError = 0.f;
    Settings.ShapePreservation = bPreserveArea ? ENaniteShapePreservation::PreserveArea : ENaniteShapePreservation::None;
    Mesh.SetNaniteSettings(Settings);
    return true;
}
#endif
