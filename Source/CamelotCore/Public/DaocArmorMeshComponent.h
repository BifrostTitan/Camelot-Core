#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "DaocArmorMeshComponent.generated.h"
/// Armor can reference skirt/socket bones unused by the body's base geometry.
UCLASS(Transient)
class CAMELOTCORE_API UDaocArmorMeshComponent : public USkeletalMeshComponent
{
    GENERATED_BODY()
  public:
    /// Adds armor attachment bones required when evaluating the leader pose LOD.
    virtual void GetAdditionalRequiredBonesForLeader(int32 LODIndex, TArray<FBoneIndexType> &Bones) const override;
};
