#pragma once
#include "CoreMinimal.h"
#include "DaocCombatHUD.h"
#include "DaocFrontendTypes.h"
#include "DaocFrontendHUD.generated.h"
class UDaocFrontendTheme;
class UDaocNetworkSubsystem;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UDaocEquipmentComponent;
class SDaocFrontend;
class UMaterialInstanceDynamic;
/// Login, roster and character-creation interface with an orbitable player preview.
UCLASS()
class CAMELOTCORE_API ADaocFrontendHUD : public ADaocCombatHUD
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    ADaocFrontendHUD();
    /// Binds this object to its world/session when gameplay begins.
    virtual void BeginPlay() override;
    /// Updates frontend session feedback and character preview presentation.
    virtual void Tick(float DeltaSeconds) override;
    /// Draws this HUD visible presentation from its current state snapshot.
    virtual void DrawHUD() override;
    /// Releases runtime bindings and owned resources when gameplay ends.
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Frontend")
    TObjectPtr<UDaocFrontendTheme> ClassicTheme;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Frontend")
    TObjectPtr<UDaocFrontendTheme> EnhancedTheme;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Frontend")
    bool bEnhancedUI = true;
    /// Updates the frontend capture using the requested appearance snapshot.
    void Preview(const FDaocCharacterAppearance &Appearance);
    /// Applies preview camera orbit and zoom input.
    void Orbit(float Degrees, float Zoom);
    /// Selects face/body framing and preview visibility.
    void SetPreviewFocus(bool Face, bool Visible);
    /// Returns the material bound to the frontend character preview.
    UMaterialInstanceDynamic *GetPreviewMaterial() const
    {
        return PreviewMaterial;
    }
    /// Resolves the frontend presentation theme.
    UDaocFrontendTheme *GetTheme() const;
    /// Returns the game-instance session used by frontend operations.
    UDaocNetworkSubsystem *GetSession() const;

  private:
    /// Releases the frontend preview actor and capture resources.
    void ReleasePreview();
    UPROPERTY(Transient)
    TObjectPtr<AActor> PreviewActor;
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> PreviewBody;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> PreviewStage;
    UPROPERTY(Transient)
    TObjectPtr<UDaocEquipmentComponent> PreviewEquipment;
    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> PreviewCamera;
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> PreviewTexture;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;
    TSharedPtr<SDaocFrontend> Screen;
    bool bMenuInput = false;
    bool bCaptureRequested = false;
    bool bFacePreview = false;
    float CaptureElapsed = 0;
    float OrbitAngle = 0, ZoomFactor = 1;
};
