#pragma once
#include "CoreMinimal.h"
#include "DaocCharacterAppearance.h"
#include "DaocCombatTypes.h"
#include "DaocFrontendTypes.h"
#include "DaocInventoryTypes.h"
#include "DaocTrainerTypes.h"
#include "DaocWeatherTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "DaocNetworkSubsystem.generated.h"

class UDaocEquipmentCatalog;
class UDaocEquipmentComponent;
class FCamelotConnection;
class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMesh;
class UAnimSequence;
class USceneComponent;
class UPrimitiveComponent;
/// Publishes session connection state and any associated error description.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDaocConnectionChanged, const FString &, State, const FString &, Error);

/// DOL-authoritative session owned by the game instance; persists across map
/// travel.
UCLASS(Config = Game)
class CAMELOTCORE_API UDaocNetworkSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocNetworkSubsystem();
    /// A possessed local pawn owns its rendering.
    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> LocalPlayerPawn;
    /// Releases resources retained by this interface.
    virtual ~UDaocNetworkSubsystem() override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network")
    bool bAutoConnect = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Login")
    bool bUseFrontend = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|World")
    bool bManageZoneTravel = true;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|World")
    bool bWorldReady = false;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|World")
    int32 WorldRevision = 0;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|World")
    int32 ActiveZoneId = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|World")
    FName GameplayMap = TEXT("/Game/DAoC/Maps/DAoCGameplay");
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Login")
    bool bCharactersReady = false;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Login")
    int32 RosterRevision = 0;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Login")
    int32 AllowedRealm = -1;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Login")
    TArray<FDaocCharacterSlot> CharacterSlots;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Login")
    FString CheckedCharacterName;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Login")
    int32 NameCheckResult = -1;
    /// Requests an allowed realm and a refreshed character roster.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Login")
    bool RequestRealm(int32 NewRealm);
    /// Requests authoritative availability checks before creating a character.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Login")
    bool CheckCharacterName(const FString &Name);
    /// Sends a validated creation draft; the server determines whether creation succeeds.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Login")
    bool CreateCharacter(const FDaocCreationRequest &Request);
    /// Requests deletion of the named roster entry and waits for a roster refresh.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Login")
    bool DeleteCharacter(const FString &Name);

    /// Numeric IPv4 avoids blocking DNS on the game thread.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network")
    FString ServerAddress = TEXT("127.0.0.1");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network", meta = (ClampMin = "1", ClampMax = "65535"))
    int32 ServerPort = 10300;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network")
    FString Username;
    /// SetCredentials, CAMELOT_PASSWORD, or [Camelot.Network] Password in
    /// Game.ini; never saved in the level.
    UPROPERTY(Transient, BlueprintReadWrite, Category = "DAoC|Network")
    FString Password;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network")
    FString CharacterName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network", meta = (ClampMin = "1", ClampMax = "3"))
    int32 Realm = 1;
    /// 1.74 transmits Catacombs customization, ten character slots and armor
    /// extensions.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network", meta = (ClampMin = "68", ClampMax = "74"))
    int32 ClientVersion = 74;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network", meta = (ClampMin = "1"))
    float RequestTimeoutSeconds = 20.f;
    /// Absolute zone offsets from DOL's zones.xml; renderer fixtures are
    /// zone-local.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network")
    FVector2D ServerZoneOrigin = FVector2D(548864, 483328);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network")
    TObjectPtr<UStaticMesh> DummyPlayerMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network")
    FVector DummyPlayerScale = FVector(0.6, 0.6, 1.8);
    /// Explicit model IDs from monsters.csv. Unmapped models retain the position
    /// marker.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Characters")
    TMap<int32, TObjectPtr<USkeletalMesh>> CharacterMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Catalog")
    TObjectPtr<class UDaocClientAssetCatalog> ClientAssetCatalog;
    /// Resolves an imported character mesh by server model ID; returns nullptr when unavailable.
    USkeletalMesh *ResolveCharacterMesh(int32 Model);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Characters")
    bool bLoadImportedCharacterMeshes = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Characters")
    bool bPlayImportedAnimations = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Equipment")
    TObjectPtr<UDaocEquipmentCatalog> EquipmentCatalog;
    /// DAoC server speed units per second. Slower movement selects Walk.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Characters", meta = (ClampMin = "1"))
    float RunAnimationSpeed = 120.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Characters")
    FVector CharacterMeshScale = FVector::OneVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Characters")
    FRotator CharacterMeshRotationOffset = FRotator(0, -90, 0);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Characters")
    TArray<FDaocCharacterAppearance> AccountCharacters;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Characters")
    TMap<int32, FDaocCharacterAppearance> PlayerAppearances;
    UPROPERTY(BlueprintAssignable, Category = "DAoC|Characters")
    FDaocAppearanceChanged OnAppearanceChanged;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Network")
    float DummyCenterHeight = 90.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Network")
    FString ConnectionState = TEXT("Disconnected");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Network")
    FString LastNetworkError;
    UPROPERTY(BlueprintAssignable, Category = "DAoC|Network")
    FDaocConnectionChanged OnConnectionChanged;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Network")
    FVector ServerPlayerPosition = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Network")
    int32 PlayerObjectId = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Network")
    int32 ReplicatedPlayerCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Network")
    int32 ReplicatedNPCCount = 0;
    /// Sets session credentials and selected character without persisting the secret to assets.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Network")
    void SetCredentials(const FString &Account, const FString &Secret, const FString &Character);
    /// Starts transport initialization using the configured credentials and protocol version.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Network")
    void Connect();
    /// Closes the connection and clears session-owned gameplay state.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Network")
    void Disconnect();
    /// Absolute DAoC coordinates. Returns false outside this zone or before
    /// Playing.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Network")
    bool SendPlayerPosition(FVector Position, int32 Heading, int32 Speed = 0, bool bFlying = false);
    /// Requests developer authorization; the server decides whether to grant it.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Developer")
    bool RequestDeveloperMode(bool bEnabled);
    /// This is the server acknowledgement, never a locally asserted privilege.
    UFUNCTION(BlueprintPure, Category = "DAoC|Developer")
    bool IsDeveloperModeAuthorized() const;
    /// DAoC server units; zero disables distance culling. Network state is always
    /// retained.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "DAoC|Performance", meta = (ClampMin = "0"))
    float PlayerRenderDistance = 12000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "DAoC|Performance", meta = (ClampMin = "0"))
    float MobRenderDistance = 10000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "DAoC|Crowd")
    bool bUseLiveCrowd = true;
    /// Local player and selected target always use full presentation. This
    /// optional server-unit radius protects additional nearby characters; zero
    /// batches all others.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "DAoC|Crowd", meta = (ClampMin = "0"))
    float FullCharacterDistance = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "DAoC|Crowd", meta = (ClampMin = "1", ClampMax = "64"))
    int32 LiveCrowdMaxBatches = 32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "DAoC|Crowd", meta = (ClampMin = "0", ClampMax = "0.1"))
    float LiveCrowdPhaseTolerance = .04f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Crowd")
    TObjectPtr<class UCamelotLiveCrowdCatalog> LiveCrowdCatalog;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Crowd")
    int32 LiveCrowdCharacterCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Crowd")
    int32 LiveCrowdBatchCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Crowd")
    int32 LiveCrowdPoseCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAoC|Crowd")
    int32 LiveCrowdPoolMissCount = 0;
    /// Reports whether the replicated object currently uses GPU crowd presentation.
    bool IsCrowdRendered(int32 ObjectId) const;
    /// Converts absolute server coordinates through the active zone origin and renderer transform.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Network")
    FVector ServerToWorld(FVector Position) const;
    /// Converts Unreal world position back to absolute server coordinates.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Network")
    FVector WorldToServer(FVector Position) const;
    /// Generated meshes and animations are imported at 2.54 cm per NIF unit.
    /// Convert them to the same NIF-unit scale used by zone fixtures. Custom
    /// assets retain their supplied scale.
    FVector GetCharacterScale(const USkeletalMesh *Mesh) const;
    /// Combines mesh unit conversion with authored model and height scaling.
    FVector GetAppearanceScale(const USkeletalMesh *Mesh, const FDaocCharacterAppearance &Appearance) const;
    /// Copies the confirmed combat state into its Blueprint-facing snapshot.
    UFUNCTION(BlueprintPure, Category = "DAoC|Combat")
    FDaocCombatState GetCombatState() const;
    /// Copies confirmed inventory and detail state with its session-specific revision.
    UFUNCTION(BlueprintPure, Category = "DAoC|Inventory")
    FDaocInventoryState GetInventoryState() const;
    /// Copies trainer choices, balances and affordable allocation limits for the UI.
    UFUNCTION(BlueprintPure, Category = "DAoC|Training")
    FDaocTrainerState GetTrainerState() const;
    /// Requests interaction with the selected nearby NPC using classic interaction fields.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Training")
    bool RequestInteract(int32 ObjectId);
    /// Queues a revision-checked allocation, advancing each rank only after server confirmation.
    /// @param ExpectedRevision Snapshot revision returned by the matching state getter; stale revisions are rejected.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Training")
    bool RequestTraining(bool bRealmAbility, int32 Index, int32 DesiredRank, int64 ExpectedRevision);
    /// Closes training and stops unsent queued ranks; accepted purchases cannot be undone.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Training")
    void CloseTrainer();
    /// ExpectedRevision rejects stale drag/drop or actions after an item/session
    /// changes. Count zero moves the whole stack.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Inventory")
    bool RequestMoveItem(int32 FromSlot, int32 ToSlot, int32 Count, int64 ExpectedRevision);
    /// Requests a revision-checked item action; eligibility and effects remain server-owned.
    /// @param ExpectedRevision Snapshot revision returned by the matching state getter; stale revisions are rejected.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Inventory")
    bool RequestUseItem(int32 Slot, int32 UseType, int64 ExpectedRevision);
    /// Serializes a revision-checked item-detail request because replies carry no request ID.
    /// @param ExpectedRevision Snapshot revision returned by the matching state getter; stale revisions are rejected.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Inventory")
    bool RequestInspectItem(int32 Slot, int64 ExpectedRevision);
    /// Returns learned actions preserving server catalog identity.
    UFUNCTION(BlueprintPure, Category = "DAoC|Combat")
    TArray<FDaocCombatAbility> GetCombatAbilities() const;
    /// Returns effects currently reported by the server.
    UFUNCTION(BlueprintPure, Category = "DAoC|Combat")
    TArray<FDaocCombatEffect> GetCombatEffects() const;
    /// Builds gameplay target snapshots from current replicated presentation state.
    UFUNCTION(BlueprintPure, Category = "DAoC|Combat")
    TArray<FDaocCombatTarget> GetCombatTargets() const;
    /// Requests selection of a known target with the current visibility result.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool SelectCombatTarget(int32 ObjectId);
    /// Requests an attack-state change; damage and eligibility remain server-owned.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool RequestAttack(bool bEnabled);
    /// Requests release of the dead character through the server.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool RequestRelease();
    /// Requests a learned action using the acting pawn position and visibility context.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool RequestAbility(const FDaocCombatAbility &Ability, AActor *ActingPawn);
    /// Converts the world ground target and queues its classic request.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool SetCombatGroundTarget(FVector WorldPosition);
    /// Requests cancellation by effect internal ID when the server permits it.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Combat")
    bool RequestCancelEffect(int32 InternalId);
    UPROPERTY(BlueprintAssignable, Category = "DAoC|Combat")
    FDaocCombatEventReceived OnCombatEvent;
    // Latest client-submitted pose, including actions sent between periodic
    // movement updates.
    FVector LastSubmittedPosition = FVector::ZeroVector;
    /// Returns the decoded server weather front without selecting its rain/snow artwork.
    UFUNCTION(BlueprintPure, Category = "DAoC|Weather")
    FDaocServerWeather GetServerWeather() const;
    /// Queues an administration command only for an authorized session.
    /// @return Whether the request was queued, not whether the server accepted the operation.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Admin")
    bool SendAdminCommand(const FString &Command);
    /// Ordinary chat/commands retain the server's normal privilege checks.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Chat")
    bool SendChat(const FString &Text);
    /// Returns the bounded session-owned server chat history.
    UFUNCTION(BlueprintPure, Category = "DAoC|Chat")
    TArray<FDaocChatMessage> GetChatMessages() const
    {
        return ChatMessages;
    }
    /// Returns the revision used to detect chat-history changes.
    int64 GetChatRevision() const
    {
        return ChatRevision;
    }
    /// Resolves a visible registered character body into its world-space label anchor.
    bool GetNameplateAnchor(int32 ObjectId, FVector &Position) const;
    /// Returns the current server region ID, which is distinct from the zone ID.
    UFUNCTION(BlueprintPure, Category = "DAoC|Admin")
    int32 GetServerRegionId() const;
    /// Initializes session rendering against the supplied zone and world scale.
    void StartForZone(int32 ZoneId, float RendererScale);
    /// Creates game-instance session state and registers world lifecycle callbacks.
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    /// Releases subsystem bindings and transient resources during shutdown.
    virtual void Deinitialize() override;
    /// Polls transport, advances protocol deadlines and updates session presentation on the game thread.
    virtual void Tick(float DeltaTime) override;
    /// Returns the Unreal profiling identifier for this tickable object.
    virtual TStatId GetStatId() const override;
    /// Reports whether this object currently participates in world ticking.
    virtual bool IsTickable() const override;
    /// Reports whether session polling continues while gameplay is paused.
    virtual bool IsTickableWhenPaused() const override
    {
        return true;
    }
    /// Returns the active world used to schedule session ticks.
    virtual UWorld *GetTickableGameObjectWorld() const override
    {
        /// Returns the world currently associated with this game-instance subsystem.
        return GetWorld();
    }
    /// Returns the world currently associated with this game-instance subsystem.
    virtual UWorld *GetWorld() const override;
    /// Binds the session presentation to the current renderer transform and world scale.
    void SetRenderingContext(UWorld *World, const FTransform &Transform)
    {
        RenderWorld = World;
        RenderTransform = Transform;
    }
    /// Returns the active actor that owns session zone rendering.
    AActor *GetRenderHost() const
    {
        return RenderHost.Get();
    }
    /// Reads explicit private login settings from the supplied configuration file.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Network")
    bool LoadLoginConfig(const FString &File);
    /// Requests world entry for the named server roster character.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Network")
    bool SelectCharacter(const FString &Name);
    /// Finds the game-instance session for a world context; returns nullptr when unavailable.
    UFUNCTION(BlueprintPure, Category = "DAoC|Network", meta = (WorldContext = "Context"))
    static UDaocNetworkSubsystem *Get(const UObject *Context);
    /// Renderer supplies spatial placement only; no credentials or socket
    /// ownership.
    FVector GetRenderScale() const
    {
        return RenderTransform.GetScale3D();
    }
    /// Updates the renderer transform used for world/server conversion.
    void SetRenderTransform(const FTransform &Transform)
    {
        RenderTransform = Transform;
    }

  protected:
  private:
    /// Loads configured startup values and supported command-line overrides.
    void LoadStartupSettings();
    /// Releases transient session presentation when the world or renderer changes.
    void ClearRenderState();
    /// Finds or creates the transient actor hosting replicated character rendering.
    bool EnsureRenderHost();
    /// Applies replicated transforms and appearance to visible character presenters.
    void RefreshPlayers();
    /// Seats ground mobs on rendered terrain or cached collision floors; flying mobs retain altitude.
    FVector ResolveMobFeet(FVector WorldFeet, bool bFlying);
    /// Cached collision-floor sample used to place replicated ground mobs.
    struct FMobFloorSample
    {
        FVector Point = FVector::ZeroVector;
        FVector Normal = FVector::UpVector;
        TWeakObjectPtr<UPrimitiveComponent> Component;
        double ExpiresAt = 0;
    };
    // Shared spatial samples keep bridge/floor checks bounded in large crowds.
    TMap<FIntVector, FMobFloorSample> MobFloorSamples;
    int32 MobFloorQueriesRemaining = 0;
    /// Resolves server position to a prepared zone and initiates required world travel.
    bool UpdateDestination();
    bool bTravelRequested = false;
    uint32 LoadedDestinationRevision = 0;
    FString DestinationLoadError;
    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> RenderHost;
    UPROPERTY(Transient)
    TWeakObjectPtr<UWorld> RenderWorld;
    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> ZoneRenderer;
    FTransform RenderTransform = FTransform::Identity;
    bool bStartupAttempted = false;
    bool bSessionInitialized = false;
    bool bStartupConfigValid = true;
    double LastDiagnostic = 0;
    FString LastPublishedState, LastPublishedError;
    /// Publishes protocol connection and gameplay state to Unreal consumers.
    void PublishState();
    /// Drains confirmed events into gameplay, animation, chat and sound handlers.
    void DispatchCombatEvents();
    /// Appends a decoded chat event to the bounded session history.
    UFUNCTION()
    void RecordChatEvent(const FDaocCombatEvent &Event);
    UPROPERTY(Transient)
    TArray<FDaocChatMessage> ChatMessages;
    int64 ChatRevision = 0;
    /// Maps a confirmed event to a supported catalog sound.
    void PlayCombatSound(const FDaocCombatEvent &Event);
    /// Resolves one object ID without constructing the complete target list.
    bool ResolveCombatTarget(int32 ObjectId, FDaocCombatTarget &Target) const;
    /// Plays a mapped sound at a world position within its radius and delay.
    void PlayCombatSoundAt(int32 SoundId, FVector Position, int32 Radius, float Delay = 0);
    /// Evaluates scene visibility between the supplied replicated object IDs.
    bool CombatLineOfSight(int32 Source, int32 Target) const;
    /// Releases replicated character presenters and associated transient state.
    void ClearPlayers();
    uint64 CombatSessionGeneration = 0;
    FCamelotConnection *Connection = nullptr;
    TMap<uint8, double> PacketDiagnosticTimes;
    TMap<uint8, uint64> PacketDiagnosticCounts;
    int32 NetworkZoneId = 0;
    float UnitsPerServerUnit = 100.f * 255.f / 65536.f;
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<USceneComponent>> Players;
    /// URO groups by owning actor. Each live skeletal body needs its own group;
    /// using RenderHost for every body synchronizes all skipped evaluations.
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<AActor>> CharacterRenderHosts;
    UPROPERTY(Transient)
    TObjectPtr<class UCamelotLiveCrowdPresenter> LiveCrowd;
    bool bCrowdCatalogLookupAttempted = false;
    /// Routes eligible replicated characters into prepared appearance batches.
    void RefreshCrowdPresentation();
    TMap<int32, uint32> AppearanceRevisions;
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<UDaocEquipmentComponent>> EquipmentRenderers;
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<class UDaocCombatAnimationComponent>> CombatPresentations;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAoC|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<class UDaocCombatAnimationCatalog> CombatAnimationCatalog;
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<USkeletalMesh>> ResolvedCharacterMeshes;
    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UAnimSequence>> ResolvedAnimations;
};
