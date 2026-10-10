#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocAnimationsTest, "DAoC.Characters.ImportedAnimations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocAnimationsTest::RunTest(const FString &)
{
    for (int32 Model : {14, 47, 479})
    {
        auto *Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Model, Model));
        if (!TestNotNull(TEXT("Model ID resolves skeletal mesh"), Mesh))
            return false;
        for (const TCHAR *Action : {TEXT("Idle"), TEXT("Walk"), TEXT("Run")})
        {
            auto *Sequence = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_%s.A_daoc_%d_%s"), Model, Action, Model, Action));
            if (!TestNotNull(TEXT("Model ID resolves animation"), Sequence))
                return false;
            TestTrue(TEXT("Clip uses model's skeleton"), Sequence->GetSkeleton() == Mesh->GetSkeleton());
            TestTrue(TEXT("Clip has original source duration"), Sequence->GetPlayLength() > 0.3 && Sequence->GetPlayLength() < 10);
            TestTrue(TEXT("Looping locomotion"), Sequence->bLoop);
            TestEqual(TEXT("Matched KFA track count"), Sequence->GetDataModel()->GetNumBoneTracks(), Model == 47 ? 34 : 106);
            int32 MovingBones = 0;
            for (int32 B = 0; B < Mesh->GetRefSkeleton().GetNum(); ++B)
            {
                FTransform Start, Middle;
                Sequence->GetBoneTransform(Start, FSkeletonPoseBoneIndex(B), FAnimExtractContext(0.), true);
                Sequence->GetBoneTransform(Middle, FSkeletonPoseBoneIndex(B), FAnimExtractContext(Sequence->GetPlayLength() * 0.4), true);
                if (!TestFalse(TEXT("Animation pose is finite"), Start.ContainsNaN() || Middle.ContainsNaN()))
                    return false;
                if (!Start.Equals(Middle, 0.001))
                    ++MovingBones;
            }
            TestTrue(TEXT("Source clip contains real bone motion"), MovingBones > 2);
            // Finite keys alone miss a wrong Euler order: it can turn a cloak
            // upward into a long spike. Check the actual skinned pose bounds.
            auto *Component = NewObject<USkeletalMeshComponent>();
            Component->SetSkeletalMesh(Mesh);
            Component->SetComponentSpaceTransformsDoubleBuffering(false);
            auto &Pose = Component->GetEditableComponentSpaceTransforms();
            Pose.SetNum(Mesh->GetRefSkeleton().GetNum());
            for (int32 B = 0; B < Pose.Num(); ++B)
            {
                Sequence->GetBoneTransform(Pose[B], FSkeletonPoseBoneIndex(B), FAnimExtractContext(0.35), true);
                const int32 Parent = Mesh->GetRefSkeleton().GetParentIndex(B);
                if (Parent != INDEX_NONE)
                    Pose[B] = Pose[B] * Pose[Parent];
            }
            auto &LOD = Mesh->GetResourceForRendering()->LODRenderData[0];
            FBox Bounds(ForceInit);
            for (uint32 V = 0; V < LOD.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices(); ++V)
                Bounds += FVector(USkinnedMeshComponent::GetSkinnedVertexPosition(Component, V, LOD, LOD.SkinWeightVertexBuffer));
            TestTrue(TEXT("Locomotion does not stretch geometry above the character"), Bounds.Max.Z < (Model == 47 ? 100 : 220));
            TestTrue(TEXT("Locomotion does not stretch geometry below ground"), Bounds.Min.Z > -30);
            AddInfo(FString::Printf(TEXT("Model %d %s: %d moving bones, %.3fs"), Model, Action, MovingBones, Sequence->GetPlayLength()));
        }
    }
    return !HasAnyErrors();
}
#endif
