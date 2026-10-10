#pragma once
#include "CoreMinimal.h"
#include "DaocWeatherTypes.generated.h"

/// Visual precipitation choice independent of server weather simulation.
UENUM(BlueprintType)
enum class EDaocWeatherType : uint8
{
    Sunny,
    Rain,
    Snow
};
/// Selects fixed, server-driven or combined cosmetic-cycle weather behavior.
UENUM(BlueprintType)
enum class EDaocWeatherPolicy : uint8
{
    AlwaysSunny,
    Persistent,
    Server,
    // A server storm over the player overrides the local cycle; clear skies resume it.
    ServerAndCycle
};

/// Per-zone weather policy and visual transition settings.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocZoneWeatherSettings
{
    GENERATED_BODY()
    /// Built-in presets: zone 0 sunny, zone 100 persistent snow, other zones server rain.
    /// Disable this to author this zone's policy and precipitation choices below.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
    bool bUseZoneDefaults = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
    EDaocWeatherPolicy Policy = EDaocWeatherPolicy::Server;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
    EDaocWeatherType PersistentType = EDaocWeatherType::Snow;
    /// DOL transmits intensity, not rain versus snow. The zone chooses its climate.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
    EDaocWeatherType ServerPrecipitation = EDaocWeatherType::Rain;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
    float Intensity = .55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "30"))
    float CycleSeconds = 900.f;
    /// Repeated entries increase that pattern's probability, e.g. Sunny, Sunny, Rain.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
    TArray<EDaocWeatherType> CyclePatterns = {EDaocWeatherType::Sunny, EDaocWeatherType::Rain};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
    int32 CycleSeed = 173;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = ".1"))
    float TransitionSeconds = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
    bool bSuppressParticlesUnderShelter = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
    bool bAffectLighting = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
    float StormSunMultiplier = .75f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = ".1"))
    float AddedFogDensity = .008f;
    /// Optional material buildup. Snowfall does not paint the terrain white by default.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
    float SnowSurfaceAmount = 0.f;
};

/// Decoded server weather front; coordinates remain in server units.
USTRUCT(BlueprintType)
struct CAMELOTCORE_API FDaocServerWeather
{
    GENERATED_BODY()
    /// True after a weather front has been received for the current session.
    UPROPERTY(BlueprintReadOnly, Category = "Weather")
    bool bKnown = false;
    /// Server region containing this weather front; distinct from a zone ID.
    UPROPERTY(BlueprintReadOnly, Category = "Weather")
    int32 RegionId = 0;
    /// Weather-front position in server coordinate units.
    UPROPERTY(BlueprintReadOnly, Category = "Weather")
    float FrontX = 0;
    /// Weather-front width in server coordinate units.
    UPROPERTY(BlueprintReadOnly, Category = "Weather")
    float Width = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Weather")
    float Speed = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Weather")
    float Intensity = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Weather")
    int32 FogDiffusion = 0;
};
