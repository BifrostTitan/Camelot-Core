#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DaocCombatTypes.h"
#include "DaocEffectCatalog.h"
#include "DaocEffectSubsystem.generated.h"
class UNiagaraComponent;
class USceneComponent;
class UCamelotLiveCrowdPresenter;
struct FStreamableHandle;

/// Pending visual work is bounded and tied to live source/target components.
struct FDaocEffectRequest
{
    int32 Effect = 0, SourceId = 0, TargetId = 0;
    TWeakObjectPtr<USceneComponent> Anchor, Destination;
    double At = 0;
    float Travel = 0, Duration = 0;
    bool bLocal = false, bGather = false;
    bool bTargetAnchor = false;
    bool bRelease = false;
};
struct FDaocActiveEffect
{
    TWeakObjectPtr<UNiagaraComponent> Component;
    FDaocEffectRequest Request;
    FVector Origin = FVector::ZeroVector;
    double Started = 0, StopAt = 0, ReleaseAt = 0;
    int32 Cost = 0;
    bool bStopped = false;
};

/// Pooled, budgeted visual presentation for confirmed local and remote combat.
UCLASS(Config = Game)
class CAMELOTCORE_API UDaocEffectSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
  public:
    /// Loads the prepared catalog asynchronously before combat needs it.
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject *Outer) const override;
    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override;
    /// Maps a confirmed event to gather, release, projectile and result stages.
    void HandleCombatEvent(const FDaocCombatEvent &Event, USceneComponent *Source, USceneComponent *Target, bool bLocal, UCamelotLiveCrowdPresenter *Crowd = nullptr);
    /// Cancels current and delayed effects on disconnect or zone replacement.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Effects")
    void ResetEffects();
    /// Allows host projects and isolated tests to supply their own prepared catalog.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Effects")
    void SetCatalog(UDaocEffectCatalog *Prepared);
    UPROPERTY(Config, EditAnywhere, Category = "DAoC|Effects")
    TSoftObjectPtr<UDaocEffectCatalog> DefaultCatalog = TSoftObjectPtr<UDaocEffectCatalog>(FSoftObjectPath(TEXT("/Game/DAoC/Effects/DA_Effects.DA_Effects")));
    UPROPERTY(Config, EditAnywhere, Category = "DAoC|Effects", meta = (ClampMin = "1", ClampMax = "512"))
    int32 MaxActiveSystems = 128;
    UPROPERTY(Config, EditAnywhere, Category = "DAoC|Effects", meta = (ClampMin = "1", ClampMax = "262144"))
    int32 MaxParticles = 32768;
    UPROPERTY(Config, EditAnywhere, Category = "DAoC|Effects", meta = (ClampMin = "1", ClampMax = "128"))
    int32 MaxStartsPerFrame = 16;
    UPROPERTY(Config, EditAnywhere, Category = "DAoC|Effects", meta = (ClampMin = "100"))
    float MaxDistance = 7000;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Effects")
    int32 ActiveSystems = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Effects")
    int32 ReservedParticles = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Effects")
    int64 DroppedEffects = 0;
    /// Diagnostic queue size does not expose entity or character identifiers.
    int32 GetPendingCount() const
    {
        return Pending.Num();
    }

  private:
    void CatalogReady();
    void CancelGather(int32 ObjectId);
    void Queue(const TArray<int32> &Ids, const FDaocEffectRequest &Request);
    bool Start(const FDaocEffectRequest &Request, double Now);
    void Release(int32 Index);
    FVector AnchorLocation(USceneComponent *Anchor, FName Socket, int32 ObjectId = 0) const;
    TWeakObjectPtr<UCamelotLiveCrowdPresenter> CrowdPresenter;
    UPROPERTY(Transient)
    TObjectPtr<UDaocEffectCatalog> Catalog;
    TSharedPtr<FStreamableHandle> LoadHandle;
    TArray<FDaocEffectRequest> Pending;
    TArray<FDaocActiveEffect> Active;
    uint64 ReleaseFrame = MAX_uint64;
    TSet<uint64> ReleasesThisFrame;
};
