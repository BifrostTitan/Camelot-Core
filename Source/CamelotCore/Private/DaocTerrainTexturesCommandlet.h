#pragma once

#include "Commandlets/Commandlet.h"
#include "DaocTerrainTexturesCommandlet.generated.h"
class UDaocZoneProfile;
class UTexture2D;

/// Optional headless import for extracted zone tiles.
UCLASS()
class UDaocTerrainTexturesCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocTerrainTexturesCommandlet();
    /// Runs this commandlet with explicit parameters and returns its process status.
    virtual int32 Main(const FString &Params) override;

    /// Editor scripts must wait for shader/texture compilation before capturing.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Editor")
    static void FinishAssetCompilation();
    /// Assigns a prepared terrain texture to the supplied zone profile.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Editor")
    static bool SetProfileTerrainTexture(UDaocZoneProfile *Profile, UTexture2D *Texture);
    /// Stores the validated square mask samples in a zone profile.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Editor")
    static bool SetProfileGroundCoverMask(UDaocZoneProfile *Profile, int32 Width, const TArray<uint8> &Samples);
    /// Advanced Copy remaps the whole package graph; plain duplication retains pack dependencies.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Editor")
    static bool CopyFoliagePackages(const TArray<FString> &Packages, const FString &DestinationRoot);
    /// Rewrites target foliage references to the supplied prepared library root.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Editor")
    static int32 RemapFoliageReferences(UObject *Target, const FString &LibraryRoot);

    /// The shared runtime host is driven solely by the network subsystem.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Editor")
    static bool ClearRuntimeLevelScript(UWorld *World);
};
