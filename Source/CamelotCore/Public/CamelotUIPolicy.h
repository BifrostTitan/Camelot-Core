#pragma once
#include "CoreMinimal.h"
#include "DaocCombatTypes.h"

namespace CamelotUI
{
/// Semantic nameplate color selected from realm relationships and mob difficulty.
enum class ENameColor : uint8
{
    Unknown,
    Friendly,
    Enemy,
    Gray,
    Green,
    Blue,
    Yellow,
    Orange,
    Red,
    Purple,
    DarkPurple
};

/// Selects realm relationship or classic mob difficulty color without predicting XP rewards.
inline ENameColor NameColor(bool NPC, int32 Realm, int32 LocalRealm, int32 Level, int32 LocalLevel)
{
    if (Realm > 0 && LocalRealm > 0)
    {
        if (Realm == LocalRealm)
            return ENameColor::Friendly;
        if (!NPC)
            return ENameColor::Enemy;
    }
    if (!NPC || Level <= 0 || LocalLevel <= 0)
        return ENameColor::Unknown;
    // DOL GameLiving.NoXPForLevel / IsObjectGreyCon: the explicit XP cutoff
    // differs from the approximate GetConLevel formula at some levels.
    static constexpr int32 NoXP[] = {-3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 25, 25, 25, 25, 25, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};
    const int32 GrayLevel = LocalLevel < UE_ARRAY_COUNT(NoXP) ? NoXP[LocalLevel] : LocalLevel - 3 * FMath::Max(1, (LocalLevel + 9) / 10);
    if (Level <= GrayLevel)
        return ENameColor::Gray;
    // Classic color ranges documented by DOL AbstractServerRules: level / 10 + 1.
    // A yellow range ends at the player's level; higher mobs start at orange.
    const int32 Width = LocalLevel / 10 + 1;
    const int32 Band = FMath::CeilToInt(double(Level - LocalLevel) / Width);
    if (Band <= -2)
        return ENameColor::Green;
    if (Band == -1)
        return ENameColor::Blue;
    if (Band == 0)
        return ENameColor::Yellow;
    if (Band == 1)
        return ENameColor::Orange;
    if (Band == 2)
        return ENameColor::Red;
    return Band == 3 ? ENameColor::Purple : ENameColor::DarkPurple;
}

/// Classic chat is a printable single-byte string in the existing 0xaf packet.
inline bool NormalizeChat(const FString &Input, FString &Wire)
{
    Wire = Input.TrimStartAndEnd();
    if (Wire.StartsWith(TEXT("/")))
        Wire[0] = TEXT('&');
    if (Wire.IsEmpty() || Wire.Len() > 254 || Wire == TEXT("&"))
        return false;
    for (TCHAR C : Wire)
        if (C < 32 || C > 126)
            return false;
    return true;
}

/// Selects a bounded distance-filtered label set with selected-target priority.
inline TArray<int32> SelectNameplates(const TArray<FDaocCombatTarget> &Targets, FVector Origin, float Distance, int32 Limit, int32 SelectedId)
{
    TArray<int32> Result;
    if (Distance <= 0 || Limit <= 0)
        return Result;
    for (int32 I = 0; I < Targets.Num(); ++I)
        if (!Targets[I].bLocal && !Targets[I].Name.IsEmpty() && FVector::DistSquared(Origin, Targets[I].Position) <= FMath::Square(Distance))
            Result.Add(I);
    Result.Sort([&](int32 A, int32 B) {
        const bool SelectedA = Targets[A].ObjectId == SelectedId, SelectedB = Targets[B].ObjectId == SelectedId;
        if (SelectedA != SelectedB)
            return SelectedA;
        const double DA = FVector::DistSquared(Origin, Targets[A].Position), DB = FVector::DistSquared(Origin, Targets[B].Position);
        return DA == DB ? Targets[A].ObjectId < Targets[B].ObjectId : DA < DB;
    });
    if (Result.Num() > Limit)
        Result.SetNum(Limit);
    return Result;
}
} // namespace CamelotUI
