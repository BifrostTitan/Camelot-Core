#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "DaocEffectSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraFunctionLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocEffectBudgetTest, "DAoC.Effects.BudgetAndLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocEffectBudgetTest::RunTest(const FString &)
{
    auto *Prepared = LoadObject<UDaocEffectCatalog>(nullptr, TEXT("/Game/DAoC/Effects/DA_Effects.DA_Effects"));
    if (!TestNotNull(TEXT("Prepared effect catalog"), Prepared))
        return false;
    const auto *Sparkle = Prepared->Effects.Find(1);
    if (!TestTrue(TEXT("Classic hand sparkle is converted"), Sparkle && Sparkle->System && Sparkle->System->IsReadyToRun()))
        return false;
    UWorld::InitializationValues Settings;
    Settings.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto *Owner = World->SpawnActor<AActor>();
    auto *Anchor = NewObject<USceneComponent>(Owner);
    Owner->SetRootComponent(Anchor);
    Anchor->RegisterComponent();
    auto *Simulation = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sparkle->System, FVector::ZeroVector, FRotator::ZeroRotator, FVector::OneVector, false, true, ENCPoolMethod::None, false);
    if (TestNotNull(TEXT("Particle simulation component"), Simulation))
    {
        Simulation->SetForceSolo(true);
        Simulation->AdvanceSimulation(12, 1.f / 60.f);
        const auto Controller = Simulation->GetSystemInstanceController();
        auto *Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
        int32 ParticleCount = 0;
        if (Instance)
            for (const auto &Emitter : Instance->GetEmitters())
                ParticleCount += Emitter->GetNumParticles();
        AddInfo(FString::Printf(TEXT("Simulation age %.3f, particles %d"), Instance ? Instance->GetAge() : -1.f, ParticleCount));
        TestTrue(TEXT("Converted emitter produces live particles"), ParticleCount > 0);
        Simulation->DestroyComponent();
    }
    auto *Effects = World->GetSubsystem<UDaocEffectSubsystem>();
    if (!TestNotNull(TEXT("Game world creates the effect service"), Effects))
    {
        World->DestroyWorld(false);
        return false;
    }
    auto *Catalog = NewObject<UDaocEffectCatalog>();
    Catalog->Effects.Add(1, *Sparkle);
    Catalog->Effects[1].Duration = .1f;
    Catalog->Effects[1].ParticleCost = 32;
    FDaocSpellVisuals Visuals;
    Visuals.Gather = {1};
    Visuals.Release = {1};
    Visuals.Projectile = {1};
    Visuals.Hit = {1};
    Visuals.Miss = {1};
    Catalog->Spells.Add(1234, Visuals);
    Effects->SetCatalog(Catalog);
    Effects->MaxActiveSystems = 4;
    Effects->MaxParticles = 128;
    Effects->MaxStartsPerFrame = 2;
    FDaocCombatEvent Event;
    Event.Kind = EDaocCombatEventKind::Cast;
    Event.EffectId = 1234;
    Event.SourceObjectId = 50;
    Event.Duration = .5f;
    Effects->HandleCombatEvent(Event, Anchor, Anchor, true);
    TestEqual(TEXT("Confirmed cast queues its gather stage"), Effects->GetPendingCount(), 1);
    Event.Kind = EDaocCombatEventKind::Interrupt;
    Effects->HandleCombatEvent(Event, Anchor, Anchor, true);
    TestEqual(TEXT("Interrupt cancels queued gather"), Effects->GetPendingCount(), 0);
    Event.Kind = EDaocCombatEventKind::SpellEffect;
    Event.Duration = .25f;
    Event.ResultCode = 1;
    Effects->HandleCombatEvent(Event, Anchor, Anchor, true);
    TestEqual(TEXT("Release, projectile and delayed impact have distinct requests"), Effects->GetPendingCount(), 3);
    Effects->ResetEffects();
    TestEqual(TEXT("Zone/disconnect reset cancels delayed impacts"), Effects->GetPendingCount(), 0);
    Event.Duration = 0;
    for (int32 I = 0; I < 50; ++I)
    {
        Event.TargetObjectId = 2000 + I;
        Effects->HandleCombatEvent(Event, Anchor, Anchor, false);
    }
    TestEqual(TEXT("AoE coalesces caster release while retaining every target result"), Effects->GetPendingCount(), 51);
    Effects->ResetEffects();
    for (int32 I = 0; I < 1000; ++I)
    {
        Event.SourceObjectId = 1000 + I;
        Effects->HandleCombatEvent(Event, Anchor, Anchor, false);
    }
    TestEqual(TEXT("Thousand-caster burst cannot grow an unbounded queue"), Effects->GetPendingCount(), 1024);
    TestEqual(TEXT("Overflow is accounted for"), Effects->DroppedEffects, int64(976));
    Effects->Tick(.016f);
    TestTrue(TEXT("Per-frame work and particle reservation stay bounded"), Effects->ActiveSystems <= 2 && Effects->ReservedParticles <= 64);
    TestTrue(TEXT("Prepared lightweight systems actually start"), Effects->ActiveSystems > 0);
    for (int32 I = 0; I < 20; ++I)
        Effects->Tick(.016f);
    TestTrue(TEXT("Continuing the burst cannot exceed active-system budget"), Effects->ActiveSystems <= 4);
    TestTrue(TEXT("Continuing the burst cannot exceed particle budget"), Effects->ReservedParticles <= 128);
    Effects->ResetEffects();
    TestEqual(TEXT("Cleanup releases all particle reservations"), Effects->ReservedParticles, 0);
    TestEqual(TEXT("Cleanup releases all active systems"), Effects->ActiveSystems, 0);
    Effects->HandleCombatEvent(Event, Anchor, Anchor, false);
    Anchor->DestroyComponent();
    Effects->Tick(.016f);
    TestEqual(TEXT("Removed entities cannot receive delayed effects"), Effects->ActiveSystems, 0);
    TestEqual(TEXT("Removed entity requests are discarded"), Effects->GetPendingCount(), 0);
    Effects->SetCatalog(nullptr);
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
