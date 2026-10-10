#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DaocZoneToolboxSettings.generated.h"
class UDaocZoneProfile;
class UStaticMesh;
/// Editor preferences contain paths only; no login credentials belong in the toolbox.
UCLASS(Config = EditorPerProjectUserSettings)
class UDaocZoneToolboxSettings : public UObject
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, Config, Category = "Zone", meta = (ClampMin = "0", ClampMax = "65535"))
    int32 ZoneId = 0;
    UPROPERTY(EditAnywhere, Config, Category = "Zone")
    FString DisplayName;
    UPROPERTY(EditAnywhere, Config, Category = "Zone")
    FString ExportRoot = TEXT("D:/extract173/zones");
    UPROPERTY(EditAnywhere, Config, Category = "Zone")
    FString ProfileRoot = TEXT("/Game/DAoC/ZoneProfiles");
    UPROPERTY(EditAnywhere, Category = "Zone")
    TObjectPtr<UDaocZoneProfile> Profile;
    UPROPERTY(EditAnywhere, Category = "Replacement")
    FName SourceNif;
    UPROPERTY(EditAnywhere, Category = "Replacement")
    TObjectPtr<UStaticMesh> ReplacementMesh;
    UPROPERTY(EditAnywhere, Category = "Replacement", meta = (ClampMin = "0.25", ClampMax = "2"))
    float HeightMultiplier = 1;
    UPROPERTY(EditAnywhere, Category = "Replacement", meta = (ClampMin = "0.25", ClampMax = "2"))
    float WidthMultiplier = 1;
    UPROPERTY(EditAnywhere, Category = "Replacement", meta = (Units = "cm", ToolTip = "Negative values lower this NIF replacement into the terrain. Zero keeps its normal placement."))
    float GroundPlacementOffset = 0;
    UPROPERTY(EditAnywhere, Config, Category = "Import")
    FString SourcePluginDirectory = TEXT("D:/CamelotCore");
    UPROPERTY(EditAnywhere, Config, Category = "Import")
    FString ClientDirectory = TEXT("C:/Mythic/Catacombs");
    UPROPERTY(EditAnywhere, Config, Category = "Import")
    FString ExtractRoot = TEXT("D:/extract173");
    UPROPERTY(EditAnywhere, Config, Category = "Import")
    FString PreparedObjectsDirectory = TEXT("D:/DAoCMigration/toolbox/objects");
    UPROPERTY(EditAnywhere, Config, Category = "Import")
    FString PythonExecutable = TEXT("C:/Users/proje/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe");
    UPROPERTY(EditAnywhere, Config, Category = "Import", meta = (FilePathFilter = "json"))
    FString CharacterManifest;
    UPROPERTY(EditAnywhere, Config, Category = "Import", meta = (FilePathFilter = "json"))
    FString AnimationManifest;
    UPROPERTY(EditAnywhere, Config, Category = "Import")
    bool bReimport = false;
    UPROPERTY(EditAnywhere, Config, Category = "Performance")
    bool bEnableNaniteForSelectedReplacement = false;
};
