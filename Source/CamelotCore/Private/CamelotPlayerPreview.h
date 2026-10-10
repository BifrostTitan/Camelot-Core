#pragma once
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget2D.h"
class ADaocCombatHUD;
class AActor;
class USkeletalMeshComponent;
class UDaocEquipmentComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UDaocNetworkSubsystem;
/// On-demand player capture whose rendering resources exist only while active.
class SCamelotPlayerPreview : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SCamelotPlayerPreview) : _Owner(nullptr), _Session(nullptr)
    {
    }
    SLATE_ARGUMENT(ADaocCombatHUD *, Owner)
    SLATE_ARGUMENT(UDaocNetworkSubsystem *, Session)
    SLATE_END_ARGS()
    /// Builds the widget from its supplied Slate arguments and ownership references.
    void Construct(const FArguments &Args);
    /// Releases resources retained by this interface.
    virtual ~SCamelotPlayerPreview() override;
    /// Enables this view or releases resources that are only needed while visible.
    void SetActive(bool Active);
    /// Refreshes the preview mesh and equipment from the local player appearance.
    void Refresh();
    /// Refreshes the visible preview and advances its presentation clock.
    virtual void Tick(const FGeometry &, double CurrentTime, float DeltaTime) override;
    /// Returns the active preview texture, or nullptr while preview resources are released.
    UTextureRenderTarget2D *GetRenderTarget() const;

  private:
    /// Destroys the preview actor and releases its capture and render-target resources.
    void Release();
    /// Updates the player preview capture at the requested rotation.
    void Capture(float Rotation = 0);
    TWeakObjectPtr<ADaocCombatHUD> Owner;
    TWeakObjectPtr<UDaocNetworkSubsystem> Session;
    TStrongObjectPtr<AActor> Actor;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    TWeakObjectPtr<USkeletalMeshComponent> Body;
    TWeakObjectPtr<UDaocEquipmentComponent> Equipment;
    TWeakObjectPtr<USceneCaptureComponent2D> Camera;
    FSlateBrush Brush;
    bool bActive = false;
    int64 Revision = -1;
    int32 ObjectId = INDEX_NONE;
    float Angle = 0;
    float CaptureElapsed = 0;
};
