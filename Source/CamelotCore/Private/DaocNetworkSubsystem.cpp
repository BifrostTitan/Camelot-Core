#include "DaocNetworkSubsystem.h"
#include "DaocEffectSubsystem.h"
#include "ProfilingDebugging/CsvProfiler.h"
CSV_DEFINE_CATEGORY(CamelotNetwork, true);
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camelot/CamelotClient.h"
#include "CamelotLiveCrowd.h"
#include "CamelotUIPolicy.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DaocCharacterSizing.h"
#include "DaocClientAssetCatalog.h"
#include "DaocCombatAnimationCatalog.h"
#include "DaocCombatAnimationComponent.h"
#include "DaocEquipmentCatalog.h"
#include "DaocEquipmentComponent.h"
#include "DaocPlayerCharacter.h"
#include "DaocZoneActor.h"
#include "DaocZoneProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

class FCamelotConnection
{
  public:
    camelot::Client Client;
    FSocket *Socket = nullptr;
    bool bConnected = false;
    double ConnectDeadline = 0;
    camelot::Bytes Pending;
    size_t SendOffset = 0;
    explicit FCamelotConnection(camelot::Settings Settings) : Client(MoveTemp(Settings))
    {
    }
    ~FCamelotConnection()
    {
        if (Socket)
        {
            Socket->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
        }
    }
    void Open(const FString &Host, int32 Port, double Timeout)
    {
        auto *Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        auto Address = Subsystem->CreateInternetAddr();
        bool bValid = false;
        Address->SetIp(*Host, bValid);
        Address->SetPort(Port);
        if (!bValid || Port < 1 || Port > 65535)
        {
            Client.Fail("Invalid IPv4 address or port");
            return;
        }
        Socket = Subsystem->CreateSocket(NAME_Stream, TEXT("Camelot DOL client"), Address->GetProtocolType());
        if (!Socket || !Socket->SetNonBlocking(true))
        {
            Client.Fail("Unable to create nonblocking socket");
            return;
        }
        Socket->SetNoDelay(true);
        const bool bImmediate = Socket->Connect(*Address);
        const auto Error = Subsystem->GetLastErrorCode();
        if (!bImmediate && Error != SE_EWOULDBLOCK && Error != SE_EINPROGRESS && Error != SE_EALREADY)
        {
            Client.Fail("TCP connection failed");
            return;
        }
        ConnectDeadline = FPlatformTime::Seconds() + Timeout;
    }
    void Pump()
    {
        CSV_SCOPED_TIMING_STAT(CamelotNetwork, SocketPump);
        const double Now = FPlatformTime::Seconds();
        if (!bConnected)
        {
            const auto State = Socket->GetConnectionState();
            if (State == SCS_ConnectionError || Now >= ConnectDeadline)
            {
                Client.Fail("TCP connection failed or timed out");
                return;
            }
            if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::Zero()))
                return;
            bConnected = true;
            Client.Start(Now);
        }
        for (auto &Packet : Client.TakeOutgoing())
            Pending.insert(Pending.end(), Packet.begin(), Packet.end());
        if (Pending.size() - SendOffset > 65536)
        {
            Client.Fail("Socket send queue limit exceeded");
            return;
        }
        if (SendOffset < Pending.size())
        {
            int32 Sent = 0;
            if (!Socket->Send(Pending.data() + SendOffset, int32(Pending.size() - SendOffset), Sent))
            {
                if (ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode() != SE_EWOULDBLOCK)
                {
                    Client.Fail("TCP send failed");
                    return;
                }
            }
            SendOffset += Sent;
            if (SendOffset == Pending.size())
            {
                Pending.clear();
                SendOffset = 0;
            }
        }
        for (int32 I = 0; I < 32; ++I)
        {
            // UE reports would-block as a successful Recv with zero bytes on Windows.
            // Check readability first so only a real EOF is treated as a disconnect.
            if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::Zero()))
                break;
            uint8 Buffer[8192];
            int32 Read = 0;
            if (!Socket->Recv(Buffer, sizeof(Buffer), Read, ESocketReceiveFlags::None))
            {
                if (ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode() != SE_EWOULDBLOCK)
                    Client.Fail("TCP receive failed");
                break;
            }
            if (Read == 0)
            {
                Client.Fail("Server disconnected");
                break;
            }
            Client.Feed(Buffer, size_t(Read), Now);
            if (Client.GetState() == camelot::State::Failed)
                return;
        }
        Client.Tick(Now);
    }
};
UDaocNetworkSubsystem::UDaocNetworkSubsystem()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    DummyPlayerMesh = Cylinder.Object;
}
UDaocNetworkSubsystem::~UDaocNetworkSubsystem()
{
    delete Connection;
}
UDaocNetworkSubsystem *UDaocNetworkSubsystem::Get(const UObject *Context)
{
    const auto *World = Context ? Context->GetWorld() : nullptr;
    return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UDaocNetworkSubsystem>() : nullptr;
}
UWorld *UDaocNetworkSubsystem::GetWorld() const
{
    auto *World = Super::GetWorld();
    return World ? World : RenderWorld.Get();
}
TStatId UDaocNetworkSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UDaocNetworkSubsystem, STATGROUP_Tickables);
}
bool UDaocNetworkSubsystem::IsTickable() const
{
    return bSessionInitialized && !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld();
}
void UDaocNetworkSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);
    bSessionInitialized = true;
    LoadStartupSettings();
}
void UDaocNetworkSubsystem::Deinitialize()
{
    bSessionInitialized = false;
    Disconnect();
    if (RenderHost.IsValid())
        RenderHost->Destroy();
    RenderHost.Reset();
    RenderWorld.Reset();
    Password.Reset();
    Super::Deinitialize();
}
bool UDaocNetworkSubsystem::EnsureRenderHost()
{
    auto *World = GetWorld();
    if (!World || !World->IsGameWorld() || World->bIsTearingDown)
        return false;
    if (RenderHost.IsValid() && RenderHost->GetWorld() != World)
    {
        ClearRenderState();
        LocalPlayerPawn.Reset();
        RenderHost.Reset();
        ZoneRenderer.Reset();
    }
    if (!RenderHost.IsValid())
    {
        ClearRenderState();
        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        RenderHost = World->SpawnActor<AActor>(Params);
        if (!RenderHost.IsValid())
            return false;
        auto *Root = NewObject<USceneComponent>(RenderHost.Get(), TEXT("NetworkEntities"), RF_Transient);
        RenderHost->SetRootComponent(Root);
        Root->RegisterComponent();
    }
    RenderWorld = World;
    if (ZoneRenderer.IsValid() && ZoneRenderer->GetWorld() != World)
        ZoneRenderer.Reset();
    if (LocalPlayerPawn.IsValid() && LocalPlayerPawn->GetWorld() != World)
        LocalPlayerPawn.Reset();
    if (!ZoneRenderer.IsValid())
        for (TActorIterator<ADaocZoneActor> It(World); It; ++It)
        {
            ZoneRenderer = *It;
            break;
        }
    if (auto *Zone = Cast<ADaocZoneActor>(ZoneRenderer.Get()))
    {
        RenderTransform = Zone->GetActorTransform();
        if (Zone->GetZoneId() >= 0)
            StartForZone(Zone->GetZoneId(), Zone->GetWorldUnitsPerDaocUnit());
    }
    RenderHost->SetActorTransform(RenderTransform);
    return true;
}
bool UDaocNetworkSubsystem::RequestRealm(int32 NewRealm)
{
    if (!Connection || NewRealm < 1 || NewRealm > 3)
        return false;
    if (!Connection->Client.RequestRealm(uint8(NewRealm), FPlatformTime::Seconds()))
        return false;
    Realm = NewRealm;
    CharacterName.Reset();
    bCharactersReady = false;
    CharacterSlots.Reset();
    return true;
}
bool UDaocNetworkSubsystem::CheckCharacterName(const FString &Name)
{
    return Connection && Connection->Client.CheckCharacterName(TCHAR_TO_UTF8(*Name));
}
bool UDaocNetworkSubsystem::CreateCharacter(const FDaocCreationRequest &R)
{
    if (!Connection || R.Stats.Num() != 8 || R.Slot < 0 || R.Slot >= 10 || R.Region < 0 || R.Region > 255)
        return false;
    const auto &A = R.Appearance;
    for (int32 V : {A.Race, A.Gender, A.ClassId, A.Realm, A.EyeSize, A.LipSize, A.EyeColor, A.HairColor, A.FaceType, A.HairStyle, A.MoodType})
        if (V < 0 || V > 255)
            return false;
    if (A.Model <= 0 || A.Model > 2047)
        return false;
    camelot::CharacterDraft D;
    D.name = TCHAR_TO_UTF8(*A.Name);
    D.slot = uint8(R.Slot);
    D.region = uint8(R.Region);
    D.isles = R.bIsles;
    D.race = uint8(A.Race);
    D.gender = uint8(A.Gender);
    D.realm = uint8(A.Realm);
    D.classId = uint8(A.ClassId);
    D.model = uint16(A.Model | ((A.ModelFlags & 0x1800) ? (A.ModelFlags & 0x1800) : 0x1000));
    D.eyeSize = uint8(A.EyeSize);
    D.lipSize = uint8(A.LipSize);
    D.eyeColor = uint8(A.EyeColor);
    D.hairColor = uint8(A.HairColor);
    D.faceType = uint8(A.FaceType);
    D.hairStyle = uint8(A.HairStyle);
    D.moodType = uint8(A.MoodType);
    for (int32 I = 0; I < 8; ++I)
    {
        if (R.Stats[I] < 0 || R.Stats[I] > 255)
            return false;
        D.stats[I] = uint8(R.Stats[I]);
    }
    const bool Sent = Connection->Client.CreateCharacter(D, FPlatformTime::Seconds());
    if (Sent)
        bCharactersReady = false;
    return Sent;
}
bool UDaocNetworkSubsystem::DeleteCharacter(const FString &Name)
{
    return Connection && Connection->Client.DeleteCharacter(TCHAR_TO_UTF8(*Name), FPlatformTime::Seconds());
}
bool UDaocNetworkSubsystem::SelectCharacter(const FString &Name)
{
    if (!Connection)
        return false;
    const bool Selected = Connection->Client.SelectCharacter(TCHAR_TO_UTF8(*Name), FPlatformTime::Seconds());
    LastNetworkError = UTF8_TO_TCHAR(Connection->Client.Error().c_str());
    if (Selected)
    {
        bWorldReady = false;
        LoadedDestinationRevision = 0;
        bTravelRequested = false;
        ClearPlayers();
        CharacterName = Name;
        ConnectionState = TEXT("Selecting");
    }
    PublishState();
    return Selected;
}
bool UDaocNetworkSubsystem::LoadLoginConfig(const FString &File)
{
    if (Connection)
    {
        LastNetworkError = TEXT("Disconnect before changing login configuration");
        return false;
    }
    FConfigFile Config;
    const FString Path = FPaths::ConvertRelativePathToFull(File);
    if (!FPaths::FileExists(Path))
    {
        LastNetworkError = TEXT("Cannot read Camelot login configuration");
        return false;
    }
    Config.Read(Path);
    const TCHAR *Section = TEXT("Camelot.Login");
    if (!Config.FindSection(Section))
    {
        LastNetworkError = TEXT("Login configuration needs a [Camelot.Login] section");
        return false;
    }
    Config.GetString(Section, TEXT("ServerAddress"), ServerAddress);
    Config.GetInt(Section, TEXT("ServerPort"), ServerPort);
    Config.GetString(Section, TEXT("Username"), Username);
    Config.GetString(Section, TEXT("Password"), Password);
    Config.GetString(Section, TEXT("CharacterName"), CharacterName);
    Config.GetInt(Section, TEXT("Realm"), Realm);
    Config.GetInt(Section, TEXT("ClientVersion"), ClientVersion);
    Config.GetFloat(Section, TEXT("RequestTimeoutSeconds"), RequestTimeoutSeconds);
    Config.GetBool(Section, TEXT("AutoConnect"), bAutoConnect);
    Config.GetBool(Section, TEXT("UseFrontend"), bUseFrontend);
    Config.GetInt(Section, TEXT("ZoneId"), NetworkZoneId);
    double X = ServerZoneOrigin.X, Y = ServerZoneOrigin.Y;
    Config.GetDouble(Section, TEXT("ZoneOriginX"), X);
    Config.GetDouble(Section, TEXT("ZoneOriginY"), Y);
    ServerZoneOrigin = FVector2D(X, Y);
    LastNetworkError.Reset();
    return true;
}
void UDaocNetworkSubsystem::LoadStartupSettings()
{
    if (GConfig)
    {
        const TCHAR *S = TEXT("Camelot.Login");
        GConfig->GetString(S, TEXT("ServerAddress"), ServerAddress, GGameIni);
        GConfig->GetInt(S, TEXT("ServerPort"), ServerPort, GGameIni);
        GConfig->GetString(S, TEXT("Username"), Username, GGameIni);
        GConfig->GetString(S, TEXT("CharacterName"), CharacterName, GGameIni);
        GConfig->GetString(S, TEXT("Password"), Password, GGameIni);
        GConfig->GetInt(S, TEXT("Realm"), Realm, GGameIni);
        GConfig->GetInt(S, TEXT("ClientVersion"), ClientVersion, GGameIni);
        GConfig->GetBool(S, TEXT("AutoConnect"), bAutoConnect, GGameIni);
        GConfig->GetBool(S, TEXT("UseFrontend"), bUseFrontend, GGameIni);
        GConfig->GetInt(S, TEXT("ZoneId"), NetworkZoneId, GGameIni);
        GConfig->GetFloat(S, TEXT("RequestTimeoutSeconds"), RequestTimeoutSeconds, GGameIni);
        double X = ServerZoneOrigin.X, Y = ServerZoneOrigin.Y;
        GConfig->GetDouble(S, TEXT("ZoneOriginX"), X, GGameIni);
        GConfig->GetDouble(S, TEXT("ZoneOriginY"), Y, GGameIni);
        ServerZoneOrigin = FVector2D(X, Y);
    }
    FString File;
    if (FParse::Value(FCommandLine::Get(), TEXT("CamelotConfig="), File))
        bStartupConfigValid = LoadLoginConfig(File);
    FString Value;
    if (FParse::Value(FCommandLine::Get(), TEXT("CamelotServer="), Value))
        ServerAddress = Value;
    FParse::Value(FCommandLine::Get(), TEXT("CamelotPort="), ServerPort);
    if (FParse::Value(FCommandLine::Get(), TEXT("CamelotUsername="), Value))
        Username = Value;
    if (FParse::Value(FCommandLine::Get(), TEXT("CamelotCharacter="), Value))
    {
        CharacterName = Value;
        bUseFrontend = false;
    }
    FParse::Value(FCommandLine::Get(), TEXT("CamelotRealm="), Realm);
    if (FParse::Param(FCommandLine::Get(), TEXT("CamelotAutoConnect")))
        bAutoConnect = true;
    if (FParse::Param(FCommandLine::Get(), TEXT("CamelotFrontend")))
        bUseFrontend = true;
    if (FParse::Param(FCommandLine::Get(), TEXT("CamelotSkipFrontend")))
        bUseFrontend = false;
#if !UE_BUILD_SHIPPING
    FString FrontendPreview;
    if (FParse::Value(FCommandLine::Get(), TEXT("CamelotFrontendPreview="), FrontendPreview))
    {
        bUseFrontend = true;
        bAutoConnect = false;
    }
#endif
    if (FParse::Param(FCommandLine::Get(), TEXT("CamelotNoAutoConnect")))
        bAutoConnect = false;
    Value = FPlatformMisc::GetEnvironmentVariable(TEXT("CAMELOT_PASSWORD"));
    if (!Value.IsEmpty())
        Password = Value;
    ClientAssetCatalog = LoadObject<UDaocClientAssetCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_ClientAssets.DA_ClientAssets"), nullptr, LOAD_NoWarn);
    EquipmentCatalog = LoadObject<UDaocEquipmentCatalog>(nullptr, TEXT("/Game/DAoC/Equipment/DA_EquipmentCatalog.DA_EquipmentCatalog"), nullptr, LOAD_NoWarn);
    if (!EquipmentCatalog)
        EquipmentCatalog = LoadObject<UDaocEquipmentCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_Equipment.DA_Equipment"), nullptr, LOAD_NoWarn);
}
void UDaocNetworkSubsystem::SetCredentials(const FString &Account, const FString &Secret, const FString &Character)
{
    Username = Account;
    Password = Secret;
    CharacterName = Character;
}
void UDaocNetworkSubsystem::StartForZone(int32 ZoneId, float RendererScale)
{
    NetworkZoneId = ZoneId;
    UnitsPerServerUnit = RendererScale * 255.f / 65536.f;
}
void UDaocNetworkSubsystem::Connect()
{
    Disconnect();
    LastNetworkError.Reset();
    if (!GetWorld() || !GetWorld()->IsGameWorld())
    {
        LastNetworkError = TEXT("Connect is available during play");
        ConnectionState = TEXT("Failed");
        PublishState();
        return;
    }
    if (NetworkZoneId < 0 || NetworkZoneId > (ClientVersion >= 72 ? 65535 : 255) || UnitsPerServerUnit <= 0 || !FMath::IsFinite(UnitsPerServerUnit) || (ClientVersion != 68 && (ClientVersion < 72 || ClientVersion > 74)) || Realm < 1 || Realm > 3 || RequestTimeoutSeconds <= 0 || !FMath::IsFinite(RequestTimeoutSeconds) || !FMath::IsFinite(ServerZoneOrigin.X) || !FMath::IsFinite(ServerZoneOrigin.Y) || ServerZoneOrigin.X < 0 || ServerZoneOrigin.Y < 0 || ServerZoneOrigin.X > 4294901760.0 || ServerZoneOrigin.Y > 4294901760.0)
    {
        LastNetworkError = TEXT("Invalid session realm, zone coordinates, client version or timeout");
        ConnectionState = TEXT("Failed");
        PublishState();
        return;
    }
    camelot::Settings Settings;
    Settings.username = TCHAR_TO_UTF8(*Username);
    FString Secret = Password.IsEmpty() ? FPlatformMisc::GetEnvironmentVariable(TEXT("CAMELOT_PASSWORD")) : Password;
    if (Secret.IsEmpty() && GConfig)
        GConfig->GetString(TEXT("Camelot.Network"), TEXT("Password"), Secret, GGameIni);
    Settings.password = TCHAR_TO_UTF8(*Secret);
    Settings.character = TCHAR_TO_UTF8(*CharacterName);
    Settings.realm = uint8(Realm);
    Settings.interactive = bUseFrontend;
    Settings.timeoutSeconds = RequestTimeoutSeconds;
    Settings.version = uint8(ClientVersion);
    Settings.zone = uint16(NetworkZoneId);
    if (bManageZoneTravel)
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CamelotCore"));
        FString Json;
        TArray<TSharedPtr<FJsonValue>> Zones;
        if (!Plugin || !FFileHelper::LoadFileToString(Json, *(Plugin->GetBaseDir() / TEXT("Data/WorldZones.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Zones))
        {
            LastNetworkError = TEXT("Missing WorldZones.json destination catalog");
            ConnectionState = TEXT("Failed");
            PublishState();
            return;
        }
        for (const auto &Value : Zones)
        {
            const auto Z = Value->AsObject();
            if (!Z || !Z->GetBoolField(TEXT("enabled")))
                continue;
            camelot::Zone Zone;
            Zone.id = uint16(Z->GetIntegerField(TEXT("id")));
            Zone.region = uint16(Z->GetIntegerField(TEXT("region")));
            Zone.x = uint32(Z->GetNumberField(TEXT("originX")));
            Zone.y = uint32(Z->GetNumberField(TEXT("originY")));
            const int32 Type = Z->GetIntegerField(TEXT("type"));
            Zone.outdoor = Type == 0 || Type == 1 || Type == 3;
            Settings.zones.push_back(Zone);
        }
    }
    Settings.zoneOriginX = uint32(ServerZoneOrigin.X);
    Settings.zoneOriginY = uint32(ServerZoneOrigin.Y);
    Connection = new FCamelotConnection(MoveTemp(Settings));
    Connection->Client.Start(FPlatformTime::Seconds());
    Connection->Client.TakeOutgoing();
    if (Connection->Client.GetState() != camelot::State::Failed)
        Connection->Open(ServerAddress, ServerPort, RequestTimeoutSeconds);
    if (Connection->Client.GetState() == camelot::State::Failed)
    {
        LastNetworkError = UTF8_TO_TCHAR(Connection->Client.Error().c_str());
        ConnectionState = TEXT("Failed");
        delete Connection;
        Connection = nullptr;
    }
    else
        ConnectionState = TEXT("Connecting");
    PublishState();
}
void UDaocNetworkSubsystem::PublishState()
{
    if (ConnectionState == LastPublishedState && LastNetworkError == LastPublishedError)
        return;
    LastPublishedState = ConnectionState;
    LastPublishedError = LastNetworkError;
    UE_LOG(LogTemp, Display, TEXT("Camelot session: %s"), *ConnectionState);
    OnConnectionChanged.Broadcast(ConnectionState, LastNetworkError);
}
void UDaocNetworkSubsystem::ClearPlayers()
{
    PacketDiagnosticTimes.Reset();
    PacketDiagnosticCounts.Reset();
    ++CombatSessionGeneration;
    ClearRenderState();
    AccountCharacters.Empty();
    CharacterSlots.Empty();
    bCharactersReady = false;
    AllowedRealm = -1;
    CheckedCharacterName.Reset();
    NameCheckResult = -1;
    ReplicatedNPCCount = ReplicatedPlayerCount = PlayerObjectId = 0;
    ServerPlayerPosition = LastSubmittedPosition = FVector::ZeroVector;
}
void UDaocNetworkSubsystem::ClearRenderState()
{
    if (GetWorld())
        if (auto *Effects = GetWorld()->GetSubsystem<UDaocEffectSubsystem>())
            Effects->ResetEffects();
    MobFloorSamples.Reset();
    if (LiveCrowd)
        LiveCrowd->Clear();
    LiveCrowd = nullptr;
    LiveCrowdCharacterCount = LiveCrowdBatchCount = LiveCrowdPoseCount = LiveCrowdPoolMissCount = 0;
    for (auto &Entry : CombatPresentations)
        if (Entry.Value)
            Entry.Value->DestroyComponent();
    CombatPresentations.Reset();
    if (auto *Pawn = Cast<ADaocPlayerCharacter>(LocalPlayerPawn.Get()))
        Pawn->CombatAnimations->ResetCombat();
    for (auto &Entry : EquipmentRenderers)
        if (Entry.Value)
            Entry.Value->DestroyComponent();
    EquipmentRenderers.Reset();
    for (auto &Entry : Players)
        if (Entry.Value)
            Entry.Value->DestroyComponent();
    Players.Empty();
    for (auto &Entry : CharacterRenderHosts)
        if (IsValid(Entry.Value))
            Entry.Value->Destroy();
    CharacterRenderHosts.Empty();
    AppearanceRevisions.Empty();
    PlayerAppearances.Empty();
    ResolvedAnimations.Empty();
    ReplicatedNPCCount = 0;
    ReplicatedPlayerCount = 0;
}
void UDaocNetworkSubsystem::Disconnect()
{
    ChatMessages.Reset();
    ++ChatRevision;
    delete Connection;
    Connection = nullptr;
    bWorldReady = false;
    LoadedDestinationRevision = 0;
    bTravelRequested = false;
    ActiveZoneId = INDEX_NONE;
    ClearPlayers();
    ConnectionState = TEXT("Disconnected");
    LastNetworkError.Reset();
    PublishState();
}

FVector UDaocNetworkSubsystem::ServerToWorld(FVector Position) const
{
    FVector Local((Position.X - ServerZoneOrigin.X) * UnitsPerServerUnit, (Position.Y - ServerZoneOrigin.Y) * UnitsPerServerUnit, Position.Z * UnitsPerServerUnit);
    return RenderTransform.TransformPosition(Local);
}
FVector UDaocNetworkSubsystem::WorldToServer(FVector Position) const
{
    FVector Local = RenderTransform.InverseTransformPosition(Position) / UnitsPerServerUnit;
    return FVector(Local.X + ServerZoneOrigin.X, Local.Y + ServerZoneOrigin.Y, Local.Z);
}
bool UDaocNetworkSubsystem::SendPlayerPosition(FVector Position, int32 Heading, int32 Speed, bool bFlying)
{
    if (!Connection || Position.ContainsNaN() || Position.X < 0 || Position.Y < 0 || Position.Z < 1 || Position.X > 4294967295.0 || Position.Y > 4294967295.0 || Position.Z > 65535 || Speed < 0 || Speed > 511)
        return false;
    const bool Sent = Connection->Client.Move(uint32(Position.X), uint32(Position.Y), uint16(Position.Z), uint16(Heading & 4095), uint16(Speed), CombatLineOfSight(PlayerObjectId, Connection->Client.Combat().target), false, bFlying);
    if (Sent)
        LastSubmittedPosition = FVector(FMath::Floor(Position.X), FMath::Floor(Position.Y), FMath::Floor(Position.Z));
    return Sent;
}
bool UDaocNetworkSubsystem::RequestDeveloperMode(bool bEnabled)
{
    return Connection && Connection->Client.SendCommand(bEnabled ? "&debug on" : "&debug off");
}
bool UDaocNetworkSubsystem::IsDeveloperModeAuthorized() const
{
    return Connection && Connection->Client.GetState() == camelot::State::Playing && Connection->Client.DebugEnabled();
}
void UDaocNetworkSubsystem::Tick(float DeltaTime)
{
    if (!GetWorld() || !GetWorld()->IsGameWorld())
        return;
    EnsureRenderHost();
    if (!bStartupAttempted && GetWorld()->HasBegunPlay())
    {
        bStartupAttempted = true;
        if (bAutoConnect && !FParse::Param(FCommandLine::Get(), TEXT("DaocOffline")))
        {
            if (bStartupConfigValid)
                Connect();
            else
            {
                ConnectionState = TEXT("Failed");
                UE_LOG(LogTemp, Error, TEXT("Camelot: %s"), *LastNetworkError);
                PublishState();
            }
        }
    }
    if (!Connection)
        return;
    Connection->Pump();
    for (const auto &Diagnostic : Connection->Client.TakePacketDiagnostics())
    {
        const double Now = FPlatformTime::Seconds();
        uint64 &Count = PacketDiagnosticCounts.FindOrAdd(Diagnostic.opcode);
        Count += Diagnostic.occurrences;
        const double *Last = PacketDiagnosticTimes.Find(Diagnostic.opcode);
        if (!Last || Now - *Last >= 5.0)
        {
            UE_LOG(LogTemp, Warning, TEXT("Camelot: rejected packet 0x%02x (%llu occurrences, latest %llu bytes): %s; keeping confirmed gameplay state"), Diagnostic.opcode, static_cast<unsigned long long>(Count), static_cast<unsigned long long>(Diagnostic.payloadBytes), UTF8_TO_TCHAR(Diagnostic.reason.c_str()));
            PacketDiagnosticTimes.Add(Diagnostic.opcode, Now);
            Count = 0;
        }
    }
    if (Connection->Client.GetState() == camelot::State::Failed)
    {
        LastNetworkError = UTF8_TO_TCHAR(Connection->Client.Error().c_str());
        UE_LOG(LogTemp, Error, TEXT("Camelot: %s"), *LastNetworkError);
        delete Connection;
        Connection = nullptr;
        ClearPlayers();
        ConnectionState = TEXT("Failed");
        PublishState();
        return;
    }
    if (Connection->bConnected)
        ConnectionState = UTF8_TO_TCHAR(camelot::StateName(Connection->Client.GetState()));
    LastNetworkError = UTF8_TO_TCHAR(Connection->Client.Error().c_str());
    if (!LastNetworkError.IsEmpty() && FPlatformTime::Seconds() - LastDiagnostic > 5)
    {
        UE_LOG(LogTemp, Warning, TEXT("Camelot: %s"), *LastNetworkError);
        LastDiagnostic = FPlatformTime::Seconds();
    }
    UpdateDestination();
    RefreshPlayers();
    DispatchCombatEvents();
    RefreshCrowdPresentation();
    PublishState();
}
bool UDaocNetworkSubsystem::SendAdminCommand(const FString &Command)
{
    if (!bWorldReady || !IsDeveloperModeAuthorized() || !Connection)
        return false;
    FString Wire = Command.TrimStartAndEnd();
    if (Wire.StartsWith(TEXT("/")))
        Wire[0] = TEXT('&');
    else if (!Wire.StartsWith(TEXT("&")))
        Wire = TEXT("&") + Wire;
    if (Wire.Len() < 2 || Wire.Len() > 254)
        return false;
    for (TCHAR C : Wire)
        if (C < 32 || C > 126)
            return false;
    // DOL performs its normal command-specific privilege checks and database
    // operations.
    return Connection->Client.SendCommand(TCHAR_TO_UTF8(*Wire));
}
bool UDaocNetworkSubsystem::SendChat(const FString &Text)
{
    FString Wire;
    return bWorldReady && Connection && CamelotUI::NormalizeChat(Text, Wire) && Connection->Client.SendCommand(TCHAR_TO_UTF8(*Wire));
}
bool UDaocNetworkSubsystem::GetNameplateAnchor(int32 ObjectId, FVector &Position) const
{
    const auto *Mesh = Cast<UPrimitiveComponent>(Players.FindRef(ObjectId).Get());
    if (!Mesh || !Mesh->IsRegistered() || !Mesh->GetVisibleFlag())
        return false;
    if (!IsCrowdRendered(ObjectId) && (!Mesh->IsVisible() || Mesh->bHiddenInGame))
        return false;
    Position = Mesh->Bounds.Origin + FVector(0, 0, Mesh->Bounds.BoxExtent.Z + 16);
    return true;
}
int32 UDaocNetworkSubsystem::GetServerRegionId() const
{
    return Connection ? Connection->Client.Region() : 0;
}
FDaocServerWeather UDaocNetworkSubsystem::GetServerWeather() const
{
    FDaocServerWeather Result;
    if (!Connection)
        return Result;
    const auto &W = Connection->Client.CurrentWeather();
    if (!W.known || W.region != Connection->Client.Region())
        return Result;
    Result.bKnown = true;
    Result.RegionId = W.region;
    Result.FrontX = float(double(W.position) + FMath::Max(0., FPlatformTime::Seconds() - W.receivedAt) * W.speed);
    Result.Width = float(W.width);
    Result.Speed = W.speed;
    Result.FogDiffusion = W.fogDiffusion;
    Result.Intensity = FMath::Clamp(float(W.intensity) / 120.f, 0.f, 1.f);
    return Result;
}
bool UDaocNetworkSubsystem::UpdateDestination()
{
    if (!Connection)
        return false;
    const auto State = Connection->Client.GetState();
    if (State != camelot::State::Playing)
        bWorldReady = false;
    if (!bManageZoneTravel)
    {
        bWorldReady = State == camelot::State::Playing;
        return bWorldReady;
    }
    if (State == camelot::State::Playing)
        bWorldReady = LoadedDestinationRevision == Connection->Client.DestinationRevision();
    if (State != camelot::State::Loading)
        return bWorldReady;
    const auto Revision = Connection->Client.DestinationRevision();
    if (LoadedDestinationRevision == Revision)
    {
        LastNetworkError = DestinationLoadError;
        ConnectionState = TEXT("LoadingFailed");
        return false;
    }
    FString Current = UGameplayStatics::GetCurrentLevelName(GetWorld(), true);
    if (Current != FPaths::GetBaseFilename(GameplayMap.ToString()))
    {
        if (!bTravelRequested)
        {
            bTravelRequested = true;
            ClearPlayers();
            UGameplayStatics::OpenLevel(GetWorld(), GameplayMap);
        }
        ConnectionState = TEXT("Loading");
        return false;
    }
    bTravelRequested = false;
    auto *Zone = Cast<ADaocZoneActor>(ZoneRenderer.Get());
    const int32 Destination = Connection->Client.ZoneId();
    if (!Zone || !Zone->LoadZoneById(Destination) || Zone->GetZoneId() != Destination)
    {
        LastNetworkError = FString::Printf(TEXT("Zone %d is not prepared; cannot enter world"), Destination);
        DestinationLoadError = LastNetworkError;
        UE_LOG(LogTemp, Error, TEXT("Camelot destination load failed: %s"), *LastNetworkError);
        LoadedDestinationRevision = Revision;
        ConnectionState = TEXT("LoadingFailed");
        return false;
    }
    ClearPlayers();
    DestinationLoadError.Reset();
    ServerZoneOrigin = FVector2D(Connection->Client.ZoneOriginX(), Connection->Client.ZoneOriginY());
    StartForZone(Destination, Zone->GetWorldUnitsPerDaocUnit());
    RenderTransform = Zone->GetActorTransform();
    ActiveZoneId = Destination;
    LoadedDestinationRevision = Revision;
    ++WorldRevision;
    Connection->Client.ConfirmWorldReady(FPlatformTime::Seconds());
    UE_LOG(LogTemp, Display,
           TEXT("Camelot destination ready: region %d zone %d origin %.0f,%.0f "
                "revision %d"),
           Connection->Client.Region(), Destination, ServerZoneOrigin.X, ServerZoneOrigin.Y, WorldRevision);
    UE_LOG(LogTemp, Display, TEXT("Camelot character %s destination profile %s export %s"), *CharacterName, *Zone->SelectedZoneProfile->GetPathName(), *Zone->SelectedZoneProfile->ExportDirectory);
    bWorldReady = Connection->Client.GetState() == camelot::State::Playing;
    ConnectionState = UTF8_TO_TCHAR(camelot::StateName(Connection->Client.GetState()));
    return bWorldReady;
}
USkeletalMesh *UDaocNetworkSubsystem::ResolveCharacterMesh(int32 Model)
{
    if (auto *Explicit = CharacterMeshes.FindRef(Model).Get())
        return Explicit;
    if (!bLoadImportedCharacterMeshes || Model <= 0)
        return nullptr;
    if (!ResolvedCharacterMeshes.Contains(Model))
    {
        USkeletalMesh *Asset = nullptr;
        if (ClientAssetCatalog)
            if (const auto *Entry = ClientAssetCatalog->Characters.Find(Model))
                Asset = Entry->LoadSynchronous();
        if (!Asset)
        {
            const FString Path = FString::Printf(TEXT("/Game/DAoC/Characters/SK_daoc_%d.SK_daoc_%d"), Model, Model);
            Asset = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn);
        }
        ResolvedCharacterMeshes.Add(Model, Asset);
    }
    return ResolvedCharacterMeshes.FindRef(Model).Get();
}
FVector UDaocNetworkSubsystem::GetCharacterScale(const USkeletalMesh *Mesh) const
{
    const bool Imported = Mesh && Mesh->GetPathName().StartsWith(TEXT("/Game/DAoC/Characters/SK_daoc_"));
    return CharacterMeshScale * (Imported ? UnitsPerServerUnit / 2.54f : 1.f);
}
FVector UDaocNetworkSubsystem::GetAppearanceScale(const USkeletalMesh *Mesh, const FDaocCharacterAppearance &A) const
{
    return GetCharacterScale(Mesh) * DaocCharacterSizing::AppearanceScale(A.Model, A.ModelFlags);
}
FVector UDaocNetworkSubsystem::ResolveMobFeet(FVector WorldFeet, bool bFlying)
{
    const auto *Zone = Cast<ADaocZoneActor>(ZoneRenderer.Get());
    FVector Ground;
    if (bFlying || !Zone || !Zone->FindTerrainSurface(WorldFeet, Ground))
        return WorldFeet;
    // Exact rendered terrain triangles are cheap to sample between server
    // waypoints. Elevated walkable surfaces need collision: terrain alone would
    // sink bridges.
    if (WorldFeet.Z <= Ground.Z + 4.f)
        return Ground;
    // Aquatic/swimming NPCs use the server's depth rather than the river bed.
    if (Zone->IsUnderwater(WorldFeet))
        return WorldFeet;
    const FIntVector Key(FMath::FloorToInt(WorldFeet.X / 50.f), FMath::FloorToInt(WorldFeet.Y / 50.f), FMath::FloorToInt(WorldFeet.Z / 64.f));
    const double Now = FPlatformTime::Seconds();
    auto *Sample = MobFloorSamples.Find(Key);
    if (!Sample || Sample->ExpiresAt < Now)
    {
        // While a new cell awaits its check, preserve the server's floor height.
        if (!MobFloorQueriesRemaining)
            return WorldFeet;
        --MobFloorQueriesRemaining;
        if (MobFloorSamples.Num() >= 4096)
            MobFloorSamples.Reset();
        FMobFloorSample NewSample;
        NewSample.ExpiresAt = Now + 2.;
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(CamelotMobFloor), true);
        if (LocalPlayerPawn.IsValid())
            Params.AddIgnoredActor(LocalPlayerPawn.Get());
        if (GetWorld()->LineTraceSingleByObjectType(Hit, WorldFeet + FVector(0, 0, 32), FVector(WorldFeet.X, WorldFeet.Y, Ground.Z + 1), FCollisionObjectQueryParams(ECC_WorldStatic), Params) && Hit.ImpactNormal.Z >= .7f && Hit.ImpactPoint.Z > Ground.Z + 4 && FMath::Abs(Hit.ImpactPoint.Z - WorldFeet.Z) <= 64.f)
        {
            NewSample.Point = Hit.ImpactPoint;
            NewSample.Normal = Hit.ImpactNormal;
            NewSample.Component = Hit.GetComponent();
        }
        Sample = &MobFloorSamples.Add(Key, NewSample);
    }
    if (const auto *Floor = Sample->Component.Get())
    {
        const FBox Bounds = Floor->Bounds.GetBox();
        const double Z = Sample->Point.Z - ((WorldFeet.X - Sample->Point.X) * Sample->Normal.X + (WorldFeet.Y - Sample->Point.Y) * Sample->Normal.Y) / Sample->Normal.Z;
        if (WorldFeet.X >= Bounds.Min.X && WorldFeet.X <= Bounds.Max.X && WorldFeet.Y >= Bounds.Min.Y && WorldFeet.Y <= Bounds.Max.Y && Z > Ground.Z && FMath::Abs(Z - WorldFeet.Z) <= 64.)
            Ground.Z = Z;
    }
    return Ground;
}
void UDaocNetworkSubsystem::RefreshPlayers()
{
    CSV_SCOPED_TIMING_STAT(CamelotNetwork, RefreshPlayers);
    if (!EnsureRenderHost())
        return;
    if (LiveCrowd && (!bUseLiveCrowd || LiveCrowd->GetCatalog() != LiveCrowdCatalog))
    {
        for (const auto &Entry : Players)
            LiveCrowd->Remove(Entry.Key, CombatPresentations.FindRef(Entry.Key));
        LiveCrowd->Clear();
        LiveCrowd = nullptr;
    }
    const auto Convert = [](const std::string &Name, const camelot::Appearance &A) {
        FDaocCharacterAppearance R;
        R.Name = UTF8_TO_TCHAR(Name.c_str());
        R.Model = A.model;
        R.RawModelCode = A.modelCode;
        R.ModelFlags = A.modelCode & 0xf800;
        R.Race = A.race;
        R.Gender = A.gender;
        R.Realm = A.realm;
        R.Level = A.level;
        R.ClassId = A.classId;
        R.RightWeapon = A.rightWeapon;
        R.LeftWeapon = A.leftWeapon;
        R.bHoodUp = A.hood != 0;
        R.bEquipmentKnown = A.equipmentKnown;
        R.Revision = A.revision;
        R.EyeSize = A.eyeSize;
        R.LipSize = A.lipSize;
        R.EyeColor = A.eyeColor;
        R.HairColor = A.hairColor;
        R.FaceType = A.faceType;
        R.HairStyle = A.hairStyle;
        R.MoodType = A.moodType;
        R.CustomisationStep = A.customisationStep;
        for (const auto &E : A.equipment)
        {
            FDaocEquipmentAppearance Item;
            Item.Slot = E.slot;
            Item.Model = E.model;
            Item.ColorOrEmblem = E.color;
            Item.Effect = E.effect;
            Item.Extension = E.extension;
            R.Equipment.Add(Item);
        }
        return R;
    };
    bCharactersReady = Connection->Client.CharactersReady();
    RosterRevision = int32(Connection->Client.RosterRevision());
    AllowedRealm = Connection->Client.AllowedRealm();
    CheckedCharacterName = UTF8_TO_TCHAR(Connection->Client.CheckedName().c_str());
    NameCheckResult = Connection->Client.NameResult();
    CharacterSlots.Reset();
    AccountCharacters.Reset();
    for (const auto &C : Connection->Client.Characters())
    {
        auto A = Convert(C.name, C.appearance);
        AccountCharacters.Add(A);
        FDaocCharacterSlot Slot;
        Slot.Slot = C.slot;
        Slot.Appearance = A;
        Slot.Location = UTF8_TO_TCHAR(C.location.c_str());
        Slot.ClassName = UTF8_TO_TCHAR(C.className.c_str());
        Slot.RaceName = UTF8_TO_TCHAR(C.raceName.c_str());
        CharacterSlots.Add(Slot);
    }
    const auto &Snapshot = Connection->Client.Players();
    MobFloorQueriesRemaining = 16;
    TArray<TPair<int32, FDaocCharacterAppearance>> Notifications;
    for (auto It = EquipmentRenderers.CreateIterator(); It; ++It)
        if (Snapshot.find(uint16(It.Key())) == Snapshot.end())
        {
            if (It.Value())
                It.Value()->DestroyComponent();
            It.RemoveCurrent();
        }
    for (auto It = Players.CreateIterator(); It; ++It)
        if (Snapshot.find(uint16(It.Key())) == Snapshot.end())
        {
            if (LiveCrowd)
                LiveCrowd->Remove(It.Key());
            if (It.Value())
                It.Value()->DestroyComponent();
            AppearanceRevisions.Remove(It.Key());
            PlayerAppearances.Remove(It.Key());
            It.RemoveCurrent();
        }
    for (auto It = CombatPresentations.CreateIterator(); It; ++It)
        if (Snapshot.find(uint16(It.Key())) == Snapshot.end())
        {
            if (It.Value())
                It.Value()->DestroyComponent();
            It.RemoveCurrent();
        }
    for (auto It = CharacterRenderHosts.CreateIterator(); It; ++It)
        if (Snapshot.find(uint16(It.Key())) == Snapshot.end())
        {
            if (IsValid(It.Value()))
                It.Value()->Destroy();
            It.RemoveCurrent();
        }
    PlayerObjectId = Connection->Client.LocalObjectId();
    if (const auto Local = Snapshot.find(uint16(PlayerObjectId)); Local != Snapshot.end())
        ServerPlayerPosition = FVector(Local->second.x, Local->second.y, Local->second.z);
    for (const auto &Entry : Snapshot)
    {
        const auto &P = Entry.second;
        FVector Position(P.x, P.y, P.z);
        if (P.npc && P.speed)
        {
            const FVector Target(P.targetX, P.targetY, P.targetZ);
            const double Distance = FVector::Distance(Position, Target);
            if (Distance > 0)
                Position = FMath::Lerp(Position, Target, FMath::Clamp((FPlatformTime::Seconds() - P.positionTime) * P.speed / Distance, 0., 1.));
        }
        if (P.local)
            ServerPlayerPosition = Position;
        if (!AppearanceRevisions.Contains(P.objectId) || AppearanceRevisions[P.objectId] != P.appearance.revision)
        {
            const auto Appearance = Convert(P.name, P.appearance);
            // DOL can resend create/equipment packets while an NPC fights. A
            // revision alone does not change its meshes, dyes or attachment set.
            if (LiveCrowd && !LiveCrowd->HasAppearance(P.objectId, Appearance))
                LiveCrowd->Remove(P.objectId, CombatPresentations.FindRef(P.objectId));
            PlayerAppearances.Add(P.objectId, Appearance);
            AppearanceRevisions.Add(P.objectId, P.appearance.revision);
            Notifications.Emplace(P.objectId, Appearance);
        }
        const float Limit = P.npc ? MobRenderDistance : PlayerRenderDistance;
        const bool bInRange = P.local || Limit <= 0 || FVector::DistSquared(Position, ServerPlayerPosition) <= FMath::Square(Limit);
        if (!bInRange)
        {
            if (LiveCrowd)
                LiveCrowd->Remove(P.objectId);
            if (auto *Existing = Players.FindRef(P.objectId).Get())
            {
                Existing->SetHiddenInGame(true, true);
                Existing->SetComponentTickEnabled(false);
            }
            if (auto *Presentation = CombatPresentations.FindRef(P.objectId).Get())
                Presentation->SetComponentTickEnabled(false);
            continue;
        }
        auto *Mesh = Players.FindRef(P.objectId).Get();
        auto *Desired = ResolveCharacterMesh(P.appearance.model);
        auto *Skeletal = Cast<USkeletalMeshComponent>(Mesh);
        if (Mesh && ((Desired && (!Skeletal || Skeletal->GetSkeletalMeshAsset() != Desired)) || (!Desired && Skeletal)))
        {
            Mesh->DestroyComponent();
            Players.Remove(P.objectId);
            Mesh = nullptr;
        }
        if (!Mesh && Desired)
        {
            auto *Owner = CharacterRenderHosts.FindRef(P.objectId).Get();
            if (!IsValid(Owner))
            {
                FActorSpawnParameters Params;
                Params.ObjectFlags |= RF_Transient;
                Owner = GetWorld()->SpawnActor<AActor>(Params);
                if (!Owner)
                    continue;
                Owner->SetActorTickEnabled(false);
                CharacterRenderHosts.Add(P.objectId, Owner);
            }
            auto *Part = NewObject<USkeletalMeshComponent>(Owner, NAME_None, RF_Transient);
            Owner->SetRootComponent(Part);
            Part->SetSkeletalMesh(Desired);
            Part->bEnableUpdateRateOptimizations = true;
            Part->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
            Part->SetMobility(EComponentMobility::Movable);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetupAttachment(RenderHost.Get()->GetRootComponent());
            Part->RegisterComponent();
            Mesh = Part;
            Players.Add(P.objectId, Mesh);
        }
        if (!Mesh && DummyPlayerMesh)
        {
            auto *Marker = NewObject<UStaticMeshComponent>(RenderHost.Get(), NAME_None, RF_Transient);
            Mesh = Marker;
            Marker->SetStaticMesh(DummyPlayerMesh);
            Mesh->SetMobility(EComponentMobility::Movable);
            Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetupAttachment(RenderHost.Get()->GetRootComponent());
            Mesh->RegisterComponent();
            Players.Add(P.objectId, Mesh);
        }
        if (Mesh)
        {
            const bool Instanced = IsCrowdRendered(P.objectId);
            Mesh->SetHiddenInGame(Instanced || (P.local && LocalPlayerPawn.IsValid()), true);
            Mesh->SetComponentTickEnabled(!Instanced && (!P.local || !LocalPlayerPawn.IsValid()));
            if (auto *Presentation = CombatPresentations.FindRef(P.objectId).Get())
                Presentation->SetComponentTickEnabled(!P.local || !LocalPlayerPawn.IsValid());
            FVector WorldFeet = ServerToWorld(Position);
            if (P.npc)
                WorldFeet = ResolveMobFeet(WorldFeet, P.flying);
            FVector Local = RenderTransform.InverseTransformPosition(WorldFeet);
            if (!Desired)
                Local.Z += DummyCenterHeight;
            // DAoC heading zero faces south (+Y), increasing toward west (-X).
            Mesh->SetRelativeLocationAndRotation(Local, FRotator(0, 90.f + P.heading * 360.f / 4096.f, 0) + (Desired ? CharacterMeshRotationOffset : FRotator::ZeroRotator));
            const float Size = Desired ? DaocCharacterSizing::PacketSizeScale(P.appearance.size, P.npc) : 1.f;
            Mesh->SetRelativeScale3D(Desired ? GetCharacterScale(Desired) * DaocCharacterSizing::AppearanceScale(P.appearance.model, P.npc ? 0 : P.appearance.modelCode) * Size : DummyPlayerScale);
            if (auto *Character = Cast<USkeletalMeshComponent>(Mesh))
            {
                if (bPlayImportedAnimations)
                {
                    const TCHAR *Action = P.speed >= RunAnimationSpeed ? TEXT("Run") : P.speed > 0 ? TEXT("Walk")
                                                                                                   : TEXT("Idle");
                    const FString Key = FString::Printf(TEXT("%d_%s"), P.appearance.model, Action);
                    if (!ResolvedAnimations.Contains(Key))
                    {
                        const FString Path = TEXT("/Game/DAoC/Animations/A_daoc_") + Key + TEXT(".A_daoc_") + Key;
                        ResolvedAnimations.Add(Key, LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn));
                    }
                    auto *Animation = ResolvedAnimations.FindRef(Key).Get();
                    auto *Instance = Character->GetSingleNodeInstance();
                    auto *Presentation = CombatPresentations.FindRef(P.objectId).Get();
                    if (!Presentation)
                    {
                        Presentation = NewObject<UDaocCombatAnimationComponent>(Character->GetOwner());
                        Presentation->RegisterComponent();
                        CombatPresentations.Add(P.objectId, Presentation);
                    }
                    if (CombatAnimationCatalog)
                        Presentation->Catalog = CombatAnimationCatalog;
                    Presentation->SetCharacter(Character, P.appearance.model, P.objectId);
                    int32 Weapon = 0;
                    for (const auto &Item : P.appearance.equipment)
                        if (Item.slot == 10 + P.appearance.rightWeapon)
                            Weapon = Item.model;
                    Presentation->UpdateLocomotion(Animation, true, P.local ? Connection->Client.Combat().attacking : P.attacking, Weapon, !P.local && !P.attackModeKnown);
                }
                else if (Character->GetSingleNodeInstance())
                    Character->SetAnimation(nullptr);
            }
        }
    }
    for (const auto &Entry : Snapshot)
    {
        const auto &P = Entry.second;
        auto *Body = Cast<USkeletalMeshComponent>(Players.FindRef(P.objectId).Get());
        auto *Equipment = EquipmentRenderers.FindRef(P.objectId).Get();
        if (LiveCrowd)
            if (const auto *Appearance = PlayerAppearances.Find(P.objectId))
                if (LiveCrowd->HasAppearance(P.objectId, *Appearance))
                    continue;
        if (Body && Body->bHiddenInGame && !P.local)
            continue;
        if (!Body || !EquipmentCatalog || (P.local && LocalPlayerPawn.IsValid()))
        {
            if (Equipment)
                Equipment->ClearEquipment();
            continue;
        }
        if (!Equipment)
        {
            Equipment = NewObject<UDaocEquipmentComponent>(Body->GetOwner(), NAME_None, RF_Transient);
            Equipment->RegisterComponent();
            EquipmentRenderers.Add(P.objectId, Equipment);
        }
        if (const auto *Appearance = PlayerAppearances.Find(P.objectId))
        {
            Equipment->ApplyAppearance(Body, EquipmentCatalog, *Appearance);
            Body->SetHiddenInGame(P.local && LocalPlayerPawn.IsValid(), true);
        }
    }
    ReplicatedPlayerCount = 0;
    ReplicatedNPCCount = 0;
    for (const auto &Entry : Snapshot)
    {
        if (Entry.second.npc)
            ++ReplicatedNPCCount;
        else
            ++ReplicatedPlayerCount;
    }
    // Blueprint callbacks can disconnect/reconnect. Finish reading the snapshot
    // first.
    CSV_CUSTOM_STAT(CamelotNetwork, MobFloorTraces, 16 - MobFloorQueriesRemaining, ECsvCustomStatOp::Set);
    for (const auto &Notification : Notifications)
        OnAppearanceChanged.Broadcast(Notification.Key, Notification.Value);
}
// Combat bridge: all requests and confirmed state share the existing DOL
// connection.
FDaocInventoryState UDaocNetworkSubsystem::GetInventoryState() const
{
    FDaocInventoryState Result;
    if (!Connection)
        return Result;
    const auto &State = Connection->Client.Inventory();
    Result.Revision = int64(State.revision);
    Result.bEquipmentKnown = State.equipmentKnown;
    Result.bBackpackKnown = State.backpackKnown;
    Result.DetailSlot = State.detailSlot;
    Result.DetailText = UTF8_TO_TCHAR(State.detailText.c_str());
    for (const auto &Pair : State.items)
    {
        const auto &I = Pair.second;
        FDaocInventoryItem Item;
        Item.Slot = I.slot;
        Item.Name = UTF8_TO_TCHAR(I.name.c_str());
        Item.Level = I.level;
        Item.Count = I.Count();
        Item.ObjectType = I.type;
        Item.Model = I.model;
        Item.Extension = I.extension;
        Item.ColorOrEmblem = I.color;
        Item.Effect = I.effect;
        Item.Weight = I.weight;
        Item.Condition = I.condition;
        Item.Durability = I.durability;
        Item.Quality = I.quality;
        Item.Bonus = I.bonus;
        Result.Items.Add(MoveTemp(Item));
    }
    return Result;
}
bool UDaocNetworkSubsystem::IsCrowdRendered(int32 Id) const
{
    return LiveCrowd && LiveCrowd->IsInstanced(Id);
}
void UDaocNetworkSubsystem::RefreshCrowdPresentation()
{
    CSV_SCOPED_TIMING_STAT(CamelotNetwork, RefreshCrowd);
    if (!Connection)
        return;
    if (!LiveCrowdCatalog && ClientAssetCatalog)
        LiveCrowdCatalog = ClientAssetCatalog->LiveCrowdCatalog;
    if (bUseLiveCrowd && !LiveCrowdCatalog && !bCrowdCatalogLookupAttempted)
    {
        bCrowdCatalogLookupAttempted = true;
        LiveCrowdCatalog = LoadObject<UCamelotLiveCrowdCatalog>(nullptr, TEXT("/Game/DAoC/Crowd/DA_LiveCrowdCatalog.DA_LiveCrowdCatalog"), nullptr, LOAD_NoWarn);
        UE_LOG(LogTemp, Display, TEXT("Camelot live crowd: %d prepared models"), LiveCrowdCatalog ? LiveCrowdCatalog->Models.Num() : 0);
    }
    if (bUseLiveCrowd && LiveCrowdCatalog && !LiveCrowd)
    {
        LiveCrowd = NewObject<UCamelotLiveCrowdPresenter>(this);
        LiveCrowd->Initialize(GetWorld(), LiveCrowdCatalog);
    }
    const int32 Selected = Connection->Client.Combat().target;
    for (const auto &Entry : Connection->Client.Players())
    {
        const auto &P = Entry.second;
        auto *Body = Cast<USkeletalMeshComponent>(Players.FindRef(P.objectId).Get());
        auto *Animation = CombatPresentations.FindRef(P.objectId).Get();
        const auto *Appearance = PlayerAppearances.Find(P.objectId);
        if (!Body || !Appearance || !Animation)
            continue;
        // Crowded bodies are hidden but still registered lightweight transform
        // anchors.
        const bool WasInstanced = IsCrowdRendered(P.objectId);
        const bool Visible = WasInstanced || !Body->bHiddenInGame;
        const bool Protected = P.local || P.objectId == Selected || (FullCharacterDistance > 0 && FVector::DistSquared(FVector(P.x, P.y, P.z), ServerPlayerPosition) <= FMath::Square(FullCharacterDistance));
        bool Instanced = false;
        if (LiveCrowd && bUseLiveCrowd && bPlayImportedAnimations && Visible && !Protected)
            Instanced = LiveCrowd->Present(P.objectId, Body, *Appearance, Animation, P.speed >= RunAnimationSpeed ? 2 : P.speed > 0 ? 1
                                                                                                                                    : 0,
                                           LiveCrowdMaxBatches, LiveCrowdPhaseTolerance);
        else if (LiveCrowd)
            LiveCrowd->Remove(P.objectId, Animation);
        if (Instanced)
        {
            Animation->SetSharedPresentation(true);
            // Repeated hits may briefly promote a mixed pose. Retain that recent
            // full equipment without ticking it, avoiding expensive armor/material
            // reconstruction on every promotion. Ordinary crowd gear still releases.
            if (auto *Equipment = EquipmentRenderers.FindRef(P.objectId).Get())
            {
                if (Equipment->CanRetainForCrowd())
                    Equipment->SetPresentationActive(false);
                else
                {
                    Equipment->DestroyComponent();
                    EquipmentRenderers.Remove(P.objectId);
                }
            }
            Body->SetHiddenInGame(true, true);
            Body->SetComponentTickEnabled(false);
        }
        else if (WasInstanced && Visible && !(P.local && LocalPlayerPawn.IsValid()))
        {
            if (EquipmentCatalog)
            {
                auto *Equipment = EquipmentRenderers.FindRef(P.objectId).Get();
                if (!Equipment)
                {
                    Equipment = NewObject<UDaocEquipmentComponent>(Body->GetOwner(), NAME_None, RF_Transient);
                    Equipment->RegisterComponent();
                    EquipmentRenderers.Add(P.objectId, Equipment);
                }
                Equipment->ApplyAppearance(Body, EquipmentCatalog, *Appearance);
                Equipment->SetPresentationActive(true);
            }
            Body->SetHiddenInGame(false, true);
            Body->SetComponentTickEnabled(true);
        }
        if (!Instanced)
            Animation->SetSharedPresentation(false);
    }
    if (LiveCrowd)
        LiveCrowd->CollectEmptyBatches();
    LiveCrowdCharacterCount = LiveCrowd ? LiveCrowd->GetCharacterCount() : 0;
    LiveCrowdBatchCount = LiveCrowd ? LiveCrowd->GetBatchCount() : 0;
    LiveCrowdPoseCount = LiveCrowd ? LiveCrowd->GetActivePoseCount() : 0;
    LiveCrowdPoolMissCount = LiveCrowd ? LiveCrowd->GetPoolMissCount() : 0;
}
bool UDaocNetworkSubsystem::RequestMoveItem(int32 From, int32 To, int32 Count, int64 Revision)
{
    return bWorldReady && Connection && From >= 0 && From <= 255 && To >= 0 && To <= 255 && Count >= 0 && Count <= 65535 && Revision >= 0 && Connection->Client.MoveInventoryItem(uint16(From), uint16(To), uint16(Count), uint64(Revision));
}
bool UDaocNetworkSubsystem::RequestInspectItem(int32 Slot, int64 Revision)
{
    return bWorldReady && Connection && Slot >= 0 && Slot <= 255 && Revision >= 0 && Connection->Client.InspectInventoryItem(uint16(Slot), uint64(Revision), FPlatformTime::Seconds());
}
bool UDaocNetworkSubsystem::RequestUseItem(int32 Slot, int32 UseType, int64 Revision)
{
    if (!bWorldReady || !Connection || !LocalPlayerPawn.IsValid() || Slot < 0 || Slot > 255 || UseType < 0 || UseType > 2 || Revision < 0)
        return false;
    const auto *Pawn = LocalPlayerPawn.Get();
    const FVector Velocity = WorldToServer(Pawn->GetActorLocation() + Pawn->GetVelocity()) - WorldToServer(Pawn->GetActorLocation());
    if (Velocity.ContainsNaN())
        return false;
    camelot::ActionContext C;
    C.speed = uint16(FMath::Clamp(FMath::RoundToInt(Velocity.Size2D()), 0, 511));
    C.backward = FVector::DotProduct(Pawn->GetVelocity(), Pawn->GetActorForwardVector()) < 0;
    C.targetVisible = CombatLineOfSight(PlayerObjectId, Connection->Client.Combat().target);
    const auto &Ground = Connection->Client.Combat();
    if (Ground.groundKnown)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DaocItemGroundLOS), true);
        Params.AddIgnoredActor(Pawn);
        const auto Position = ServerToWorld(FVector(Ground.groundX, Ground.groundY, Ground.groundZ));
        C.groundVisible = !GetWorld()->LineTraceTestByChannel(Pawn->GetActorLocation(), Position + FVector(0, 0, 2), ECC_Visibility, Params);
    }
    return Connection->Client.UseInventoryItem(uint16(Slot), uint8(UseType), C, uint64(Revision));
}
FDaocCombatState UDaocNetworkSubsystem::GetCombatState() const
{
    FDaocCombatState S;
    if (!Connection)
        return S;
    const auto &C = Connection->Client.Combat();
    S.bStatusKnown = C.statusKnown;
    S.bDead = C.dead;
    S.bSitting = C.sitting;
    S.bAttacking = C.attacking;
    S.bSkillsReady = C.skillsReady;
    S.HealthPercent = C.health;
    S.ManaPercent = C.mana;
    S.EndurancePercent = C.endurance;
    S.ConcentrationPercent = C.concentration;
    S.TargetObjectId = C.target;
    S.CastingEffectId = C.castEffect;
    S.CastingRemaining = float(FMath::Max(0.0, C.castUntil - FPlatformTime::Seconds()));
    S.SkillsRevision = C.skillsRevision;
    S.bProgressKnown = C.progressKnown;
    S.ExperiencePercent = C.levelPermill / 10.f;
    S.SkillPoints = C.skillPoints;
    S.RealmSkillPoints = C.realmSkillPoints;
    S.RealmPoints = C.realmPoints;
    S.BountyPoints = C.bountyPoints;
    return S;
}
FDaocTrainerState UDaocNetworkSubsystem::GetTrainerState() const
{
    FDaocTrainerState S;
    if (!Connection)
        return S;
    const auto &Client = Connection->Client;
    const auto &T = Client.Trainer();
    const auto &C = Client.Combat();
    S.Revision = int64(T.revision);
    S.ObjectId = T.object;
    S.bOpen = T.open;
    S.bPending = T.pending;
    S.bSpecsKnown = T.specsKnown;
    S.bRealmKnown = T.realmKnown;
    S.SpecializationPoints = C.progressKnown ? C.skillPoints : T.specPoints;
    S.RealmPoints = C.progressKnown ? C.realmSkillPoints : T.realmPoints;
    S.Feedback = UTF8_TO_TCHAR(T.feedback.c_str());
    const auto NPC = Client.Players().find(T.object);
    if (NPC != Client.Players().end())
        S.Name = UTF8_TO_TCHAR(NPC->second.name.c_str());
    auto Copy = [&](const auto &Lines, bool IsRealmAbility, auto &Out) {
        for (const auto &Line : Lines)
        {
            FDaocTrainerLine R;
            R.Index = Line.index;
            R.Name = UTF8_TO_TCHAR(Line.name.c_str());
            R.Rank = Line.level;
            R.NextCost = Line.nextCost;
            R.bAvailable = Line.available;
            R.MaximumRank = Client.MaxTrainingRank(Line, IsRealmAbility);
            Out.Add(MoveTemp(R));
        }
    };
    Copy(T.specs, false, S.Specializations);
    Copy(T.realm, true, S.RealmAbilities);
    return S;
}
bool UDaocNetworkSubsystem::RequestInteract(int32 ObjectId)
{
    if (!Connection || ObjectId <= 0 || ObjectId > 65535)
        return false;
    return Connection->Client.Interact(uint16(ObjectId), FPlatformTime::Seconds());
}
bool UDaocNetworkSubsystem::RequestTraining(bool IsRealmAbility, int32 Index, int32 DesiredRank, int64 Revision)
{
    return Connection && Index >= 0 && Index <= 255 && DesiredRank >= 0 && DesiredRank <= 255 && Revision >= 0 && Connection->Client.Train(IsRealmAbility, uint8(Index), uint8(DesiredRank), uint64(Revision), FPlatformTime::Seconds());
}
void UDaocNetworkSubsystem::CloseTrainer()
{
    if (Connection)
        Connection->Client.CloseTrainer();
}
TArray<FDaocCombatAbility> UDaocNetworkSubsystem::GetCombatAbilities() const
{
    TArray<FDaocCombatAbility> Result;
    if (!Connection)
        return Result;
    const auto &C = Connection->Client.Combat();
    const double Now = FPlatformTime::Seconds();
    int32 First = 0;
    while (First < int32(C.skills.size()) && C.skills[First].type == camelot::SkillType::Specialization)
        ++First;
    for (const auto &Skill : C.skills)
    {
        FDaocCombatAbility A;
        A.Kind = EDaocAbilityKind(Skill.type);
        A.ServerIndex = Skill.index;
        A.Name = UTF8_TO_TCHAR(Skill.name.c_str());
        A.Level = Skill.level;
        A.Icon = Skill.icon;
        A.SpecialField = Skill.special;
        A.Bonus = Skill.bonus;
        auto It = C.skillCooldowns.find(uint16(Skill.index - First));
        if (Skill.index >= First && It != C.skillCooldowns.end())
            A.CooldownRemaining = float(FMath::Max(0.0, It->second - Now));
        Result.Add(A);
    }
    for (const auto &Entry : C.spellLines)
        for (const auto &Spell : Entry.second.spells)
        {
            FDaocCombatAbility A;
            A.Kind = EDaocAbilityKind::ListSpell;
            A.SpellLine = Entry.first;
            A.LineName = UTF8_TO_TCHAR(Entry.second.name.c_str());
            A.ServerIndex = Spell.index;
            A.Name = UTF8_TO_TCHAR(Spell.name.c_str());
            A.Level = Spell.level;
            A.Icon = Spell.icon;
            auto It = C.spellCooldowns.find(uint16((Entry.first << 8) | Spell.index));
            if (It != C.spellCooldowns.end())
                A.CooldownRemaining = float(FMath::Max(0.0, It->second - Now));
            Result.Add(A);
        }
    return Result;
}
TArray<FDaocCombatEffect> UDaocNetworkSubsystem::GetCombatEffects() const
{
    TArray<FDaocCombatEffect> Result;
    if (Connection)
        for (const auto &Effect : Connection->Client.Combat().effects)
        {
            FDaocCombatEffect E;
            E.InternalId = Effect.id;
            E.Icon = Effect.icon;
            E.Name = UTF8_TO_TCHAR(Effect.name.c_str());
            E.ReportedSeconds = Effect.seconds;
            E.bCancelable = Effect.index != 255;
            Result.Add(E);
        }
    return Result;
}
TArray<FDaocCombatTarget> UDaocNetworkSubsystem::GetCombatTargets() const
{
    TArray<FDaocCombatTarget> Result;
    if (!Connection)
        return Result;
    for (const auto &Entry : Connection->Client.Players())
    {
        const auto &P = Entry.second;
        FDaocCombatTarget T;
        T.ObjectId = P.objectId;
        T.Name = UTF8_TO_TCHAR(P.name.c_str());
        T.GuildName = UTF8_TO_TCHAR(P.guildName.c_str());
        T.bLocal = P.local;
        T.bNPC = P.npc;
        T.Position = ServerToWorld(FVector(P.x, P.y, P.z)) + FVector(0, 0, DummyCenterHeight);
        if (P.local && LocalPlayerPawn.IsValid())
            T.Position = LocalPlayerPawn->GetActorLocation();
        else if (const auto *Component = Cast<UPrimitiveComponent>(Players.FindRef(P.objectId).Get()))
            T.Position = Component->Bounds.Origin;
        const auto H = Connection->Client.Combat().entityHealth.find(P.objectId);
        if (H != Connection->Client.Combat().entityHealth.end())
            T.HealthPercent = H->second;
        if (P.local && Connection->Client.Combat().statusKnown)
            T.HealthPercent = Connection->Client.Combat().health;
        Result.Add(T);
    }
    return Result;
}
bool UDaocNetworkSubsystem::CombatLineOfSight(int32 Source, int32 Target) const
{
    FDaocCombatTarget A, B;
    if (!GetWorld() || !ResolveCombatTarget(Source, A) || !ResolveCombatTarget(Target, B))
        return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DaocCombatLOS), true);
    if (LocalPlayerPawn.IsValid())
        Params.AddIgnoredActor(LocalPlayerPawn.Get());
    // Do not ignore the zone actor: its terrain/building components must occlude
    // sight.
    return !GetWorld()->LineTraceTestByChannel(A.Position, B.Position, ECC_Visibility, Params);
}
bool UDaocNetworkSubsystem::ResolveCombatTarget(int32 ObjectId, FDaocCombatTarget &T) const
{
    if (!Connection || ObjectId < 1 || ObjectId > 65535)
        return false;
    const auto &Snapshot = Connection->Client.Players();
    const auto It = Snapshot.find(uint16(ObjectId));
    if (It == Snapshot.end())
        return false;
    const auto &P = It->second;
    T.ObjectId = P.objectId;
    T.Name = UTF8_TO_TCHAR(P.name.c_str());
    T.bLocal = P.local;
    T.bNPC = P.npc;
    T.GuildName = UTF8_TO_TCHAR(P.guildName.c_str());
    T.Position = ServerToWorld(FVector(P.x, P.y, P.z)) + FVector(0, 0, DummyCenterHeight);
    if (P.local && LocalPlayerPawn.IsValid())
        T.Position = LocalPlayerPawn->GetActorLocation();
    else if (const auto *Mesh = Cast<UPrimitiveComponent>(Players.FindRef(P.objectId).Get()))
        T.Position = Mesh->Bounds.Origin;
    const auto Health = Connection->Client.Combat().entityHealth.find(P.objectId);
    if (Health != Connection->Client.Combat().entityHealth.end())
        T.HealthPercent = Health->second;
    if (P.local && Connection->Client.Combat().statusKnown)
        T.HealthPercent = Connection->Client.Combat().health;
    return true;
}
bool UDaocNetworkSubsystem::SelectCombatTarget(int32 ObjectId)
{
    return Connection && ObjectId >= 0 && ObjectId <= 65535 && Connection->Client.SelectTarget(uint16(ObjectId), CombatLineOfSight(PlayerObjectId, ObjectId));
}
bool UDaocNetworkSubsystem::RequestAttack(bool bEnabled)
{
    return Connection && Connection->Client.SetAttack(bEnabled);
}
bool UDaocNetworkSubsystem::RequestRelease()
{
    return Connection && Connection->Client.Release();
}
bool UDaocNetworkSubsystem::RequestAbility(const FDaocCombatAbility &Ability, AActor *ActingPawn)
{
    if (!Connection || !ActingPawn || ActingPawn != LocalPlayerPawn.Get())
        return false;
    const auto Abilities = GetCombatAbilities();
    const auto *Current = Abilities.FindByPredicate([&](const FDaocCombatAbility &A) { return A.Kind == Ability.Kind && A.ServerIndex == Ability.ServerIndex && A.SpellLine == Ability.SpellLine && A.Name == Ability.Name && A.Level == Ability.Level && A.Icon == Ability.Icon; });
    if (!Current || Current->CooldownRemaining > 0 || Current->Kind == EDaocAbilityKind::Specialization)
        return false;
    FVector Feet = ActingPawn->GetActorLocation();
    if (const auto *Character = Cast<ACharacter>(ActingPawn))
        Feet.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2;
    const FVector P = WorldToServer(Feet);
    if (P.ContainsNaN() || P.X < 0 || P.Y < 0 || P.Z < 1 || P.X > 4294967295.0 || P.Y > 4294967295.0 || P.Z > 65535)
        return false;
    const FVector Velocity = WorldToServer(Feet + ActingPawn->GetVelocity()) - P;
    const FVector Forward = WorldToServer(Feet + ActingPawn->GetActorForwardVector()) - P;
    camelot::ActionContext C;
    C.x = uint32(P.X);
    C.y = uint32(P.Y);
    C.z = uint16(P.Z);
    C.heading = uint16(FMath::RoundToInt(FMath::Atan2(-Forward.X, Forward.Y) * 4096 / (2 * PI)) & 4095);
    C.speed = uint16(FMath::Clamp(FMath::RoundToInt(Velocity.Size2D()), 0, 511));
    C.backward = FVector::DotProduct(Velocity, Forward) < 0;
    C.targetVisible = CombatLineOfSight(PlayerObjectId, Connection->Client.Combat().target);
    const auto &Ground = Connection->Client.Combat();
    if (Ground.groundKnown)
    {
        const FVector WorldGround = ServerToWorld(FVector(Ground.groundX, Ground.groundY, Ground.groundZ));
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DaocGroundCastLOS), true);
        Params.AddIgnoredActor(ActingPawn);
        C.groundVisible = !GetWorld()->LineTraceTestByChannel(ActingPawn->GetActorLocation(), WorldGround + FVector(0, 0, 2), ECC_Visibility, Params);
    }
    const double Now = FPlatformTime::Seconds();
    bool Sent = false;
    if (Current->Kind == EDaocAbilityKind::ListSpell)
        Sent = Connection->Client.UseSpell(uint8(Current->SpellLine), uint8(Current->ServerIndex), C, Now);
    else
        Sent = Connection->Client.UseSkill(uint16(Current->ServerIndex), C, Now);
    if (Sent)
        LastSubmittedPosition = FVector(C.x, C.y, C.z);
    return Sent;
}
bool UDaocNetworkSubsystem::SetCombatGroundTarget(FVector WorldPosition)
{
    if (!Connection || WorldPosition.ContainsNaN() || !LocalPlayerPawn.IsValid())
        return false;
    const FVector P = WorldToServer(WorldPosition);
    if (P.X < 0 || P.Y < 0 || P.Z < 0 || P.X > 2147483647.0 || P.Y > 2147483647.0 || P.Z > 65535)
        return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DaocGroundLOS), true);
    Params.AddIgnoredActor(LocalPlayerPawn.Get());
    const bool Visible = !GetWorld()->LineTraceTestByChannel(LocalPlayerPawn->GetActorLocation(), WorldPosition + FVector(0, 0, 2), ECC_Visibility, Params);
    if (!Connection->Client.SetGroundTarget(uint32(P.X), uint32(P.Y), uint16(P.Z), Visible))
        return false;
    return true;
}
bool UDaocNetworkSubsystem::RequestCancelEffect(int32 InternalId)
{
    return Connection && InternalId > 0 && InternalId <= 65535 && Connection->Client.CancelEffect(uint16(InternalId));
}
void UDaocNetworkSubsystem::DispatchCombatEvents()
{
    CSV_SCOPED_TIMING_STAT(CamelotNetwork, CombatEvents);
    if (!Connection)
        return;
    const auto Generation = CombatSessionGeneration;
    // The first server event can precede the pawn's tick after login/model
    // changes.
    if (auto *Pawn = Cast<ADaocPlayerCharacter>(LocalPlayerPawn.Get()))
        if (const auto *Appearance = PlayerAppearances.Find(PlayerObjectId))
        {
            Pawn->SetCharacterModel(Appearance->Model);
            if (CombatAnimationCatalog)
                Pawn->CombatAnimations->Catalog = CombatAnimationCatalog;
            Pawn->CombatAnimations->SetCharacter(Pawn->GetMesh(), Appearance->Model, PlayerObjectId);
        }
    for (const auto &C : Connection->Client.TakeCombatEvents())
    {
        if (!Connection || Generation != CombatSessionGeneration)
            return;
        if (C.type == camelot::CombatEventType::LineOfSight)
        {
            Connection->Client.ReplyLineOfSight(C.source, C.target, CombatLineOfSight(C.source, C.target));
            continue;
        }
        FDaocCombatEvent E;
        E.Kind = EDaocCombatEventKind(C.type);
        E.SourceObjectId = C.source;
        E.TargetObjectId = C.target;
        E.EffectId = C.effect;
        E.StyleId = C.style;
        E.WeaponId = C.weapon;
        E.ShieldId = C.shield;
        E.ResultCode = C.result;
        E.Stance = C.stance;
        E.HealthPercent = C.health;
        E.ChatType = C.chatType;
        E.Duration = float(C.seconds);
        E.bNoSound = C.noSound;
        E.SoundZone = C.zone;
        E.SoundServerPosition = FVector(C.x, C.y, C.z);
        E.SoundRadius = C.radius;
        E.Text = UTF8_TO_TCHAR(C.text.c_str());
        const auto PresentEvent = [&](int32 Id) {
            // The pawn receives the original event through OnCombatEvent. Its
            // hidden network anchor must not also evaluate the same local hits.
            if (Id == PlayerObjectId && LocalPlayerPawn.IsValid())
                return;
            if (auto *Presentation = CombatPresentations.FindRef(Id).Get())
                Presentation->HandleEvent(E);
        };
        PresentEvent(E.SourceObjectId);
        if (E.TargetObjectId != E.SourceObjectId)
            PresentEvent(E.TargetObjectId);
        PlayCombatSound(E);
        if (GetWorld())
            if (auto *Effects = GetWorld()->GetSubsystem<UDaocEffectSubsystem>())
            {
                const auto Anchor = [&](int32 Id) -> USceneComponent * {
                    if (Id == PlayerObjectId && LocalPlayerPawn.IsValid())
                        if (auto *Body = LocalPlayerPawn->FindComponentByClass<USkeletalMeshComponent>())
                            return Body;
                    return Players.FindRef(Id);
                };
                Effects->HandleCombatEvent(E, Anchor(E.SourceObjectId), Anchor(E.TargetObjectId), E.SourceObjectId == PlayerObjectId || E.TargetObjectId == PlayerObjectId, LiveCrowd);
            }
        RecordChatEvent(E);
        OnCombatEvent.Broadcast(E);
    }
}
void UDaocNetworkSubsystem::RecordChatEvent(const FDaocCombatEvent &Event)
{
    if (Event.Kind != EDaocCombatEventKind::Message || Event.Text.IsEmpty())
        return;
    FDaocChatMessage Message;
    Message.SequenceId = ++ChatRevision;
    Message.Text = Event.Text.Left(2048);
    Message.ChatType = Event.ChatType;
    ChatMessages.Add(MoveTemp(Message));
    if (ChatMessages.Num() > 100)
        ChatMessages.RemoveAt(0);
}
void UDaocNetworkSubsystem::PlayCombatSound(const FDaocCombatEvent &E)
{
    // Most battlefield events have no sound mapping. Do not rebuild the entire
    // target list for every melee, movement, death or status event.
    if (E.bNoSound || !GetWorld() || (E.Kind != EDaocCombatEventKind::Sound && E.Kind != EDaocCombatEventKind::Cast && E.Kind != EDaocCombatEventKind::SpellEffect))
        return;
    if (!CombatAnimationCatalog)
        CombatAnimationCatalog = LoadObject<UDaocCombatAnimationCatalog>(nullptr, TEXT("/Game/DAoC/Catalogs/DA_CombatAnimations.DA_CombatAnimations"), nullptr, LOAD_NoWarn);
    if (!CombatAnimationCatalog)
        return;
    const auto Play = [&](int32 Id, FVector Location, int32 Radius) { PlayCombatSoundAt(Id, Location, Radius); };
    if (E.Kind == EDaocCombatEventKind::Sound)
    {
        if (E.SoundZone == NetworkZoneId)
            Play(E.EffectId, ServerToWorld(FVector(E.SoundServerPosition.X + ServerZoneOrigin.X, E.SoundServerPosition.Y + ServerZoneOrigin.Y, E.SoundServerPosition.Z)), E.SoundRadius);
        return;
    }
    FDaocCombatTarget Source;
    if (!ResolveCombatTarget(E.SourceObjectId, Source))
        return;
    if (E.Kind == EDaocCombatEventKind::Cast)
    {
        if (const auto *Spell = CombatAnimationCatalog->Spells.Find(E.EffectId))
            Play(Spell->GatherSound, Source.Position, 0);
    }
    if (E.Kind == EDaocCombatEventKind::SpellEffect)
    {
        if (const auto *Spell = CombatAnimationCatalog->Spells.Find(E.EffectId))
        {
            Play(Spell->CastSound, Source.Position, 0);
            FDaocCombatTarget Target;
            if (ResolveCombatTarget(E.TargetObjectId, Target))
                PlayCombatSoundAt(E.ResultCode ? Spell->HitSound : Spell->MissSound, Target.Position, 0, E.Duration);
        }
    }
}
void UDaocNetworkSubsystem::PlayCombatSoundAt(int32 Id, FVector Location, int32 Radius, float Delay)
{
    if (!GetWorld() || !CombatAnimationCatalog || Id <= 0)
        return;
    if (Delay > 0)
    {
        const auto Generation = CombatSessionGeneration;
        const auto Zone = NetworkZoneId;
        FTimerHandle Handle;
        GetWorld()->GetTimerManager().SetTimer(Handle,
                                               FTimerDelegate::CreateWeakLambda(this,
                                                                                [this, Generation, Zone, Id, Location, Radius] {
                                                                                    if (Connection && Generation == CombatSessionGeneration && Zone == NetworkZoneId)
                                                                                        PlayCombatSoundAt(Id, Location, Radius);
                                                                                }),
                                               Delay, false);
        return;
    }
    const auto *Entry = CombatAnimationCatalog->Sounds.Find(Id);
    if (!Entry)
        return;
    auto *Sound = Entry->Sound.LoadSynchronous();
    if (!Sound)
        return;
    const float Scale = FVector::Distance(ServerToWorld(FVector::ZeroVector), ServerToWorld(FVector(1, 0, 0)));
    auto *Attenuation = NewObject<USoundAttenuation>(this);
    auto &Settings = Attenuation->Attenuation;
    Settings.bAttenuate = true;
    Settings.bSpatialize = true;
    Settings.AttenuationShape = EAttenuationShape::Sphere;
    Settings.AttenuationShapeExtents = FVector(Entry->MinDistance * Scale);
    Settings.FalloffDistance = FMath::Max(1.f, (Radius > 0 ? Radius : Entry->MaxDistance) * Scale - Settings.AttenuationShapeExtents.X);
    UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, 1, 1, 0, Attenuation);
}
