#include "CamelotPlayerPreview.h"
#include "Animation/AnimSequence.h"
#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "DaocCharacterSizing.h"
#include "DaocCombatHUD.h"
#include "DaocEquipmentComponent.h"
#include "DaocNetworkSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
void SCamelotPlayerPreview::Construct(const FArguments &A)
{
    Owner = A._Owner;
    Session = A._Session;
    Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
    Brush.ImageSize = FVector2D(200, 250);
    ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(250)[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SBox).WidthOverride(200).HeightOverride(250)[SNew(SImage).Image(&Brush)]]]] + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).IsFocusable(false).Text(FText::FromString(TEXT("‹"))).OnClicked_Lambda([this] {
        Capture(-30);
        return FReply::Handled();
    })] + SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).IsFocusable(false).Text(FText::FromString(TEXT("›"))).OnClicked_Lambda([this] {
        Capture(30);
        return FReply::Handled();
    })]]];
}
SCamelotPlayerPreview::~SCamelotPlayerPreview()
{
    Release();
}
UTextureRenderTarget2D *SCamelotPlayerPreview::GetRenderTarget() const
{
    return Target.Get();
}
void SCamelotPlayerPreview::Release()
{
    Brush.SetResourceObject(nullptr);
    Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
    if (Actor.IsValid() && IsValid(Actor.Get()))
    {
        TInlineComponentArray<UMeshComponent *> Meshes(Actor.Get());
        for (auto *Mesh : Meshes)
            Mesh->SetTextureForceResidentFlag(false);
        Actor->Destroy();
    }
    Actor.Reset();
    Target.Reset();
    Body.Reset();
    Equipment.Reset();
    Camera.Reset();
    Revision = -1;
    ObjectId = INDEX_NONE;
}
void SCamelotPlayerPreview::SetActive(bool Active)
{
    bActive = Active;
    if (!Active)
        Release();
    else
        Refresh();
}
void SCamelotPlayerPreview::Refresh()
{
    if (!bActive)
        return;
    auto *Net = Session.IsValid() ? Session.Get() : Owner.IsValid() ? UDaocNetworkSubsystem::Get(Owner.Get())
                                                                    : nullptr;
    auto *World = Net ? Net->GetWorld() : nullptr;
    const auto *Appearance = Net && Net->bWorldReady ? Net->PlayerAppearances.Find(Net->PlayerObjectId) : nullptr;
    if (!Appearance || !World)
    {
        Release();
        return;
    }
    if (Revision == Appearance->Revision && ObjectId == Net->PlayerObjectId && Actor.IsValid())
        return;
    auto *Mesh = Net->ResolveCharacterMesh(Appearance->Model);
    if (!Mesh)
    {
        Release();
        return;
    }
    if (!Actor.IsValid())
    {
        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        Actor.Reset(World->SpawnActor<AActor>(FVector(0, 0, -200000), FRotator::ZeroRotator, Params));
        if (!Actor.IsValid())
            return;
        auto *Root = NewObject<USceneComponent>(Actor.Get());
        Actor->SetRootComponent(Root);
        Root->RegisterComponent();
        Root->SetWorldLocation(FVector(0, 0, -200000));
        Body = NewObject<USkeletalMeshComponent>(Actor.Get());
        Body->SetupAttachment(Root);
        Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Body->SetForcedLOD(1);
        Body->RegisterComponent();
        Body->SetComponentTickEnabled(false);
        Equipment = NewObject<UDaocEquipmentComponent>(Actor.Get());
        Equipment->RegisterComponent();
        for (int32 I = 0; I < 3; ++I)
        {
            auto *Light = NewObject<UPointLightComponent>(Actor.Get());
            Light->SetupAttachment(Root);
            Light->SetRelativeLocation(I == 0 ? FVector(150, -150, 230) : I == 1 ? FVector(-150, 100, 230)
                                                                                 : FVector(200, 200, 150));
            Light->SetIntensity(I == 0 ? 25000 : 18000);
            Light->SetSourceRadius(60);
            Light->SetAttenuationRadius(2000);
            Light->SetCastShadows(false);
            Light->RegisterComponent();
        }
        Target.Reset(NewObject<UTextureRenderTarget2D>(Actor.Get()));
        Target->ClearColor = FLinearColor(.012f, .018f, .028f, 1);
        Target->InitAutoFormat(512, 640);
        Camera = NewObject<USceneCaptureComponent2D>(Actor.Get());
        Camera->SetupAttachment(Root);
        Camera->TextureTarget = Target.Get();
        Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        Camera->ShowOnlyActors.Add(Actor.Get());
        Camera->FOVAngle = 42;
        Camera->bCaptureEveryFrame = false;
        Camera->bCaptureOnMovement = false;
        Camera->ShowFlags.SetMotionBlur(false);
        Camera->ShowFlags.SetDepthOfField(false);
        Camera->ShowFlags.SetGlobalIllumination(false);
        Camera->ShowFlags.SetFog(false);
        Camera->ShowFlags.SetAtmosphere(false);
        Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
        Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
        Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
        Camera->RegisterComponent();
        Brush.SetResourceObject(Target.Get());
        Brush.DrawAs = ESlateBrushDrawType::Image;
    }
    Equipment->ClearEquipment();
    Body->EmptyOverrideMaterials();
    Body->SetSkeletalMesh(Mesh);
    // A menu portrait uses its own stage scale, independent of the battlefield's
    // server-unit transform.
    Body->SetRelativeScale3D(FVector(DaocCharacterSizing::AppearanceScale(Appearance->Model, Appearance->ModelFlags)));
    Body->SetRelativeRotation(Net->CharacterMeshRotationOffset);
    const auto Path = FString::Printf(TEXT("/Game/DAoC/Animations/A_daoc_%d_Idle.A_daoc_%d_Idle"), Appearance->Model, Appearance->Model);
    if (auto *Idle = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn))
        if (Idle->GetSkeleton() == Mesh->GetSkeleton())
        {
            Body->PlayAnimation(Idle, true);
            Body->SetPosition(.5f, false);
            Body->TickAnimation(0, false);
            Body->RefreshBoneTransforms();
        }
    Equipment->ApplyAppearance(Body.Get(), Net->EquipmentCatalog, *Appearance);
    TInlineComponentArray<UMeshComponent *> Meshes(Actor.Get());
    for (auto *Part : Meshes)
        Part->SetTextureForceResidentFlag(true);
    Revision = Appearance->Revision;
    ObjectId = Net->PlayerObjectId;
    Capture();
}
void SCamelotPlayerPreview::Capture(float Rotation)
{
    if (!bActive || !Body.IsValid() || !Camera.IsValid())
        return;
    Angle += Rotation;
    const auto Bounds = Body->CalcBounds(Body->GetComponentTransform());
    const FVector Focus = Bounds.Origin;
    const float Distance = FMath::Max(1.f, Bounds.BoxExtent.Z * 3.5f);
    const FVector Eye = Focus + FVector(FMath::Cos(FMath::DegreesToRadians(Angle)) * Distance, FMath::Sin(FMath::DegreesToRadians(Angle)) * Distance, Bounds.BoxExtent.Z * .08f);
    Camera->SetWorldLocationAndRotation(Eye, (Focus - Eye).Rotation());
    Camera->CaptureScene();
}
void SCamelotPlayerPreview::Tick(const FGeometry &Geometry, double Time, float Delta)
{
    SCompoundWidget::Tick(Geometry, Time, Delta);
    if (!bActive || !Body.IsValid())
        return;
    CaptureElapsed += Delta;
    if (CaptureElapsed < 1.f / 15.f)
        return;
    Body->TickAnimation(FMath::Min(CaptureElapsed, .2f), false);
    Body->RefreshBoneTransforms();
    CaptureElapsed = 0;
    Capture();
}
