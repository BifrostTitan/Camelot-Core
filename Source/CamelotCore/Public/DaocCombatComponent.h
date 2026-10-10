#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DaocCombatTypes.h"
#include "DaocCombatComponent.generated.h"
class UDaocNetworkSubsystem;
class UInputComponent;

/// Reusable local combat controller; DOL/session state lives in the network component.
UCLASS(ClassGroup = (DAoC), meta = (BlueprintSpawnableComponent))
class CAMELOTCORE_API UDaocCombatComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocCombatComponent();
    /// Binds local combat input and state to the supplied session.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    void SetNetworkClient(UDaocNetworkSubsystem *Client);
    /// Returns the current state snapshot without simulating authoritative results.
    UFUNCTION(BlueprintPure, Category = "DAoC|Combat")
    FDaocCombatState GetState() const;
    /// Returns learned actions available through the bound session.
    UFUNCTION(BlueprintPure, Category = "DAoC|Combat")
    TArray<FDaocCombatAbility> GetLearnedAbilities() const;
    /// Returns the current session effect list for display and cancellation.
    UFUNCTION(BlueprintPure, Category = "DAoC|Combat")
    TArray<FDaocCombatEffect> GetEffects() const;
    /// Returns target snapshots from the bound network session.
    UFUNCTION(BlueprintPure, Category = "DAoC|Combat")
    TArray<FDaocCombatTarget> GetTargets() const;
    /// Selects a known target through the session interface.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool SelectTarget(int32 ObjectId);
    /// Requests a current learned action rather than predicting its combat result.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool ActivateAbility(const FDaocCombatAbility &Ability);
    /// Binds a current learned action to one local slot without sending a combat request.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool AssignHotbar(int32 Slot, const FDaocCombatAbility &Ability);
    /// Clears one local binding without removing the learned ability.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool ClearHotbar(int32 Slot);
    /// Requests the action currently assigned to the selected hotbar slot.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool ActivateHotbar(int32 Slot);
    /// Clamps visible hotbars to one through three while retaining independent bindings.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    void SetVisibleHotbarCount(int32 Count);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Combat")
    int32 VisibleHotbarCount = 1;
    /// Queues the requested attack mode through the active session.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool SetAttack(bool bEnabled);
    /// Sets the ground target using server coordinates and its visibility state.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool SetGroundTarget(FVector WorldPosition);
    /// Queues cancellation using the effect internal ID rather than its icon or list position.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool CancelEffect(int32 InternalId);
    /// Selects the next eligible target from the current local target snapshot.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    void CycleTarget();
    /// Selects the local character as the current target.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    void TargetSelf();
    /// Clears the current target through the session interface.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    void ClearTarget();
    /// Requests the opposite of the current reported attack mode.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    void ToggleAttack();
    /// Forwards the character release request through the session.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    void Release();
    /// Binds targeting, attack and hotbar shortcuts to the supplied input component.
    void BindDefaultInput(UInputComponent *Input);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Combat")
    bool bAutoFillHotbar = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Combat")
    TArray<FDaocCombatAbility> Hotbar;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Combat")
    FString LastFeedback;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Combat")
    TArray<FString> CombatMessages;
    /// Returns the bounded session-owned server chat history.
    UFUNCTION(BlueprintPure, Category = "DAoC|Chat")
    TArray<FDaocChatMessage> GetChatMessages() const;
    /// Returns the revision used to detect chat-history changes.
    int64 GetChatRevision() const;
    UPROPERTY(BlueprintAssignable, Category = "DAoC|Combat")
    FDaocCombatEventReceived OnCombatEvent;
    /// Updates this component using the current frame delta and tick context.
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *TickFunction) override;

  protected:
    /// Binds this object to its world/session when gameplay begins.
    virtual void BeginPlay() override;
    /// Releases runtime bindings and owned resources when gameplay ends.
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    /// Maps a keyboard slot through modifier keys to one of three independent hotbars.
    int32 KeyboardSlot(int32 Slot) const;
    /// Forwards a confirmed session event to component consumers.
    UFUNCTION()
    void HandleCombatEvent(const FDaocCombatEvent &Event);
    /// Reconciles local bindings with the current learned catalog.
    void RefreshHotbar();
    /// Activates keyboard hotbar slot 1 using the current modifier keys.
    void Slot0()
    {
        /// Requests the action currently assigned to the selected hotbar slot.
        ActivateHotbar(KeyboardSlot(0));
    }
    /// Activates keyboard hotbar slot 2 using the current modifier keys.
    void Slot1()
    {
        /// Requests the action currently assigned to the selected hotbar slot.
        ActivateHotbar(KeyboardSlot(1));
    }
    /// Activates keyboard hotbar slot 3 using the current modifier keys.
    void Slot2()
    {
        /// Requests the action currently assigned to the selected hotbar slot.
        ActivateHotbar(KeyboardSlot(2));
    }
    /// Activates keyboard hotbar slot 4 using the current modifier keys.
    void Slot3()
    {
        /// Requests the action currently assigned to the selected hotbar slot.
        ActivateHotbar(KeyboardSlot(3));
    }
    /// Activates keyboard hotbar slot 5 using the current modifier keys.
    void Slot4()
    {
        /// Requests the action currently assigned to the selected hotbar slot.
        ActivateHotbar(KeyboardSlot(4));
    }
    /// Activates keyboard hotbar slot 6 using the current modifier keys.
    void Slot5()
    {
        /// Requests the action currently assigned to the selected hotbar slot.
        ActivateHotbar(KeyboardSlot(5));
    }
    /// Activates keyboard hotbar slot 7 using the current modifier keys.
    void Slot6()
    {
        /// Requests the action currently assigned to the selected hotbar slot.
        ActivateHotbar(KeyboardSlot(6));
    }
    /// Activates keyboard hotbar slot 8 using the current modifier keys.
    void Slot7()
    {
        /// Requests the action currently assigned to the selected hotbar slot.
        ActivateHotbar(KeyboardSlot(7));
    }
    UPROPERTY(Transient)
    TObjectPtr<UDaocNetworkSubsystem> Network;
    int64 LastSkillsRevision = -1;
};
