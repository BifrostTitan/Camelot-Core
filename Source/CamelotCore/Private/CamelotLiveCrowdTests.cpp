#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "AssetCompilingManager.h"
#include "CamelotCharacterAnimInstance.h"
#include "CamelotCrowdComponent.h"
#include "CamelotLiveCrowd.h"
#include "Components/SkeletalMeshComponent.h"
#include "DaocCombatAnimationComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PrimitiveSceneInfoData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamelotLiveCrowdTest, "Camelot.Crowd.LivePresentation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCamelotLiveCrowdTest::RunTest(const FString &)
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
    auto *Catalog = LoadObject<UCamelotLiveCrowdCatalog>(nullptr, TEXT("/Game/DAoC/Crowd/DA_LiveCrowdCatalog.DA_LiveCrowdCatalog"));
    auto *Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/DAoC/Characters/SK_daoc_14.SK_daoc_14"));
    if (!TestNotNull(TEXT("Saved live catalog"), Catalog) || !TestNotNull(TEXT("Prepared body"), Mesh))
        return false;
    const auto *Model = Catalog->Models.Find(14);
    if (!TestNotNull(TEXT("Prepared player family"), Model))
        return false;
    // A synchronous automation body does not pump the editor's per-frame
    // compilation finisher. Runtime deliberately falls back until it is ready.
    for (const auto &Entry : Model->MeshProviders)
        Entry.Value.LoadSynchronous();
    FAssetCompilingManager::Get().FinishAllCompilation();
    auto *Idle = Model->Idle.LoadSynchronous();
    auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto *Presenter = NewObject<UCamelotLiveCrowdPresenter>();
    Presenter->Initialize(World, Catalog);
    ON_SCOPE_EXIT
    {
        Presenter->Clear();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };
    auto *Owner = World->SpawnActor<AActor>();
    auto *Body = NewObject<USkeletalMeshComponent>(Owner);
    Owner->SetRootComponent(Body);
    Body->SetSkeletalMesh(Mesh);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->RegisterComponent();
    auto *Animation = NewObject<UDaocCombatAnimationComponent>(Owner);
    Animation->RegisterComponent();
    Animation->SetCharacter(Body, 14, 1);
    Animation->UpdateLocomotion(Idle, true, false, 0);
    FDaocCharacterAppearance A;
    A.Model = 14;
    TestTrue(TEXT("Saved providers route live idle"), Presenter->Present(1, Body, A, Animation, 0, 1, 0));
    auto FindBatch = [&]() -> UCamelotCrowdComponent * {
        for (TActorIterator<AActor> It(World); It; ++It)
            if (auto *Batch = It->FindComponentByClass<UCamelotCrowdComponent>())
                if (Batch->GetCharacterCount() > 0)
                    return Batch;
        return nullptr;
    };
    auto *Batch = FindBatch();
    if (!TestNotNull(TEXT("Live batch created"), Batch))
        return false;
    const int32 OriginalTrack = Batch->GetCharacterTrack(1);
    TestTrue(TEXT("Unrelated identity shares appearance"), Presenter->Present(2, Body, A, Animation, 0, 1, 0));
    TestEqual(TEXT("One exact appearance batch"), Presenter->GetBatchCount(), 1);
    auto NonVisual = A;
    NonVisual.Name = TEXT("Renamed");
    NonVisual.Level = 50;
    ++NonVisual.Revision;
    TestTrue(TEXT("Nonvisual changes retain cached appearance"), Presenter->HasAppearance(1, NonVisual));
    auto Changed = A;
    Changed.HairColor = 1;
    TestFalse(TEXT("Visual change invalidates cache even without a revision change"), Presenter->HasAppearance(1, Changed));
    auto Different = A;
    Different.HairColor = 1;
    TestFalse(TEXT("Appearance budget retains full presenter"), Presenter->Present(3, Body, Different, Animation, 0, 1, 0));
    TestEqual(TEXT("Budget fallback preserves existing track"), Batch->GetCharacterTrack(1), OriginalTrack);
    auto Unsupported = A;
    Unsupported.Model = 999999;
    TestFalse(TEXT("Unprepared model retains full presenter"), Presenter->Present(4, Body, Unsupported, Animation, 0, 32, 0));
    TestTrue(TEXT("Changed appearance routes to its own batch"), Presenter->Present(1, Body, Changed, Animation, 0, 32, 0));
    TestTrue(TEXT("New appearance becomes the cached identity"), Presenter->HasAppearance(1, Changed));
    TestFalse(TEXT("Old appearance no longer matches the identity"), Presenter->HasAppearance(1, A));
    Presenter->Clear();
    Presenter->Initialize(World, Catalog);
    FDaocCombatEvent Event;
    Event.Kind = EDaocCombatEventKind::Melee;
    Event.WeaponId = 4;
    int32 Successes = 0;
    float ClipLength = 0;
    for (int32 I = 0; I < 113; ++I)
    {
        const int32 Id = 1000 + I;
        Animation->SetCharacter(Body, 14, Id);
        Event.SourceObjectId = Id;
        Animation->HandleEvent(Event);
        UAnimSequence *Clip = nullptr;
        float Position = 0, Rate = 0;
        bool Loop = false, Action = false, Hold = false;
        uint64 Revision = 0;
        TestTrue(TEXT("Confirmed action has visual snapshot"), Animation->GetPlaybackState(Clip, Position, Loop, Rate, Action, Hold, Revision));
        if (!Clip)
            return false;
        ClipLength = Clip->GetPlayLength();
        Animation->RestorePlaybackPosition(ClipLength * I / 200.f);
        if (Presenter->Present(Id, Body, A, Animation, 0, 32, 0))
            ++Successes;
    }
    TestEqual(TEXT("Four loop families leave 112 independent action slots"), Successes, 112);
    TestEqual(TEXT("Exhaustion is exposed"), Presenter->GetPoolMissCount(), 1);
    TestFalse(TEXT("Exhausted identity stays full"), Presenter->IsInstanced(1112));
    TestTrue(TEXT("Fallback preserves current action position"), Body->GetSingleNodeInstance()->GetCurrentTime() > ClipLength * .4f);
    Batch = FindBatch();
    if (!TestNotNull(TEXT("Action batch remains"), Batch))
        return false;
    const int32 Track = Batch->GetCharacterTrack(1000);
    TestTrue(TEXT("Exhaustion did not restart existing action"), Track >= 16);
    Presenter->Clear();
    Presenter->Initialize(World, Catalog);
    Animation->SetCharacter(Body, 14, 9);
    Event.SourceObjectId = 9;
    Animation->HandleEvent(Event);
    TestTrue(TEXT("Action routes after reset"), Presenter->Present(9, Body, A, Animation, 0, 32, 0));
    Batch = FindBatch();
    if (!TestNotNull(TEXT("Loop test batch"), Batch))
        return false;
    UAnimSequence *Clip = nullptr;
    float Position = 0, Rate = 0;
    bool Loop = false;
    TestTrue(TEXT("GPU phase available for representation transition"), Batch->GetCharacterPlayback(9, Clip, Position, Loop, Rate));
    auto *Set = Model->MeshProviders.FindChecked(FSoftObjectPath(Mesh)).LoadSynchronous();
    int32 ClipIndex = INDEX_NONE;
    for (int32 I = 0; I < Set->Providers[0]->GetSequences().Num(); ++I)
        if (Set->Providers[0]->GetSequences()[I].Sequence == Clip)
            ClipIndex = I;
    TestTrue(TEXT("Looping actions use the prepared clip"), Batch->PlayCharacterAction(9, ClipIndex, 0, 0, 1, false, true));
    Batch->TickComponent(ClipLength * 3.5f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Casting or aim loop remains an action after clip end"), Batch->GetCharacterTrack(9) >= 16);
    TestTrue(TEXT("Loop GPU clock readable"), Batch->GetCharacterPlayback(9, Clip, Position, Loop, Rate));
    TestTrue(TEXT("Loop clock wraps"), Loop && Position < ClipLength);
    TestTrue(TEXT("Explicit interruption restores locomotion"), Batch->SetCharacterLocomotion(9, 0, true));
    TestEqual(TEXT("Interrupted action returns to loop track"), Batch->GetCharacterTrack(9), 0);
    // Separate characters must retain their own blend histories, even for the
    // same attack.
    TestTrue(TEXT("Second blend subject added"), Batch->AddCharacter(10, FTransform::Identity, 1));
    TestTrue(TEXT("First crowd attack blends from idle"), Batch->PlayCharacterAction(9, ClipIndex, 0, 0, 1, false, false, .12f));
    const int32 FirstBlendTrack = Batch->GetCharacterTrack(9);
    TestTrue(TEXT("Second crowd attack blends from its own idle phase"), Batch->PlayCharacterAction(10, ClipIndex, 1, 0, 1, false, false, .12f));
    TestTrue(TEXT("Different starting phases cannot share an unfinished blend"), Batch->GetCharacterTrack(10) != FirstBlendTrack);
    World->TimeSeconds += .04;
    ++GFrameCounter;
    UAnimSequence *Target = nullptr, *Source = nullptr;
    float TargetPosition = 0, SourcePosition = 0, Weight = 1;
    TestTrue(TEXT("Crowd blend exposes both clips for attachments/promotion"), Batch->GetCharacterBlend(9, Target, TargetPosition, Source, SourcePosition, Weight));
    TestTrue(TEXT("Crowd uses a real intermediate pose"), Target == Clip && Source == Idle && Weight > 0 && Weight < 1);
    TestEqual(TEXT("Saved crowd providers support interrupted blends"), Set->Providers[0]->GetNumLayers(), 4);
    TArray<UAnimSequence *> PoseClips;
    TArray<float> PosePositions, PoseWeights;
    auto SampleBody = [&]() {
        Batch->GetCharacterPose(9, PoseClips, PosePositions, PoseWeights);
        TArray<FCamelotAnimationSample, TInlineAllocator<4>> Samples;
        for (int32 I = 0; I < PoseClips.Num(); ++I)
            Samples.Add({PoseClips[I], PosePositions[I], PoseWeights[I]});
        UCamelotCharacterAnimInstance::Ensure(Body)->SetCrowdSamples(Samples);
        Body->TickAnimation(0, false);
        Body->RefreshBoneTransforms();
        return Body->GetBoneSpaceTransforms();
    };
    const auto BeforeRestart = SampleBody();
    TestTrue(TEXT("A double hit can interrupt the GPU blend immediately"), Batch->PlayCharacterAction(9, ClipIndex, 0, 0, 1, false, false, .08f));
    const auto AfterRestart = SampleBody();
    bool Continuous = BeforeRestart.Num() == AfterRestart.Num();
    for (int32 I = 0; Continuous && I < BeforeRestart.Num(); ++I)
        Continuous = BeforeRestart[I].Equals(AfterRestart[I], .001f);
    TestTrue(TEXT("Restart preserves the blended pose of every crowd bone"), Continuous);
    World->TimeSeconds += .02;
    ++GFrameCounter;
    Batch->TickComponent(.02f, LEVELTICK_All, nullptr);
    Batch->GetCharacterPose(9, PoseClips, PosePositions, PoseWeights);
    TestEqual(TEXT("Interrupted blend retains three contributing clips"), PoseClips.Num(), 3);
    float TotalWeight = 0;
    for (float W : PoseWeights)
        TotalWeight += W;
    TestTrue(TEXT("Crowd pose weights remain normalized"), FMath::IsNearlyEqual(TotalWeight, 1.f, .0001f));
    TestTrue(TEXT("Third hit stays in the bounded GPU blend"), Batch->PlayCharacterAction(9, ClipIndex, 0, 0, 1, false, false, .08f));
    World->TimeSeconds += .02;
    ++GFrameCounter;
    Batch->TickComponent(.02f, LEVELTICK_All, nullptr);
    const int32 BeforeOverflow = Batch->GetCharacterTrack(9);
    TestFalse(TEXT("More than four contributing poses requests continuous fallback"), Batch->PlayCharacterAction(9, ClipIndex, 0, 0, 1, false, false, .08f));
    TestEqual(TEXT("Overflow does not modify the visible pose"), Batch->GetCharacterTrack(9), BeforeOverflow);
    Batch->GetCharacterPose(9, PoseClips, PosePositions, PoseWeights);
    Animation->RestoreCrowdSamples(PoseClips, PosePositions, PoseWeights, false);
    TestTrue(TEXT("Promoted mixed pose uses native blending"), CastChecked<UCamelotCharacterAnimInstance>(Body->GetAnimInstance())->IsBlending());
    World->TimeSeconds += .15;
    ++GFrameCounter;
    Batch->TickComponent(.15f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Crowd attack smoothly returns to its loop"), Batch->SetCharacterLocomotion(9, 0, true, .14f));
    Batch->GetCharacterBlend(9, Target, TargetPosition, Source, SourcePosition, Weight);
    TestTrue(TEXT("Return to locomotion retains the outgoing attack"), Target == Idle && Source == Clip && Weight < 1);
    World->TimeSeconds += .2;
    ++GFrameCounter;
    Batch->TickComponent(.2f, LEVELTICK_All, nullptr);
    TestEqual(TEXT("Finished locomotion blend releases its temporary slot"), Batch->GetCharacterTrack(9), 0);
    Batch->RemoveCharacter(10);
    for (int32 I = 0; I < 112; ++I)
    {
        const int32 Id = 2000 + I;
        TestTrue(TEXT("Return-slot pressure subject added"), Batch->AddCharacter(Id, FTransform::Identity, 0));
        TestTrue(TEXT("Distinct attack fills an action slot"), Batch->PlayCharacterAction(Id, ClipIndex, 0, ClipLength * I / 200.f));
        TestTrue(TEXT("Independent return retains its temporary slot"), Batch->SetCharacterLocomotion(Id, 0, true, .14f));
    }
    TestTrue(TEXT("New action subject added to a full pool"), Batch->AddCharacter(2112, FTransform::Identity, 0));
    World->TimeSeconds += .2;
    ++GFrameCounter;
    TestTrue(TEXT("Expired movement blends are reclaimed before the next batch tick"), Batch->PlayCharacterAction(2112, ClipIndex, 0));
    TestEqual(TEXT("Reclaimed return uses its locomotion cohort"), Batch->GetCharacterTrack(2000), 0);
    TestEqual(TEXT("Reclaiming completed returns avoids a pool miss"), Batch->GetActionPoolMissCount(), 0);
    Batch->DestroyComponent();
    TestEqual(TEXT("Direct destruction releases instances"), Batch->GetCharacterCount(), 0);
    Presenter->Clear();
    Animation->ResetCombat();
    Animation->UpdateLocomotion(Idle, true, false, 0);
    auto *FullClip = Body->GetSingleNodeInstance()->GetCurrentAsset();
    Animation->SetSharedPresentation(true);
    Event.SourceObjectId = 9;
    Animation->HandleEvent(Event);
    TestTrue(TEXT("Shared combat leaves the hidden skeletal asset untouched"), Body->GetSingleNodeInstance()->GetCurrentAsset() == FullClip);
    bool Action = false, Hold = false;
    uint64 Revision = 0;
    TestTrue(TEXT("Shared combat still advances its confirmed playback state"), Animation->GetPlaybackState(Clip, Position, Loop, Rate, Action, Hold, Revision) && Action && Clip != FullClip);
    Animation->SetSharedPresentation(false);
    TestTrue(TEXT("Leaving shared presentation restores the confirmed action"), Body->GetSingleNodeInstance()->GetCurrentAsset() == Clip);
    TestTrue(TEXT("Leaving shared presentation preserves its elapsed phase"), FMath::IsNearlyEqual(Body->GetSingleNodeInstance()->GetCurrentTime(), Position, .01f));
    Animation->RestorePlaybackPosition(Clip ? Clip->GetPlayLength() * .4f : 0);
    Animation->SetSharedPresentation(false);
    TestTrue(TEXT("Full presentation restores the shared action asset"), Body->GetSingleNodeInstance()->GetCurrentAsset() == Clip);
    TestTrue(TEXT("Full presentation retains action phase"), Clip && FMath::IsNearlyEqual(Body->GetSingleNodeInstance()->GetCurrentTime(), Clip->GetPlayLength() * .4f, .001f));
    Presenter->Clear();
    Animation->ResetCombat();
    Animation->UpdateLocomotion(Idle, true, false, 0);
    World->TimeSeconds += .3;
    ++GFrameCounter;
    Body->TickAnimation(.3f, false);
    TestTrue(TEXT("Combat return subject enters crowd"), Presenter->Present(9, Body, A, Animation, 0, 32, 0));
    Animation->SetSharedPresentation(true);
    Event.SourceObjectId = 9;
    Event.TargetObjectId = 10;
    Animation->HandleEvent(Event);
    Animation->UpdateLocomotion(Idle, true, false, 0, true);
    auto *Ready = Animation->GetCombatIdleClip();
    TestNotNull(TEXT("Combat ready sequence prepared"), Ready);
    TestTrue(TEXT("Attack reserves the combat idle return cohort"), Presenter->Present(9, Body, A, Animation, 0, 32, 0));
    Batch = FindBatch();
    if (!Batch)
        return false;
    World->TimeSeconds += ClipLength + .2;
    ++GFrameCounter;
    Animation->UpdateLocomotion(Idle, true, false, 0, true);
    Presenter->Present(9, Body, A, Animation, 0, 32, 0);
    Batch->TickComponent(.2f, LEVELTICK_All, nullptr);
    World->TimeSeconds += .2;
    ++GFrameCounter;
    Batch->TickComponent(.2f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Combat idle uses a shared loop, not an action slot"), Batch->GetCharacterTrack(9) >= 12 && Batch->GetCharacterTrack(9) < 16);
    TestTrue(TEXT("Completed attack returns to the ready pose"), Batch->GetCharacterPlayback(9, Clip, Position, Loop, Rate) && Clip == Ready && Loop);
    World->TimeSeconds += 11;
    ++GFrameCounter;
    Animation->UpdateLocomotion(Idle, true, false, 0, true);
    Presenter->Present(9, Body, A, Animation, 0, 32, 0);
    World->TimeSeconds += .3;
    ++GFrameCounter;
    Batch->TickComponent(.3f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Expired inferred stance returns to ordinary idle"), Batch->GetCharacterPlayback(9, Clip, Position, Loop, Rate) && Clip == Idle && Loop && Batch->GetCharacterTrack(9) < 4);
    Presenter->Clear();
    Animation->SetCharacter(Body, 14, 8000);
    Animation->UpdateLocomotion(Idle, true, false, 0);
    Event.SourceObjectId = 8000;
    Event.TargetObjectId = 8001;
    Animation->HandleEvent(Event);
    Body->GetSceneData().SetLastRenderTimeOnScreen(World->GetTimeSeconds());
    TestFalse(TEXT("Visible unfinished blend stays on its continuous native pose"), Presenter->Present(8000, Body, A, Animation, 0, 32, 0));
    World->TimeSeconds += .3;
    ++GFrameCounter;
    Body->SetLastRenderTime(World->GetTimeSeconds());
    TestTrue(TEXT("Unticked native blend remains unfinished offscreen"), Animation->IsPresentationBlending());
    TestTrue(TEXT("Shadow rendering alone counts as recently rendered"), Body->WasRecentlyRendered());
    TestTrue(TEXT("Offscreen blend cannot pin a live character to full rendering"), Presenter->Present(8000, Body, A, Animation, 0, 32, 0));
    Batch = FindBatch();
    if (!Batch)
        return false;
    UAnimSequence *Expected = nullptr;
    float ExpectedPosition = 0;
    bool ExpectedAction = false, ExpectedHold = false;
    uint64 ExpectedRevision = 0;
    Animation->GetPlaybackState(Expected, ExpectedPosition, Loop, Rate, ExpectedAction, ExpectedHold, ExpectedRevision);
    TestTrue(TEXT("Offscreen handoff retains the confirmed action and phase"), Batch->GetCharacterPlayback(8000, Clip, Position, Loop, Rate) && Clip == Expected && FMath::IsNearlyEqual(Position, ExpectedPosition, .001f) && Batch->GetCharacterTrack(8000) >= 16);
    return true;
}
#endif
