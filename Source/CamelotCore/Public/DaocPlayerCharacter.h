#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DaocPlayerCharacter.generated.h"
class USpringArmComponent;
class UCameraComponent;
class UDaocNetworkSubsystem;
class UAnimSequence;
class UDaocEquipmentComponent;
class UDaocCombatComponent;
class UDaocDeveloperComponent;
class UDaocCombatAnimationComponent;

/// Locally controlled character integrating movement, presentation and session requests.
UCLASS()
class CAMELOTCORE_API ADaocPlayerCharacter : public ACharacter
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    ADaocPlayerCharacter();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Developer")
    TObjectPtr<UDaocDeveloperComponent> Developer;
    /// Updates local movement, terrain following and character presentation.
    virtual void Tick(float DeltaSeconds) override;
    /// Binds local movement, camera and gameplay inputs to the supplied component.
    virtual void SetupPlayerInputComponent(UInputComponent *Input) override;
    /// Resolves local feet position against terrain and compatible collision surfaces.
    FVector ResolveWorldFeet(FVector Feet) const;
    /// Selects an imported body model; unavailable models retain the current visible body.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Player")
    bool SetCharacterModel(int32 ModelId);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Player")
    int32 OfflineModelId = 14;
    /// Normal DOL running speed; change to match server buffs/rules as needed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Player", meta = (ClampMin = "1", ClampMax = "511"))
    float ServerRunSpeed = 191;
    /// Distances use the unscaled character's centimeters, then follow zone scale.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DAoC|Camera", meta = (ClampMin = "1"))
    float CameraDistance = 400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Camera", meta = (ClampMin = "1"))
    float MinimumCameraDistance = 150;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Camera", meta = (ClampMin = "1"))
    float MaximumCameraDistance = 3000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Camera", meta = (ClampMin = "1"))
    float CameraZoomStep = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Camera", meta = (ClampMin = "0"))
    float CameraZoomSpeed = 10;
    /// Adjusts camera distance using the current world character scale.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Camera")
    void ZoomCamera(float WheelDelta);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Player")
    TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Player")
    TObjectPtr<UCameraComponent> FollowCamera;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Equipment")
    TObjectPtr<UDaocEquipmentComponent> EquipmentRenderer;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Animation")
    TObjectPtr<UDaocCombatAnimationComponent> CombatAnimations;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Combat")
    TObjectPtr<UDaocCombatComponent> Combat;

  protected:
    /// Binds this object to its world/session when gameplay begins.
    virtual void BeginPlay() override;
    /// Releases runtime bindings and owned resources when gameplay ends.
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    /// Applies forward/backward movement input when gameplay permits it.
    void Forward(float Value);
    /// Applies lateral movement input when gameplay permits it.
    void Right(float Value);
    /// Applies horizontal camera-look input.
    void LookYaw(float Value);
    /// Applies vertical camera-look input.
    void LookPitch(float Value);
    /// Applies the imported model and updates appearance scale, movement and camera sizing.
    void SetModel(int32 Id);
    /// Selects locomotion from current movement and confirmed character state.
    void UpdateAnimation();
    UPROPERTY(Transient)
    TObjectPtr<UDaocNetworkSubsystem> Network;
    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UAnimSequence>> Clips;
    TSet<int32> MissingModels;
    bool bCombatMovementDisabled = false;
    int32 CurrentModel = 0;
    int32 JoinedWorldRevision = -1;
    int32 JoinedObject = 0;
    float SendElapsed = 0;
    float WorldCharacterScale = 1;
    FVector LastSent = FVector::ZeroVector;
    FVector LastObserved = FVector::ZeroVector;
};
