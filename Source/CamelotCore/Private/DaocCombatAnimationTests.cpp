#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "DaocCombatAnimationComponent.h"
#include "CamelotCharacterAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocCombatAnimationTest, "DAoC.Combat.AnimationPlayback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocCombatAnimationTest::RunTest(const FString &)
{
    FString Content;
    const bool Mounted = FParse::Value(FCommandLine::Get(), TEXT("CamelotTestContent="), Content);
    if (Mounted)
    {
        Content = FPaths::ConvertRelativePathToFull(Content);
        FPaths::NormalizeDirectoryName(Content);
        Content += TEXT("/");
        FPackageName::RegisterMountPoint(TEXT("/Game/"), Content);
    }
    ON_SCOPE_EXIT
    {
        if (Mounted)
            FPackageName::UnRegisterMountPoint(TEXT("/Game/"), Content);
    };
    auto *Catalog = LoadObject<UDaocCombatAnimationCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_CombatAnimations.DA_CombatAnimations"));
    if (!TestNotNull(TEXT("Shared combat catalog exists"), Catalog))
        return false;
    TestTrue(TEXT("Every race/gender family prepared"), Catalog->Models.Num() >= 36);
    TestTrue(TEXT("Style animation mappings loaded"), Catalog->Styles.Num() > 100);
    TestTrue(TEXT("Spell cast phase mappings loaded"), Catalog->Spells.Num() > 100);
    TestTrue(TEXT("Original client sounds imported"), Catalog->Sounds.Num() > 100);
    TestTrue(TEXT("Identical character variants share the action catalog"), Catalog->ModelAliases.Num() > 600);
    auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto *Actor = World->SpawnActor<AActor>();
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto *Body = NewObject<USkeletalMeshComponent>(Actor);
    Body->RegisterComponent();
    auto *Presenter = NewObject<UDaocCombatAnimationComponent>(Actor);
    Presenter->RegisterComponent();
    Presenter->Catalog = Catalog;
    for (int32 Model : {14, 34, 35, 479, 47, 9})
    {
        auto *Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Model, Model));
        if (!TestNotNull(TEXT("Combat model exists"), Mesh))
            continue;
        Body->SetSkeletalMesh(Mesh);
        Presenter->SetCharacter(Body, Model, 7);
        auto *Idle = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_Idle.A_daoc_%d_Idle"), Model, Model));
        FDaocCombatEvent E;
        E.SourceObjectId = 8;
        E.Kind = EDaocCombatEventKind::Cast;
        E.Duration = 3;
        E.EffectId = 65535;
        Presenter->HandleEvent(E);
        TestTrue(TEXT("Other entity's packet cannot animate this body"), Presenter->ActiveAction.IsEmpty());
        E.SourceObjectId = 7;
        E.Kind = EDaocCombatEventKind::Melee;
        Presenter->HandleEvent(E);
        TestEqual(TEXT("Server melee plays generic attack"), Presenter->ActiveAction, FString(TEXT("C_0")));
        TestNotNull(TEXT("Combat packet creates a playable animation instance"), Body->GetSingleNodeInstance());
        if (Body->GetSingleNodeInstance())
            TestNotNull(TEXT("Animation instance retains the actual combat asset"), Body->GetSingleNodeInstance()->GetCurrentAsset());
        Presenter->UpdateLocomotion(Idle, true, false, 0);
        TestEqual(TEXT("Locomotion cannot overwrite an attack"), Presenter->ActiveAction, FString(TEXT("C_0")));
        E.Kind = EDaocCombatEventKind::Cast;
        Presenter->HandleEvent(E);
        TestEqual(TEXT("Unknown spell uses shared casting start"), Presenter->ActiveAction, FString(TEXT("G_16")));
        Presenter->UpdateLocomotion(Idle, true, false, 0);
        TestEqual(TEXT("Casting is retained across movement ticks"), Presenter->ActiveAction, FString(TEXT("G_16")));
        E.Kind = EDaocCombatEventKind::SpellEffect;
        Presenter->HandleEvent(E);
        TestEqual(TEXT("Server spell effect plays casting finish"), Presenter->ActiveAction, FString(TEXT("G_18")));
        E.Kind = EDaocCombatEventKind::Interrupt;
        Presenter->HandleEvent(E);
        Presenter->UpdateLocomotion(Idle, true, false, 0);
        TestTrue(TEXT("Interrupt returns to locomotion"), Presenter->ActiveAction.IsEmpty());
        E.Kind = EDaocCombatEventKind::Death;
        E.TargetObjectId = 7;
        Presenter->HandleEvent(E);
        Presenter->UpdateLocomotion(Idle, true, false, 0);
        TestEqual(TEXT("Death holds its animation"), Presenter->ActiveAction, FString(TEXT("G_9")));
        E.Kind = EDaocCombatEventKind::Revive;
        Presenter->HandleEvent(E);
        Presenter->UpdateLocomotion(Idle, true, false, 0);
        TestTrue(TEXT("Revive resumes locomotion"), Presenter->ActiveAction.IsEmpty());
        auto Advance = [&](float Seconds) {
            World->TimeSeconds += Seconds;
            ++GFrameCounter;
            Presenter->TickComponent(Seconds, LEVELTICK_All, nullptr);
            Body->TickAnimation(Seconds, false);
            Body->RefreshBoneTransforms();
        };
        Advance(.3f);
        E.Kind = EDaocCombatEventKind::Melee;
        E.SourceObjectId = 8;
        E.TargetObjectId = 7;
        E.ResultCode = 10;
        Presenter->HandleEvent(E);
        TestEqual(TEXT("Idle hit reacts immediately"), Presenter->ActiveAction, FString(TEXT("C_3")));
        TestNotNull(TEXT("Live body uses the blended clip presenter"), Cast<UCamelotCharacterAnimInstance>(Body->GetAnimInstance()));
        Advance(.03f);
        const auto BeforeHit = Body->GetBoneSpaceTransforms();
        UAnimSequence *Reaction = nullptr;
        float Position = 0, Rate = 0;
        bool Loop = false, Action = false, Hold = false;
        uint64 BeforeRevision = 0, AfterRevision = 0;
        Presenter->GetPlaybackState(Reaction, Position, Loop, Rate, Action, Hold, BeforeRevision);
        Presenter->HandleEvent(E);
        Presenter->GetPlaybackState(Reaction, Position, Loop, Rate, Action, Hold, AfterRevision);
        TestTrue(TEXT("Repeated hit restarts without waiting for the previous reaction"), AfterRevision > BeforeRevision && Position < .001f);
        Body->TickAnimation(0, false);
        Body->RefreshBoneTransforms();
        const auto AfterHit = Body->GetBoneSpaceTransforms();
        bool Continuous = BeforeHit.Num() == AfterHit.Num();
        for (int32 I = 0; Continuous && I < BeforeHit.Num(); ++I)
            Continuous = BeforeHit[I].Equals(AfterHit[I], .001f);
        TestTrue(TEXT("Interrupting an unfinished hit blend preserves every displayed bone"), Continuous);
        Advance(.1f);
        TestFalse(TEXT("Reaction blend completes"), CastChecked<UCamelotCharacterAnimInstance>(Body->GetAnimInstance())->IsBlending());
        Presenter->GetPlaybackState(Reaction, Position, Loop, Rate, Action, Hold, BeforeRevision);
        Presenter->HandleEvent(E);
        for (int32 Hit = 0; Hit < 199; ++Hit)
            Presenter->HandleEvent(E);
        Presenter->GetPlaybackState(Reaction, Position, Loop, Rate, Action, Hold, AfterRevision);
        TestEqual(TEXT("Same-frame hit burst retains every confirmed revision"), AfterRevision - BeforeRevision, uint64(200));
        TestTrue(TEXT("Same-frame hits keep the newly restarted reaction at zero"), Position < .001f);
        const float ReactionLength = Reaction->GetPlayLength();
        E.SourceObjectId = 7;
        E.TargetObjectId = 8;
        Presenter->HandleEvent(E);
        Presenter->GetPlaybackState(Reaction, Position, Loop, Rate, Action, Hold, AfterRevision);
        const float AttackLength = Reaction->GetPlayLength();
        E.SourceObjectId = 8;
        E.TargetObjectId = 7;
        E.ResultCode = 1;
        Presenter->HandleEvent(E);
        E.ResultCode = 10;
        Presenter->HandleEvent(E);
        TestEqual(TEXT("Hits do not cut off the outgoing attack"), Presenter->ActiveAction, FString(TEXT("C_0")));
        // Advance the fixture's game clock without waiting in real time.
        for (float T = 0; T < AttackLength + .1f; T += .1f)
            Advance(.1f);
        TestEqual(TEXT("Only the newest reaction plays between attacks"), Presenter->ActiveAction, FString(TEXT("C_3")));
        for (float T = 0; T < ReactionLength + .2f; T += .1f)
        {
            Advance(.1f);
            Presenter->UpdateLocomotion(Idle, true, false, 0);
        }
        TestTrue(TEXT("Multiple attack-time hits do not leave a reaction backlog"), Presenter->ActiveAction.IsEmpty());
        E.SourceObjectId = 7;
        E.TargetObjectId = 8;
        Presenter->HandleEvent(E);
        Presenter->UpdateLocomotion(Idle, true, false, 0, true);
        auto *Ready = Presenter->GetCombatIdleClip();
        if (Ready)
        {
            TestTrue(TEXT("Melee updates the intended combat return while the attack plays"), Presenter->GetReturnLocomotionClip() == Ready);
            Advance(AttackLength + .2f);
            Presenter->UpdateLocomotion(Idle, true, false, 0, true);
            Presenter->GetPlaybackState(Reaction, Position, Loop, Rate, Action, Hold, AfterRevision);
            TestTrue(TEXT("Remote confirmed melee returns to looping combat idle"), Reaction == Ready && Loop && !Action);
            Advance(1);
            Presenter->UpdateLocomotion(Idle, true, false, 0, true);
            TestTrue(TEXT("Combat idle survives quiet time between swings"), Presenter->GetReturnLocomotionClip() == Ready);
            Advance(11);
            Presenter->UpdateLocomotion(Idle, true, false, 0, true);
            TestTrue(TEXT("Quiet NPC eventually leaves inferred combat stance"), Presenter->GetReturnLocomotionClip() == Idle);
            Presenter->UpdateLocomotion(Idle, true, true, 0);
            TestTrue(TEXT("Authoritative attack mode selects combat idle"), Presenter->GetReturnLocomotionClip() == Ready);
            Presenter->UpdateLocomotion(Idle, true, false, 0);
            TestTrue(TEXT("Authoritative attack stop returns to ordinary idle immediately"), Presenter->GetReturnLocomotionClip() == Idle);
        }
        E.SourceObjectId = 8;
        E.TargetObjectId = 7;
        Presenter->HandleEvent(E);
        E.Kind = EDaocCombatEventKind::Death;
        Presenter->HandleEvent(E);
        E.Kind = EDaocCombatEventKind::Melee;
        Presenter->HandleEvent(E);
        TestEqual(TEXT("Hits cannot restart a dead body's animation"), Presenter->ActiveAction, FString(TEXT("G_9")));
        const int32 SourceModel = Catalog->ModelAliases.Contains(Model) ? Catalog->ModelAliases.FindChecked(Model) : Model;
        for (const auto &ClipEntry : Catalog->Models.FindChecked(SourceModel).Actions)
        {
            auto *Clip = ClipEntry.Value.LoadSynchronous();
            if (!TestNotNull(TEXT("Catalog action resolves"), Clip))
                continue;
            const auto &SourceRig = Clip->GetSkeleton()->GetReferenceSkeleton();
            const auto &TargetRig = Mesh->GetRefSkeleton();
            bool Identical = SourceRig.GetNum() == TargetRig.GetNum();
            for (int32 Bone = 0; Identical && Bone < SourceRig.GetNum(); ++Bone)
                Identical = SourceRig.GetBoneName(Bone) == TargetRig.GetBoneName(Bone) && SourceRig.GetParentIndex(Bone) == TargetRig.GetParentIndex(Bone) && SourceRig.GetRefBonePose()[Bone].Equals(TargetRig.GetRefBonePose()[Bone], .0001);
            TestTrue(TEXT("Combat clips use their own rig or a validated identical variant"), Clip->GetSkeleton() == Mesh->GetSkeleton() || (Catalog->ModelAliases.Contains(Model) && Identical));
            for (int32 Bone = 0; Bone < Mesh->GetRefSkeleton().GetNum(); ++Bone)
            {
                FTransform Pose;
                Clip->GetBoneTransform(Pose, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(Clip->GetPlayLength() * .4), true);
                TestFalse(TEXT("Sampled combat pose is finite"), Pose.ContainsNaN());
            }
        }
    }
    TestEqual(TEXT("Two hand attacks use shared two hand action"), UDaocCombatAnimationComponent::AttackAction(0, Catalog, EDaocWeaponAnimation::TwoHand), 8);
    TestEqual(TEXT("Dual weapon packet selects dual attack"), UDaocCombatAnimationComponent::AttackAction(502, Catalog, EDaocWeaponAnimation::OneHand), 28);
    TestEqual(TEXT("Bow shoot packet selects release"), UDaocCombatAnimationComponent::AttackAction(503, Catalog, EDaocWeaponAnimation::Bow), 17);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
