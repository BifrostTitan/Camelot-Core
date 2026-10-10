#pragma once
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "DaocCombatTypes.h"
#include "DaocDeveloperComponent.generated.h"

class UInputComponent;
class UStaticMeshComponent;
class SWidget;
class APlayerController;
/// Authorized developer controls for inspection, flight and administration.
UCLASS(ClassGroup = (DAoC), meta = (BlueprintSpawnableComponent))
class CAMELOTCORE_API UDaocDeveloperComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocDeveloperComponent();
    /// Binds authorized developer controls to the supplied local input component.
    void BindInput(UInputComponent *Input);
    /// Updates this component using the current frame delta and tick context.
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *TickFunction) override;
    /// Toggles authorized local developer flight.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Developer")
    void ToggleFlight();
    /// Toggles authorized fixture-inspection presentation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Developer")
    void ToggleInspector();
    /// Rebuilds the current zone renderer without changing server position.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Developer")
    void RegenerateZone();
    /// Toggles the authorized command panel and cursor/input focus.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Admin")
    void ToggleAdminPanel();
    /// Forwards an authorized command through the active session and records feedback.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Admin")
    bool ExecuteAdminCommand(const FString &Command);
    /// Closes administration interaction and restores local input state.
    void CloseAdminPanel();
    /// Returns region, coordinates and target context for the command panel.
    FString GetAdminContext() const;
    /// Returns the bounded command/response history for display.
    FString GetAdminLog() const;
    /// Cycles the administration target among currently known entities.
    void CycleAdminTarget();
    /// Reports whether authorized local flight is active.
    bool IsFlying() const;
    /// Reports whether fixture inspection is enabled.
    bool IsInspectorActive() const
    {
        return bInspecting;
    }
    /// Reports whether the administration panel owns input.
    bool IsAdminPanelOpen() const
    {
        return AdminPanel.IsValid();
    }
    /// Returns the latest developer inspection information for display.
    const FString &GetInformation() const
    {
        return Information;
    }
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Developer", meta = (ClampMin = "1", ClampMax = "511"))
    float FlightSpeed = 400; // DAoC units per second; server approves debug flight first.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Developer", meta = (ClampMin = "1"))
    float InspectRange = 8192; // DAoC units, converted using current zone scale.
  protected:
    /// Binds this object to its world/session when gameplay begins.
    virtual void BeginPlay() override;
    /// Releases runtime bindings and owned resources when gameplay ends.
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    /// Checks server acknowledgement or explicit offline developer authorization.
    bool Authorized() const;
    /// Opens the command panel only when developer access is authorized.
    void OpenAdminPanel();
    /// Appends one message to the bounded administration history.
    void AddAdminMessage(const FString &Text);
    /// Records relevant server command feedback in the administration panel.
    UFUNCTION()
    void HandleAdminEvent(const FDaocCombatEvent &Event);
    TSharedPtr<SWidget> AdminPanel;
    TWeakObjectPtr<APlayerController> AdminController;
    TArray<FString> AdminMessages;
    bool bPendingAdmin = false, bPreviousCursor = false;
    /// Applies vertical input while developer flight is active.
    void Vertical(float Value);
    /// Copies the inspected source NIF identity for authoring lookup.
    void CopyNif();
    /// Restores ordinary movement after developer flight ends.
    void StopFlight();
    /// Changes the fixture-inspection marker visibility.
    void SetHammerVisible(bool bVisible);
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> HammerHandle;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> HammerHead;
    bool bFlightActive = false, bPendingFlight = false, bPendingInspector = false, bPendingRegenerate = false, bInspecting = false;
    float PendingTime = 0, PickTime = 0;
    FString Information;
    FName HitNif;
    FVector LaserEnd = FVector::ZeroVector;
};
