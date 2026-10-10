#include "DaocTerrainTextures.h"

#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/Material.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "String/LexFromString.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#endif

namespace DaocTerrainTextures
{
namespace
{
bool ReadGrid(const FString &Directory, int32 &Columns, int32 &Rows)
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Directory, TEXT("sector.dat"))))
        return false;
    bool bSize = false;
    Columns = Rows = INDEX_NONE;
    for (FString Line : Lines)
    {
        Line.TrimStartAndEndInline();
        if (Line.StartsWith(TEXT("[")))
        {
            bSize = Line.Equals(TEXT("[sectorsize]"), ESearchCase::IgnoreCase);
            continue;
        }
        FString Key, Value;
        if (!bSize || !Line.Split(TEXT("="), &Key, &Value))
            continue;
        Key.TrimStartAndEndInline();
        int32 Comment;
        if (Value.FindChar(';', Comment))
            Value.LeftInline(Comment);
        Value.TrimStartAndEndInline();
        int32 Number = 0;
        if (!LexTryParseString(Number, *Value))
            continue;
        if (Key.Equals(TEXT("sizex"), ESearchCase::IgnoreCase))
            Columns = Number;
        if (Key.Equals(TEXT("sizey"), ESearchCase::IgnoreCase))
            Rows = Number;
    }
    return Columns > 0 && Rows > 0 && Columns <= 16 && Rows <= 16;
}

FString FindTile(const FString &Directory, int32 X, int32 Y)
{
    const FString Base = FString::Printf(TEXT("tex%02d-%02d"), X, Y);
    for (const TCHAR *Extension : {TEXT("png"), TEXT("bmp"), TEXT("tga")})
    {
        const FString Path = FPaths::Combine(Directory, Base + TEXT(".") + Extension);
        if (FPaths::FileExists(Path))
            return Path;
    }
    return FString();
}
} // namespace

bool LoadAtlasPixels(const FString &Directory, int32 &Width, int32 &Height, TArray<FColor> &Pixels, FString &Error)
{
    int32 Columns = INDEX_NONE, Rows = INDEX_NONE;
    if (!ReadGrid(Directory, Columns, Rows))
    {
        // Newer layered zones declare 0x0 sectors, but ship an 8x8 LOD
        // color grid. Migration converts that grid to ordinary color tiles.
        // Accept only an explicitly zero grid with every fallback tile present.
        bool bCompleteFallback = Columns == 0 && Rows == 0;
        for (int32 Y = 0; Y < 8 && bCompleteFallback; ++Y)
            for (int32 X = 0; X < 8; ++X)
                if (FindTile(Directory, X, Y).IsEmpty())
                {
                    bCompleteFallback = false;
                    break;
                }
        if (!bCompleteFallback)
        {
            Error = TEXT("sector.dat requires valid [sectorsize] sizex/sizey (1..16), or a complete 8x8 color fallback for a zero grid.");
            return false;
        }
        Columns = Rows = 8;
    }
    auto &Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TArray<FColor> Atlas;
    int32 TileWidth = 0, TileHeight = 0, AtlasWidth = 0, AtlasHeight = 0;
    for (int32 Y = 0; Y < Rows; ++Y)
    {
        for (int32 X = 0; X < Columns; ++X)
        {
            const FString Path = FindTile(Directory, X, Y);
            TArray<uint8> Bytes;
            if (Path.IsEmpty() || !FFileHelper::LoadFileToArray(Bytes, *Path))
            {
                Error = FString::Printf(TEXT("Missing tex%02d-%02d.png/.bmp/.tga. Convert DDS tiles to PNG first."), X, Y);
                return false;
            }
            const EImageFormat Format = Module.DetectImageFormat(Bytes.GetData(), Bytes.Num());
            auto Wrapper = Module.CreateImageWrapper(Format);
            TArray64<uint8> Raw;
            if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
            {
                Error = FString::Printf(TEXT("Cannot decode terrain tile %s."), *Path);
                return false;
            }
            const int32 W = Wrapper->GetWidth(), H = Wrapper->GetHeight();
            if (W <= 0 || H <= 0 || W > 8192 / Columns || H > 8192 / Rows || int64(W) * H * 4 != Raw.Num())
            {
                Error = FString::Printf(TEXT("Invalid or oversized terrain tile %s (atlas limit 8192x8192)."), *Path);
                return false;
            }
            if (X == 0 && Y == 0)
            {
                TileWidth = W;
                TileHeight = H;
                AtlasWidth = W * Columns;
                AtlasHeight = H * Rows;
                Atlas.SetNumUninitialized(AtlasWidth * AtlasHeight);
            }
            else if (W != TileWidth || H != TileHeight)
            {
                Error = FString::Printf(TEXT("Terrain tile dimensions differ: %s."), *Path);
                return false;
            }
            for (int32 Row = 0; Row < H; ++Row)
            {
                FColor *Dest = Atlas.GetData() + (Y * H + Row) * AtlasWidth + X * W;
                FMemory::Memcpy(Dest, Raw.GetData() + int64(Row) * W * 4, W * sizeof(FColor));
                for (int32 Col = 0; Col < W; ++Col)
                    Dest[Col].A = 255;
            }
        }
    }
    Width = AtlasWidth;
    Height = AtlasHeight;
    Pixels = MoveTemp(Atlas);
    Error.Reset();
    return true;
}

UTexture2D *ImportAtlas(const FString &Directory, FString &Error)
{
    int32 Width = 0, Height = 0;
    TArray<FColor> Pixels;
    if (!LoadAtlasPixels(Directory, Width, Height, Pixels, Error))
        return nullptr;
#if WITH_EDITOR
    FString SourcePath = FPaths::ConvertRelativePathToFull(Directory);
    FPaths::NormalizeDirectoryName(SourcePath);
    const FString Name = FString::Printf(TEXT("T_DaocTerrain_%08X"), FCrc::StrCrc32(*SourcePath.ToLower()));
    UPackage *Package = CreatePackage(*(TEXT("/Game/DAoC/ZoneTextures/") + Name));
    UTexture2D *Texture = FindObject<UTexture2D>(Package, *Name);
    if (!Texture && FPackageName::DoesPackageExist(Package->GetName()))
    {
        Texture = LoadObject<UTexture2D>(nullptr, *(Package->GetName() + TEXT(".") + Name));
    }
    const bool bNewAsset = Texture == nullptr;
    if (bNewAsset)
        Texture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
    Texture->Modify();
    Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8 *>(Pixels.GetData()));
    Texture->SRGB = true;
    Texture->CompressionSettings = TC_Default;
    Texture->CompressionNoAlpha = true;
    Texture->LODGroup = TEXTUREGROUP_World;
    Texture->MipGenSettings = TMGS_Sharpen4;
    Texture->MaxTextureSize = 8192;
    Texture->Filter = TF_Default;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->PostEditChange();
    if (bNewAsset)
        FAssetRegistryModule::AssetCreated(Texture);
    Texture->MarkPackageDirty();
    FSavePackageArgs Save;
    Save.TopLevelFlags = RF_Public | RF_Standalone;
    Save.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, Texture, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Save))
    {
        Error = TEXT("Failed to persist zone terrain atlas.");
        return nullptr;
    }
#else
    // Editor-imported assets retain mipmaps and are preferred in packaged games.
    UTexture2D *Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None, TConstArrayView64<uint8>(reinterpret_cast<const uint8 *>(Pixels.GetData()), int64(Pixels.Num()) * sizeof(FColor)));
    if (Texture)
    {
        Texture->SRGB = true;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->UpdateResource();
    }
#endif
    return Texture;
}

UMaterialInterface *DefaultMaterial(UTexture2D *DefaultTexture)
{
    constexpr const TCHAR *ObjectPath = TEXT("/Game/DAoC/Materials/M_DaocTerrain.M_DaocTerrain");
    if (auto *Existing = FindObject<UMaterialInterface>(nullptr, ObjectPath))
        return Existing;
    if (FPackageName::DoesPackageExist(TEXT("/Game/DAoC/Materials/M_DaocTerrain")))
    {
        if (auto *Existing = LoadObject<UMaterialInterface>(nullptr, ObjectPath))
            return Existing;
    }
#if WITH_EDITOR
    if (!DefaultTexture)
        return nullptr;
    UPackage *Package = CreatePackage(TEXT("/Game/DAoC/Materials/M_DaocTerrain"));
    UMaterial *Material = NewObject<UMaterial>(Package, TEXT("M_DaocTerrain"), RF_Public | RF_Standalone | RF_Transactional);
    Material->SetShadingModel(MSM_DefaultLit);
    auto *Diffuse = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Diffuse->ParameterName = TEXT("ZoneDiffuse");
    Diffuse->Texture = DefaultTexture;
    Diffuse->SamplerType = SAMPLERTYPE_Color;
    auto *Roughness = NewObject<UMaterialExpressionScalarParameter>(Material);
    Roughness->ParameterName = TEXT("ZoneRoughness");
    Roughness->DefaultValue = 1.f;
    auto *Specular = NewObject<UMaterialExpressionConstant>(Material);
    Specular->R = 0.0f;
    auto *Metallic = NewObject<UMaterialExpressionConstant>(Material);
    Metallic->R = 0.f;
    Material->GetExpressionCollection().AddExpression(Diffuse);
    Material->GetExpressionCollection().AddExpression(Roughness);
    Material->GetExpressionCollection().AddExpression(Specular);
    Material->GetExpressionCollection().AddExpression(Metallic);
    Material->GetEditorOnlyData()->BaseColor.Expression = Diffuse;
    Material->GetEditorOnlyData()->Roughness.Expression = Roughness;
    Material->GetEditorOnlyData()->Specular.Expression = Specular;
    Material->GetEditorOnlyData()->Metallic.Expression = Metallic;
    Material->PostEditChange();
    FAssetRegistryModule::AssetCreated(Material);
    Material->MarkPackageDirty();
    FSavePackageArgs Save;
    Save.TopLevelFlags = RF_Public | RF_Standalone;
    Save.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, Material, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Save))
        return nullptr;
    return Material;
#else
    return nullptr;
#endif
}
} // namespace DaocTerrainTextures
