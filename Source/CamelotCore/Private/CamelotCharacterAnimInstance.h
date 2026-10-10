#pragma once

#include "Animation/AnimSingleNodeInstance.h"
#include "CamelotCharacterAnimInstance.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
/// One animation clip sample with playback position and blend weight.
struct FCamelotAnimationSample
{
    UAnimSequence *Clip = nullptr;
    float Position = 0, Weight = 1;
};

/// Native clip presentation for imported rigs. No animation Blueprint or new assets required.
UCLASS(Transient, NotBlueprintable)
class UCamelotCharacterAnimInstance : public UAnimSingleNodeInstance
{
    GENERATED_BODY()
  public:
    /// Returns the body's blending animation instance, creating and binding it when necessary.
    static UCamelotCharacterAnimInstance *Ensure(USkeletalMeshComponent *Body);
    /// Transitions to the requested clip while retaining the displayed source pose.
    void PlayBlended(UAnimSequence *Clip, bool Loop, float Rate, float Position, float BlendSeconds);
    /// Sample the same two poses/weight as a GPU crowd track for weapon sockets or promotion.
    void SetCrowdPose(UAnimSequence *Target, float TargetPosition, UAnimSequence *Source, float SourcePosition, float TargetWeight);
    /// Restores all weighted clip samples supplied by a crowd transition.
    void SetCrowdSamples(TConstArrayView<FCamelotAnimationSample> Samples);
    /// Reports whether the native animation transition is unfinished.
    bool IsBlending() const;

  protected:
    /// Creates the proxy that evaluates blended poses during Unreal animation updates.
    virtual FAnimInstanceProxy *CreateAnimInstanceProxy() override;

  private:
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> BlendSource;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAnimSequence>> CrowdClips;
    friend struct FCamelotCharacterAnimProxy;
};
