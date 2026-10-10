#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DaocCharacterAppearance.h"
#include "DaocEquipmentComponent.generated.h"
class UDaocEquipmentCatalog;
class USkeletalMeshComponent;
class USceneComponent;
class USkeletalMesh;
/// Assembles modular equipment from confirmed character appearance.
UCLASS(ClassGroup = (DAoC), meta = (BlueprintSpawnableComponent))
class CAMELOTCORE_API UDaocEquipmentComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    /// Pick one complete geometry variant; mask bits are alternatives, not layers.
    static int32 SelectGeometryBit(int32 Mask, int32 Extension);
    /// Assembles compatible modular equipment and customization on the supplied body.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Equipment")
    void ApplyAppearance(USkeletalMeshComponent *Body, UDaocEquipmentCatalog *Catalog, const FDaocCharacterAppearance &Appearance);
    /// Releases assembled parts and clears retained appearance state.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Equipment")
    void ClearEquipment();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ArmorPartCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 WeaponCount = 0;
    /// Short-lived full-presentation cache for characters interrupting a GPU blend.
    /// Dormant parts do not tick or draw; ordinary crowd members still release gear.
    void SetPresentationActive(bool bActive);
    /// Reports whether recently used full equipment can be retained during a crowd transition.
    bool CanRetainForCrowd() const;

  protected:
    /// Releases component-owned presentation resources before destruction.
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
    /// Releases runtime bindings and owned resources when gameplay ends.
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<USceneComponent>> Parts;
    UPROPERTY(Transient)
    TWeakObjectPtr<USkeletalMeshComponent> BodyMesh;
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMesh> AppliedMesh;
    UPROPERTY(Transient)
    TObjectPtr<UDaocEquipmentCatalog> AppliedCatalog;
    uint32 AppliedHash = 0;
    bool bApplied = false;
    double LastFullPresentation = -1;
    bool bDormant = false;
    TArray<FName> DormantSockets;
    TSet<int32> WarnedMissing;
};
