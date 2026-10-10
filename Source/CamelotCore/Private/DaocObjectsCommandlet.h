#pragma once
#include "Commandlets/Commandlet.h"
#include "DaocObjectsCommandlet.generated.h"
class UStaticMesh;
class UTexture2D;

/// Editor commandlet importing prepared static scenery and texture manifests.
UCLASS()
class UDaocObjectsCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    /// Initializes default settings and owned components.
    UDaocObjectsCommandlet();
    /// Prepares static mesh collision needed for walkable imported scenery.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Import")
    static bool EnableWalkingCollision(UStaticMesh *Mesh);
    /// Imports or resolves the named source texture as an Unreal asset.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Import")
    static UTexture2D *ImportTextureAsset(const FString &Directory, const FString &Filename);
    /// Imports an explicit prepared manifest in the editor; reimporting is opt-in.
    UFUNCTION(BlueprintCallable, Category = "DAoC|Import")
    static bool ImportManifest(const FString &File, bool bReimport = false);
    /// Runs this commandlet with explicit parameters and returns its process status.
    virtual int32 Main(const FString &Params) override;
};
