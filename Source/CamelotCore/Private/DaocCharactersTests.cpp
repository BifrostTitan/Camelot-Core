#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocCharacterSkinTest, "DAoC.Characters.ImportedSkinning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocCharacterSkinTest::RunTest(const FString &)
{
    auto *Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/DAoC/Characters/SK_daoc_14.SK_daoc_14"));
    if (!TestNotNull(TEXT("Imported Briton skeletal mesh"), Mesh))
        return false;
    TestEqual(TEXT("Canonical hierarchy retained"), Mesh->GetRefSkeleton().GetNum(), 129);
    TestNotNull(TEXT("Skeleton asset retained"), Mesh->GetSkeleton());
    if (Mesh->GetMaterials().IsValidIndex(7))
    {
        auto *Hair = Mesh->GetMaterials()[7].MaterialInterface->GetMaterial();
        TestEqual(TEXT("Hair/beard texture cutouts are masked"), Hair->GetBlendMode(), BLEND_Masked);
        TestTrue(TEXT("Hair cutouts render from both sides"), Hair->TwoSided != 0);
    }
    else
        AddError(TEXT("Missing hair material slot"));
    TestTrue(TEXT("Correct character scale"), FMath::IsNearlyEqual(Mesh->GetImportedBounds().BoxExtent.Z * 2, 180., 1.));
    auto *Component = NewObject<USkeletalMeshComponent>();
    Component->SetSkeletalMesh(Mesh);
    auto *Render = Mesh->GetResourceForRendering();
    if (!Render || Render->LODRenderData.IsEmpty())
        return false;
    auto &LOD = Render->LODRenderData[0];
    auto &Weights = LOD.SkinWeightVertexBuffer;
    TestEqual(TEXT("Body and skin sections"), LOD.RenderSections.Num(), 8);
    int32 Triangles = 0;
    for (const auto &Section : LOD.RenderSections)
        Triangles += Section.NumTriangles;
    TestEqual(TEXT("Source topology survives base LOD build"), Triangles, 3214);
    if (!LOD.RenderSections.IsEmpty())
        TestEqual(TEXT("All Catacombs face and eye triangles retained"), LOD.RenderSections[0].NumTriangles, 790u);
    TestTrue(TEXT("Base LOD has no automatic reduction"), FMath::IsNearlyEqual(Mesh->GetLODInfo(0)->ReductionSettings.NumOfTrianglesPercentage, 1.0f));
    const int32 Head = Mesh->GetRefSkeleton().FindBoneIndex(TEXT("Bip01 Head"));
    if (Head == INDEX_NONE)
        return false;
    Component->SetComponentSpaceTransformsDoubleBuffering(false);
    auto &Pose = Component->GetEditableComponentSpaceTransforms();
    Pose = Mesh->GetRefSkeleton().GetRefBonePose();
    for (int32 I = 1; I < Pose.Num(); ++I)
        Pose[I] = Pose[I] * Pose[Mesh->GetRefSkeleton().GetParentIndex(I)];
    const FTransform HeadPose = Pose[Head];
    int32 Moved = 0, Unchanged = 0;
    float MaxRestError = 0, MaxMovedError = 0;
    for (uint32 V = 0; V < LOD.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices(); ++V)
    {
        const FVector3f Original = LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V);
        Pose[Head] = HeadPose;
        const auto Rest = USkinnedMeshComponent::GetSkinnedVertexPosition(Component, V, LOD, Weights);
        MaxRestError = FMath::Max(MaxRestError, (Rest - Original).Size());
        int32 Section, Local;
        LOD.GetSectionFromVertexIndex(V, Section, Local);
        float HeadWeight = 0;
        for (uint32 I = 0; I < Weights.GetMaxBoneInfluences(); ++I)
        {
            const auto LocalBone = Weights.GetBoneIndex(V, I);
            if (!LOD.RenderSections[Section].BoneMap.IsValidIndex(LocalBone))
                return false;
            if (LOD.RenderSections[Section].BoneMap[LocalBone] == Head)
                HeadWeight += Weights.GetBoneWeight(V, I) / 65535.f;
        }
        Pose[Head].AddToTranslation(FVector(0, 0, 10));
        const auto Deformed = USkinnedMeshComponent::GetSkinnedVertexPosition(Component, V, LOD, Weights);
        MaxMovedError = FMath::Max(MaxMovedError, (Deformed - (Original + FVector3f(0, 0, 10 * HeadWeight))).Size());
        if (HeadWeight > 0.01f)
            ++Moved;
        else
            ++Unchanged;
    }
    TestTrue(TEXT("Bind pose survives GPU weight quantization"), MaxRestError < 0.01f);
    TestTrue(TEXT("Head bone deforms vertices by their skin weights"), MaxMovedError < 0.01f);
    TestTrue(TEXT("Real head influences present"), Moved > 100);
    TestTrue(TEXT("Other body vertices remain unaffected"), Unchanged > 100);
    AddInfo(FString::Printf(TEXT("%d head vertices deform, %d other vertices "
                                 "remain; errors %.6f/%.6f"),
                            Moved, Unchanged, MaxRestError, MaxMovedError));
    return !HasAnyErrors();
}
#endif
