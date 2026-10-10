#include "DaocZoneActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "String/LexFromString.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "UObject/SavePackage.h"

#endif

namespace
{
constexpr float WaterHeightFactor = 255.f / 65536.f;
constexpr float WaterBankFactor = 255.f / 256.f;
bool BankPoint(const FString *Text, FVector2D &Out)
{
    if (!Text)
        return false;
    TArray<FString> Parts;
    Text->ParseIntoArray(Parts, TEXT(","), false);
    double X = 0, Y = 0;
    if (Parts.Num() < 2 || !LexTryParseString(X, *Parts[0].TrimStartAndEnd()) || !LexTryParseString(Y, *Parts[1].TrimStartAndEnd()) || !FMath::IsFinite(X) || !FMath::IsFinite(Y))
        return false;
    Out = FVector2D(X, Y);
    return true;
}
UMaterialInterface *DefaultWaterMaterial()
{
    const TCHAR *PackageName = TEXT("/Game/DAoC/Materials/M_DaocZoneWater");
    const TCHAR *ObjectName = TEXT("/Game/DAoC/Materials/M_DaocZoneWater.M_DaocZoneWater");
    if (auto *Existing = FindObject<UMaterialInterface>(nullptr, ObjectName))
        return Existing;
    if (FPackageName::DoesPackageExist(PackageName))
        return LoadObject<UMaterialInterface>(nullptr, ObjectName);
#if WITH_EDITOR
    UPackage *Package = CreatePackage(PackageName);
    auto *Material = NewObject<UMaterial>(Package, TEXT("M_DaocZoneWater"), RF_Public | RF_Standalone | RF_Transactional);
    Material->SetShadingModel(MSM_DefaultLit);
    Material->TwoSided = true;
    auto *Color = NewObject<UMaterialExpressionVectorParameter>(Material);
    Color->ParameterName = TEXT("WaterColor");
    Color->DefaultValue = FLinearColor(.025f, .12f, .15f, 1);
    Material->GetExpressionCollection().AddExpression(Color);
    Material->GetEditorOnlyData()->BaseColor.Expression = Color;
    auto Scalar = [&](const TCHAR *Name, float Value) {
        auto *E = NewObject<UMaterialExpressionScalarParameter>(Material);
        E->ParameterName = Name;
        E->DefaultValue = Value;
        Material->GetExpressionCollection().AddExpression(E);
        return E;
    };
    Material->GetEditorOnlyData()->Roughness.Expression = Scalar(TEXT("WaterRoughness"), .15f);
    Material->GetEditorOnlyData()->Metallic.Expression = Scalar(TEXT("WaterMetallic"), 0);
    Material->GetEditorOnlyData()->Specular.Expression = Scalar(TEXT("WaterSpecular"), .5f);
    Material->PostEditChange();
    FAssetRegistryModule::AssetCreated(Material);
    Material->MarkPackageDirty();
    FSavePackageArgs Save;
    Save.TopLevelFlags = RF_Public | RF_Standalone;
    Save.SaveFlags = SAVE_NoError;
    UPackage::SavePackage(Package, Material, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), Save);
    return Material;
#else
    return nullptr;
#endif
}
bool InsideTriangle(FVector2D P, FVector2D A, FVector2D B, FVector2D C)
{
    auto Cross = [](FVector2D U, FVector2D V) { return U.X * V.Y - U.Y * V.X; };
    if (FMath::Abs(Cross(B - A, C - A)) < SMALL_NUMBER)
        return false;
    const double X = Cross(B - A, P - A), Y = Cross(C - B, P - B), Z = Cross(A - C, P - C);
    return (X >= 0 && Y >= 0 && Z >= 0) || (X <= 0 && Y <= 0 && Z <= 0);
}
} // namespace

bool ADaocZoneActor::ImportWaterData(const FString &Directory)
{
    WaterSurfaces.Reset();
    bWaterDataPrepared = false;
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Directory, TEXT("sector.dat"))))
        return false;
    TMap<FString, TMap<FString, FString>> Sections;
    FString Section;
    for (FString Line : Lines)
    {
        Line.TrimStartAndEndInline();
        if (Line.IsEmpty() || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#")))
            continue;
        if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
        {
            Section = Line.Mid(1, Line.Len() - 2).ToLower();
            continue;
        }
        FString Key, Value;
        if (Line.Split(TEXT("="), &Key, &Value))
            Sections.FindOrAdd(Section).Add(Key.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
    }
    const auto *Defs = Sections.Find(TEXT("waterdefs"));
    int32 Count = 0;
    if (Defs)
        LexTryParseString(Count, *Defs->FindRef(TEXT("num")));
    bool Complete = Count >= 0 && Count <= 256;
    for (int32 Id = 0; Id < FMath::Clamp(Count, 0, 256); ++Id)
    {
        const auto *Fields = Sections.Find(FString::Printf(TEXT("river%02d"), Id));
        if (!Fields)
        {
            Complete = false;
            continue;
        }
        FDaocZoneWaterSurface S;
        S.Id = Id;
        int32 Banks = 0;
        if (!LexTryParseString(S.Height, *Fields->FindRef(TEXT("height"))) || !FMath::IsFinite(S.Height) || S.Height < 0 || !LexTryParseString(Banks, *Fields->FindRef(TEXT("bankpoints"))) || Banks < 2 || Banks > 4096)
        {
            Complete = false;
            continue;
        }
        S.Name = Fields->FindRef(TEXT("name"));
        S.Type = Fields->FindRef(TEXT("type"));
        S.Texture = Fields->FindRef(TEXT("texture"));
        S.MultiTexture = Fields->FindRef(TEXT("multitexture"));
        LexTryParseString(S.Flow, *Fields->FindRef(TEXT("flow")));
        LexTryParseString(S.SourceColor, *Fields->FindRef(TEXT("color")));
        bool Valid = true;
        for (int32 I = 0; I < Banks; ++I)
        {
            FVector2D L, R;
            if (!BankPoint(Fields->Find(FString::Printf(TEXT("left%02d"), I)), L) || !BankPoint(Fields->Find(FString::Printf(TEXT("right%02d"), I)), R))
            {
                Valid = false;
                break;
            }
            S.LeftBank.Add(L);
            S.RightBank.Add(R);
        }
        if (Valid)
            WaterSurfaces.Add(MoveTemp(S));
        else
            Complete = false;
    }
    bWaterDataPrepared = Complete;
    if (!Complete)
        UE_LOG(LogTemp, Warning, TEXT("Zone %d: incomplete outdoor water definitions in %s"), ZoneId, *Directory);
    return Complete;
}

bool ADaocZoneActor::RefreshWaterData()
{
    if (!bHasTerrain)
        return false;
    const bool Result = ImportWaterData(ZoneExportDirectory);
    RebuildWater();
    RebuildGroundCover();
    return Result;
}

void ADaocZoneActor::RebuildWater()
{
    WaterMesh->ClearAllMeshSections();
    WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (!bHasTerrain || !bGenerateWater)
        return;
    // Older prepared profiles lazily read sector.dat. Saving the profile embeds
    // the data for cooked clients.
    if (!bWaterDataPrepared && !ZoneExportDirectory.IsEmpty())
        ImportWaterData(ZoneExportDirectory);
    if (WaterSurfaces.IsEmpty())
        return;
    UMaterialInterface *Parent = WaterMaterial ? WaterMaterial.Get() : DefaultWaterMaterial();
    if (!Parent)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("Zone %d: water material missing; prepare and save a zone "
                    "profile in the editor."),
               ZoneId);
        return;
    }
    WaterMaterial = Parent;
    int32 Section = 0;
    for (const auto &S : WaterSurfaces)
    {
        if (!FMath::IsFinite(S.Height) || S.LeftBank.Num() < 2 || S.LeftBank.Num() > 4096 || S.LeftBank.Num() != S.RightBank.Num())
            continue;
        TArray<FVector> V, N;
        TArray<int32> T;
        TArray<FVector2D> UV;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        const float XY = WaterBankFactor * WorldUnitsPerDaocUnit, Z = S.Height * WaterHeightFactor * WorldUnitsPerDaocUnit;
        bool Valid = true;
        for (int32 I = 0; I < S.LeftBank.Num(); ++I)
            for (FVector2D P : {S.LeftBank[I], S.RightBank[I]})
            {
                if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y))
                {
                    Valid = false;
                    break;
                }
                V.Emplace(P.X * XY, P.Y * XY, Z);
                N.Add(FVector::UpVector);
                UV.Add(P / 8.0);
                Colors.Add(FLinearColor::White);
            }
        if (!Valid)
            continue;
        auto Triangle = [&](int32 A, int32 B, int32 C) {
            const double Cross = FVector::CrossProduct(V[B] - V[A], V[C] - V[A]).Z;
            if (FMath::Abs(Cross) < SMALL_NUMBER)
                return;
            if (Cross > 0)
                Swap(B, C);
            T.Append({A, B, C});
        };
        for (int32 I = 0; I < S.LeftBank.Num() - 1; ++I)
        {
            Triangle(2 * I, 2 * I + 1, 2 * I + 2);
            Triangle(2 * I + 1, 2 * I + 3, 2 * I + 2);
        }
        if (T.IsEmpty())
            continue;
        WaterMesh->CreateMeshSection_LinearColor(Section, V, T, N, UV, Colors, Tangents, false);
        auto *MID = UMaterialInstanceDynamic::Create(Parent, WaterMesh);
        MID->SetScalarParameterValue(TEXT("WaterFlow"), FMath::IsFinite(S.Flow) ? S.Flow : 0);
        WaterMesh->SetMaterial(Section++, MID);
    }
    UE_LOG(LogTemp, Display, TEXT("Zone %d: generated %d outdoor water surfaces"), ZoneId, Section);
}

bool ADaocZoneActor::IsUnderwater(FVector WorldPosition) const
{
    const FVector Local = GetActorTransform().InverseTransformPosition(WorldPosition);
    return IsBelowWater(Local.X, Local.Y, Local.Z);
}
bool ADaocZoneActor::IsBelowWater(float LocalX, float LocalY, float LocalZ) const
{
    if (!bGenerateWater || !bHasTerrain || WorldUnitsPerDaocUnit <= 0)
        return false;
    const FVector2D P(LocalX / (WaterBankFactor * WorldUnitsPerDaocUnit), LocalY / (WaterBankFactor * WorldUnitsPerDaocUnit));
    for (const auto &S : WaterSurfaces)
    {
        if (LocalZ >= S.Height * WaterHeightFactor * WorldUnitsPerDaocUnit || S.LeftBank.Num() != S.RightBank.Num())
            continue;
        for (int32 I = 0; I < S.LeftBank.Num() - 1; ++I)
            if (InsideTriangle(P, S.LeftBank[I], S.RightBank[I], S.LeftBank[I + 1]) || InsideTriangle(P, S.RightBank[I], S.RightBank[I + 1], S.LeftBank[I + 1]))
                return true;
    }
    return false;
}
