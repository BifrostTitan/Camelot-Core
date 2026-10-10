#include "DaocArmorMeshComponent.h"
#include "Engine/SkeletalMesh.h"
void UDaocArmorMeshComponent::GetAdditionalRequiredBonesForLeader(int32, TArray<FBoneIndexType> &Bones) const
{
    auto *Leader = LeaderPoseComponent.Get();
    auto *Mesh = GetSkeletalMeshAsset();
    if (!Leader || !Mesh)
        return;
    const auto &Skeleton = Mesh->GetRefSkeleton();
    for (int32 I = 0; I < Skeleton.GetNum(); ++I)
    {
        const int32 Index = Leader->GetBoneIndex(Skeleton.GetBoneName(I));
        if (Index != INDEX_NONE)
            Bones.AddUnique(static_cast<FBoneIndexType>(Index));
    }
    Bones.Sort();
}
