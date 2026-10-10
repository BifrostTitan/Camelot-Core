#include "CamelotCharacterAnimInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"

struct FCamelotCharacterAnimProxy : FAnimSingleNodeInstanceProxy
{
    explicit FCamelotCharacterAnimProxy(UAnimInstance *Instance) : FAnimSingleNodeInstanceProxy(Instance)
    {
    }
    UAnimSequence *Source = nullptr;
    TArray<FTransform> Snapshot;
    TArray<FCamelotAnimationSample, TInlineAllocator<4>> Samples;
    float SourcePosition = 0, SourceRate = 1, Elapsed = 0, Duration = 0, ManualWeight = 1;
    bool SourceLoop = false, Manual = false;
    float Weight() const
    {
        return Manual ? ManualWeight : Duration > 0 ? FMath::Clamp(Elapsed / Duration, 0.f, 1.f)
                                                    : 1.f;
    }

    virtual void InitializeObjects(UAnimInstance *Instance) override
    {
        FAnimSingleNodeInstanceProxy::InitializeObjects(Instance);
        Source = CastChecked<UCamelotCharacterAnimInstance>(Instance)->BlendSource;
    }
    virtual void ClearObjects() override
    {
        FAnimSingleNodeInstanceProxy::ClearObjects();
        Source = nullptr;
    }
    virtual void PreUpdate(UAnimInstance *Instance, float DeltaSeconds) override
    {
        if (!Manual)
        {
            Elapsed += DeltaSeconds;
            SourcePosition += DeltaSeconds * SourceRate;
        }
        FAnimSingleNodeInstanceProxy::PreUpdate(Instance, DeltaSeconds);
    }
    virtual bool Evaluate(FPoseContext &Output) override
    {
        if (Manual && !Samples.IsEmpty())
        {
            float Total = 0;
            for (const auto &Sample : Samples)
            {
                if (!Sample.Clip || Sample.Weight <= 0)
                    continue;
                FAnimExtractContext Context(Sample.Position, false);
                Context.InterpolationOverride = EAnimInterpolationType::Linear;
                if (Total == 0)
                {
                    FAnimationPoseData Pose(Output);
                    Sample.Clip->GetAnimationPose(Pose, Context);
                }
                else
                {
                    FPoseContext Next(Output);
                    Next.ResetToRefPose();
                    FAnimationPoseData NextPose(Next), Pose(Output);
                    Sample.Clip->GetAnimationPose(NextPose, Context);
                    FAnimationRuntime::BlendTwoPosesTogetherInPlace(Pose, NextPose, Total / (Total + Sample.Weight));
                }
                Total += Sample.Weight;
            }
            if (Total == 0)
                Output.ResetToRefPose();
            Output.Pose.NormalizeRotations();
            return true;
        }
        FAnimSingleNodeInstanceProxy::Evaluate(Output);
        const float Alpha = Weight();
        if (Alpha >= 1 || (!Source && Snapshot.IsEmpty()))
            return true;
        FPoseContext Previous(Output);
        Previous.ResetToRefPose();
        if (!Snapshot.IsEmpty())
        {
            const auto &Bones = Previous.Pose.GetBoneContainer();
            for (FCompactPoseBoneIndex Bone : Previous.Pose.ForEachBoneIndex())
            {
                const int32 MeshBone = Bones.MakeMeshPoseIndex(Bone).GetInt();
                if (Snapshot.IsValidIndex(MeshBone))
                    Previous.Pose[Bone] = Snapshot[MeshBone];
            }
        }
        else
        {
            const float Length = Source->GetPlayLength();
            const float Position = SourceLoop && Length > 0 ? FMath::Fmod(SourcePosition, Length) : FMath::Clamp(SourcePosition, 0.f, Length);
            FAnimExtractContext Context(Position, false);
            Context.InterpolationOverride = EAnimInterpolationType::Linear;
            FAnimationPoseData Pose(Previous);
            Source->GetAnimationPose(Pose, Context);
        }
        FAnimationPoseData TargetPose(Output), SourcePose(Previous);
        FAnimationRuntime::BlendTwoPosesTogetherInPlace(TargetPose, SourcePose, Alpha);
        Output.Pose.NormalizeRotations();
        return true;
    }
};

FAnimInstanceProxy *UCamelotCharacterAnimInstance::CreateAnimInstanceProxy()
{
    return new FCamelotCharacterAnimProxy(this);
}
UCamelotCharacterAnimInstance *UCamelotCharacterAnimInstance::Ensure(USkeletalMeshComponent *Body)
{
    if (!Body)
        return nullptr;
    if (!Cast<UCamelotCharacterAnimInstance>(Body->GetAnimInstance()))
        Body->SetAnimInstanceClass(StaticClass());
    return Cast<UCamelotCharacterAnimInstance>(Body->GetAnimInstance());
}
bool UCamelotCharacterAnimInstance::IsBlending() const
{
    return GetProxyOnGameThread<FCamelotCharacterAnimProxy>().Weight() < 1;
}
void UCamelotCharacterAnimInstance::PlayBlended(UAnimSequence *Clip, bool Loop, float Rate, float Position, float BlendSeconds)
{
    auto &Proxy = GetProxyOnGameThread<FCamelotCharacterAnimProxy>();
    auto *Previous = Cast<UAnimSequence>(CurrentAsset);
    const float PreviousTime = GetCurrentTime(), PreviousRate = GetPlayRate();
    const bool PreviousLoop = IsLooping();
    // An interrupted blend starts at the actual displayed pose, including prior hits.
    TArray<FTransform> Snapshot;
    if (Previous && BlendSeconds > 0 && Proxy.Weight() < 1)
        Snapshot = GetSkelMeshComponent()->GetBoneSpaceTransforms();
    SetAnimationAsset(Clip, Loop, Rate);
    SetInterpolationOverride(EAnimInterpolationType::Linear);
    SetPosition(Position, false);
    SetPlaying(true);
    BlendSource = Previous;
    CrowdClips.Reset();
    Proxy.Samples.Reset();
    Proxy.Snapshot = MoveTemp(Snapshot);
    Proxy.SourcePosition = PreviousTime;
    Proxy.SourceRate = PreviousRate;
    Proxy.SourceLoop = PreviousLoop;
    Proxy.Elapsed = 0;
    Proxy.Duration = Previous ? FMath::Max(0.f, BlendSeconds) : 0;
    Proxy.Manual = false;
}
void UCamelotCharacterAnimInstance::SetCrowdPose(UAnimSequence *Target, float TargetPosition, UAnimSequence *Source, float SourcePosition, float TargetWeight)
{
    FCamelotAnimationSample Samples[] = {{Source, SourcePosition, 1 - TargetWeight}, {Target, TargetPosition, TargetWeight}};
    SetCrowdSamples(Samples);
}
void UCamelotCharacterAnimInstance::SetCrowdSamples(TConstArrayView<FCamelotAnimationSample> Samples)
{
    if (Samples.IsEmpty())
        return;
    const auto &Target = Samples.Last();
    if (CurrentAsset != Target.Clip)
        SetAnimationAsset(Target.Clip, false, 1);
    SetInterpolationOverride(EAnimInterpolationType::Linear);
    SetPosition(Target.Position, false);
    SetPlaying(false);
    BlendSource = nullptr;
    CrowdClips.Reset();
    for (const auto &Sample : Samples)
        if (Sample.Clip)
            CrowdClips.Add(Sample.Clip);
    auto &Proxy = GetProxyOnGameThread<FCamelotCharacterAnimProxy>();
    Proxy.Snapshot.Reset();
    Proxy.Samples.Reset();
    Proxy.Samples.Append(Samples);
    Proxy.Manual = true;
    Proxy.ManualWeight = FMath::Clamp(Target.Weight, 0.f, 1.f);
    Proxy.SourceLoop = false;
}
