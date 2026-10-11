#pragma once
#include "CoreMinimal.h"
#include "DaocCombatTypes.h"
#include "GameFramework/HUD.h"
#include "DaocCombatHUD.generated.h"

/// Gameplay HUD hosting Slate panels and bounded Canvas nameplates.
UCLASS(Config = Game)
class CAMELOTCORE_API ADaocCombatHUD : public AHUD
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    ADaocCombatHUD();
    /// Binds this object to its world/session when gameplay begins.
    virtual void BeginPlay() override;
    /// Updates gameplay panels, chat and trainer presentation from session state.
    virtual void Tick(float DeltaSeconds) override;
    /// Releases runtime bindings and owned resources when gameplay ends.
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /// Draws this HUD visible presentation from its current state snapshot.
    virtual void DrawHUD() override;
    /// Maps a Canvas hit-box click to its supported gameplay interaction.
    virtual void NotifyHitBoxClick(FName BoxName) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camelot|UI")
    TObjectPtr<class UCamelotUITheme> UITheme;
    /// Optional project-owned theme loaded once when gameplay begins.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Camelot|UI")
    TSoftObjectPtr<class UCamelotUITheme> DefaultUITheme;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Camelot|UI", meta = (ClampMin = "0.5", ClampMax = "1.5"))
    float UIScale = .75f;
    /// Text-only labels match the classic presentation. Target health remains in
    /// Status.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Camelot|UI")
    bool bShowNameplateDetails = false;
    /// Returns the clamped host-configured gameplay interface scale.
    float GetGameplayUIScale() const
    {
        return FMath::IsFinite(UIScale) ? FMath::Clamp(UIScale, .5f, 1.5f) : .75f;
    }
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camelot|UI")
    bool bShowNameplates = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camelot|UI", meta = (ClampMin = "0"))
    float NameplateDistance = 4000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camelot|UI", meta = (ClampMin = "0", ClampMax = "128"))
    int32 MaximumNameplates = 48;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camelot|UI", meta = (ClampMin = "1", ClampMax = "16"))
    int32 NameplateTraceBudget = 6;
    /// Returns the gameplay theme used to style this HUD.
    const UCamelotUITheme *GetUITheme() const;
    /// Returns the local player combat component, or nullptr when unavailable.
    class UDaocCombatComponent *GetCombat() const;
    /// Returns the local developer component, or nullptr when unavailable.
    class UDaocDeveloperComponent *GetDeveloper() const;
    /// Returns the combat snapshot cached for the current UI refresh.
    const FDaocCombatState &GetViewState() const
    {
        return ViewState;
    }
    /// Returns the visible target list cached for UI presentation.
    const TArray<FDaocCombatTarget> &GetViewTargets() const
    {
        return ViewTargets;
    }
    /// Focuses chat input and pauses local movement/camera typing input.
    void OpenChat();
    /// Closes text entry and restores gameplay cursor/input state.
    void CloseChat();
    /// Sends validated chat through the session; failure preserves the draft.
    bool SubmitChat(const FString &Text);
    /// Toggles the gameplay cursor for panel, slot and nameplate interaction.
    void ToggleUICursor();
    /// Toggles character-panel visibility and its cursor interaction state.
    void ToggleCharacterMenu();
    /// Requests interaction with the currently selected nearby NPC.
    void InteractWithTarget();
    /// Updates trainer visibility and restores the previous cursor state on closure.
    void SetTrainerOpen(bool bOpen);
    /// Closes training and stops unsent queued ranks; accepted purchases cannot be undone.
    void CloseTrainer();
    /// Refreshes visible gameplay panels from current component/session snapshots.
    void RefreshGameUI();
    /// Records the visible hotbar slot eligible for cursor-enabled clearing.
    void SetHoveredHotbarSlot(int32 Slot)
    {
        HoveredHotbarSlot = Slot;
    }
    /// Clears the hovered binding context when the matching widget is left.
    void ClearHoveredHotbarSlot(int32 Slot)
    {
        if (HoveredHotbarSlot == Slot)
            HoveredHotbarSlot = INDEX_NONE;
    }
    /// Clears one hovered visible binding while chat/admin input is inactive.
    void ClearHoveredBinding();
    /// Reports whether chat currently owns text input.
    bool IsChatOpen() const
    {
        return bChatOpen;
    }
    /// Reports whether gameplay UI is visible outside frontend/loading state.
    bool IsGameUIVisible() const;
    /// Retains mapped hotbar textures for widget lifetime and garbage collection.
    void RetainHotbarIcon(int32 Slot, class UTexture2D *Texture);

  private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FCamelotHUDZoneTravelTest;
#endif
    TSharedPtr<class SCamelotGameHUD> GameUI;
    FDaocCombatState ViewState;
    TArray<FDaocCombatTarget> ViewTargets;
    TArray<int32> NameplateCandidates;
    /// Cached scene-occlusion result for one nameplate candidate.
    struct FPlateVisibility
    {
        double CheckedAt = -1;
        bool bVisible = false;
    };
    TMap<int32, FPlateVisibility> PlateVisibility;
    float RefreshElapsed = 1;
    int32 NameplateTraceCursor = 0;
    bool bChatOpen = false, bCursorBeforeChat = false;
    bool bCursorBeforeMenu = false;
    bool bTrainerOpen = false, bCursorBeforeTrainer = false;
    int32 HoveredHotbarSlot = INDEX_NONE;
    UPROPERTY(Transient)
    TArray<TObjectPtr<class UTexture2D>> HotbarTextures;
};
