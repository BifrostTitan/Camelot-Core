#include "DaocZoneRendering.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocSceneryGeometryBoundsTest, "Camelot.Rendering.SceneryGeometryBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocSceneryGeometryBoundsTest::RunTest(const FString &Parameters)
{
    auto *Mesh = NewObject<UStaticMesh>();
    const FBoxSphereBounds Fallback = Mesh->GetBounds();
    TestEqual(TEXT("Unbuilt meshes retain their bounds fallback"), DaocZoneRendering::GeometryBounds(*Mesh).BoxExtent, Fallback.BoxExtent);
    Mesh->SetRenderData(MakeUnique<FStaticMeshRenderData>());
    auto *Data = Mesh->GetRenderData();
    Data->LODResources.Add(new FStaticMeshLODResources());
    const FBoxSphereBounds Authored(FVector(0, 0, 450), FVector(390, 395, 459), 720);
    Data->LODResources[0].SourceMeshBounds = Authored;
    Data->Bounds = FBoxSphereBounds(FVector(300, 1460, 2425), FVector(897, 1868, 2646), 3694);
    Mesh->CalculateExtendedBounds();
    TestTrue(TEXT("Rendering bounds reproduce Nanite's oversized willow envelope"), Mesh->GetBounds().BoxExtent.Z > Authored.BoxExtent.Z * 5);
    TestEqual(TEXT("Fitting uses geometry, not the expanded rendering envelope"), DaocZoneRendering::GeometryBounds(*Mesh).BoxExtent, Authored.BoxExtent);
    TestEqual(TEXT("Geometry origin is retained"), DaocZoneRendering::GeometryBounds(*Mesh).Origin, Authored.Origin);
    Data->LODResources[0].SourceMeshBounds = FBoxSphereBounds(ForceInit);
    TestEqual(TEXT("Missing geometry bounds retain the rendering fallback"), DaocZoneRendering::GeometryBounds(*Mesh).BoxExtent, Mesh->GetBounds().BoxExtent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocZoneNanitePreparationTest, "Camelot.Rendering.NanitePreparation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocZoneNanitePreparationTest::RunTest(const FString &Parameters)
{
    auto *Mesh = NewObject<UStaticMesh>();
    auto *Opaque = NewObject<UMaterial>();
    auto *Masked = NewObject<UMaterial>();
    Masked->BlendMode = BLEND_Masked;
    auto *Translucent = NewObject<UMaterial>();
    Translucent->BlendMode = BLEND_Translucent;
    TestFalse(TEXT("A mesh without materials cannot be prepared"), DaocZoneRendering::CanUseNanite(*Mesh));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Opaque));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Masked));
    TestTrue(TEXT("Opaque and masked sections can share a Nanite mesh"), DaocZoneRendering::ConfigureNanite(*Mesh, true));
    const auto Prepared = Mesh->GetNaniteSettings();
    TestEqual(TEXT("Source triangles are retained"), Prepared.KeepPercentTriangles, 1.f);
    TestEqual(TEXT("Source trimming is disabled"), Prepared.TrimRelativeError, 0.f);
    TestEqual(TEXT("Fallback retains full walking/render geometry"), Prepared.FallbackPercentTriangles, 1.f);
    TestEqual(TEXT("Fallback error is zero"), Prepared.FallbackRelativeError, 0.f);
    TestTrue(TEXT("Normal-map tangents are explicit"), Prepared.bExplicitTangents);
    TestTrue(TEXT("Foliage silhouette uses area preservation"), Prepared.ShapePreservation == ENaniteShapePreservation::PreserveArea);
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Translucent));
    TestFalse(TEXT("One translucent section rejects the whole mesh"), DaocZoneRendering::ConfigureNanite(*Mesh, false));
    TestTrue(TEXT("Rejected preparation does not change settings"), Mesh->GetNaniteSettings() == Prepared);
    Mesh->GetStaticMaterials().Pop();
    auto *Instance = NewObject<UMaterialInstanceConstant>();
    Instance->SetParentEditorOnly(Opaque);
    {
        FMaterialInstanceParameterUpdateContext Update(Instance);
        FMaterialInstanceBasePropertyOverrides Overrides;
        Overrides.bOverride_BlendMode = true;
        Overrides.BlendMode = BLEND_Translucent;
        Update.SetBasePropertyOverrides(Overrides);
    }
    Instance->UpdateOverridableBaseProperties();
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Instance));
    TestFalse(TEXT("Material instance blend overrides are respected"), DaocZoneRendering::CanUseNanite(*Mesh));
    Mesh->GetStaticMaterials().Pop();
    Opaque->MaterialDomain = MD_DeferredDecal;
    TestFalse(TEXT("Non-surface materials stay conventional"), DaocZoneRendering::CanUseNanite(*Mesh));
    Opaque->MaterialDomain = MD_Surface;
    Opaque->SetShadingModel(MSM_SingleLayerWater);
    TestFalse(TEXT("Single-layer water stays conventional"), DaocZoneRendering::CanUseNanite(*Mesh));
    return true;
}
#endif
