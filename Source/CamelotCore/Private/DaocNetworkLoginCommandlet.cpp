#include "DaocNetworkLoginCommandlet.h"
#include "DaocNetworkSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
UDaocNetworkLoginCommandlet::UDaocNetworkLoginCommandlet()
{
    IsClient = true;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}
int32 UDaocNetworkLoginCommandlet::Main(const FString &Params)
{
    FString Config;
    FParse::Value(*Params, TEXT("Config="), Config);
    float Hold = 10;
    FParse::Value(*Params, TEXT("Hold="), Hold);
    if (Config.IsEmpty() || !FMath::IsFinite(Hold) || Hold < 0 || Hold > 300)
        return 1;
    auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto *Instance = NewObject<UGameInstance>();
    Instance->AddToRoot();
    auto *Session = NewObject<UDaocNetworkSubsystem>(Instance);
    Session->AddToRoot();
    Session->SetRenderingContext(World, FTransform::Identity);
    int32 Result = 1;
    if (Session->LoadLoginConfig(Config))
    {
        Session->Connect();
        const double Deadline = FPlatformTime::Seconds() + Session->RequestTimeoutSeconds + Hold;
        double PlayingAt = 0;
        while (FPlatformTime::Seconds() < Deadline)
        {
            Session->Tick(.01f);
            if (Session->ConnectionState == TEXT("Failed"))
                break;
            if (Session->ConnectionState == TEXT("Characters") && !Session->LastNetworkError.IsEmpty())
            {
                for (const auto &C : Session->AccountCharacters)
                    UE_LOG(LogTemp, Display, TEXT("Available character: %s"), *C.Name);
                break;
            }
            if (Session->ConnectionState == TEXT("Playing"))
            {
                if (!PlayingAt)
                    PlayingAt = FPlatformTime::Seconds();
                if (FPlatformTime::Seconds() - PlayingAt >= Hold)
                {
                    UE_LOG(LogTemp, Display, TEXT("DOL login validated: object %d, players %d, NPCs %d, held %.1fs"), Session->PlayerObjectId, Session->ReplicatedPlayerCount, Session->ReplicatedNPCCount, Hold);
                    Result = 0;
                    break;
                }
            }
            FPlatformProcess::Sleep(.01f);
        }
    }
    if (Result)
        UE_LOG(LogTemp, Error, TEXT("DOL login test failed: state %s, %s"), *Session->ConnectionState, *Session->LastNetworkError);
    Session->Disconnect();
    Session->RemoveFromRoot();
    World->DestroyWorld(false);
    Instance->RemoveFromRoot();
    return Result;
}
