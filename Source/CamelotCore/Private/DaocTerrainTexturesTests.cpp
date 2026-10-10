#include "DaocTerrainTextures.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace DaocTerrainTextures
{
bool RunAtlasTests(FString &Error)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DaocTerrainAtlasTests"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    if (!FFileHelper::SaveStringToFile(TEXT("[sectorsize]\nsizex=2\nsizey=2\n"), *FPaths::Combine(Directory, TEXT("sector.dat"))))
        return false;
    auto &Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    for (int32 Y = 0; Y < 2; ++Y)
        for (int32 X = 0; X < 2; ++X)
        {
            FColor Colors[4];
            for (int32 I = 0; I < 4; ++I)
                Colors[I] = FColor(X * 10 + Y * 20 + I, 77, 88, 0);
            auto Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
            if (!Wrapper->SetRaw(Colors, sizeof(Colors), 2, 2, ERGBFormat::BGRA, 8))
                return false;
            const auto &Bytes = Wrapper->GetCompressed();
            if (!FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(Directory, FString::Printf(TEXT("tex%02d-%02d.png"), X, Y))))
                return false;
        }
    int32 Width = 0, Height = 0;
    TArray<FColor> Pixels;
    if (!LoadAtlasPixels(Directory, Width, Height, Pixels, Error))
        return false;
    const uint8 Expected[] = {0, 1, 10, 11, 2, 3, 12, 13, 20, 21, 30, 31, 22, 23, 32, 33};
    if (Width != 4 || Height != 4 || Pixels.Num() != 16)
    {
        Error = TEXT("Incorrect atlas dimensions.");
        return false;
    }
    for (int32 I = 0; I < 16; ++I)
    {
        if (Pixels[I] != FColor(Expected[I], 77, 88, 255))
        {
            Error = TEXT("Atlas orientation, channel order, or opaque alpha differs.");
            return false;
        }
    }
    // Failed imports must preserve the caller's complete previous atlas.
    const FString Empty = FPaths::Combine(Directory, TEXT("MissingTile"));
    IFileManager::Get().MakeDirectory(*Empty, true);
    FFileHelper::SaveStringToFile(TEXT("[sectorsize]\nsizex=2\nsizey=2\n"), *FPaths::Combine(Empty, TEXT("sector.dat")));
    if (LoadAtlasPixels(Empty, Width, Height, Pixels, Error) || Width != 4 || Height != 4 || Pixels.Num() != 16)
    {
        Error = TEXT("Failed atlas import replaced existing data.");
        return false;
    }
    // A zero sector grid is the layered-terrain format. Its complete converted
    // LOD atlas is usable; a partial grid must still fail.
    FFileHelper::SaveStringToFile(TEXT("[sectorsize]\nsizex=0\nsizey=0\n"), *FPaths::Combine(Directory, TEXT("sector.dat")));
    if (LoadAtlasPixels(Directory, Width, Height, Pixels, Error))
    {
        Error = TEXT("Partial zero-grid fallback accepted.");
        return false;
    }
    for (int32 Y = 0; Y < 8; ++Y)
        for (int32 X = 0; X < 8; ++X)
        {
            FColor Color(77, 88, 99, 255);
            auto Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
            if (!Wrapper->SetRaw(&Color, sizeof(Color), 1, 1, ERGBFormat::BGRA, 8))
                return false;
            if (!FFileHelper::SaveArrayToFile(Wrapper->GetCompressed(), *FPaths::Combine(Directory, FString::Printf(TEXT("tex%02d-%02d.png"), X, Y))))
                return false;
        }
    if (!LoadAtlasPixels(Directory, Width, Height, Pixels, Error) || Width != 8 || Height != 8 || Pixels.Num() != 64)
        return false;
    Error.Reset();
    return true;
}
} // namespace DaocTerrainTextures

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDaocTerrainAtlasTest, "DAoC.Terrain.TextureAtlas", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDaocTerrainAtlasTest::RunTest(const FString &Parameters)
{
    FString Error;
    const bool bPassed = DaocTerrainTextures::RunAtlasTests(Error);
    if (!bPassed)
        AddError(Error);
    return bPassed;
}
#endif
