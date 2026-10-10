#pragma once
#include <cstddef>

// Modular meshes import at 2.54 cm/unit; creature manifests bake their scale.
namespace DaocCharacterSizing
{
namespace Detail
{
/// Inclusive model-ID interval sharing one authored scale multiplier.
struct ModelScaleRange
{
    int First;
    int Last;
    float Scale;
};
inline constexpr ModelScaleRange ModelScales[] = {
#include "DaocCharacterModelScales.inl"
};
inline constexpr std::size_t ModelScaleCount = sizeof(ModelScales) / sizeof(ModelScales[0]);

/// Checks inclusive range ordering, non-overlap and positive scales at compile time.
constexpr bool ValidModelScales()
{
    for (std::size_t I = 0; I < ModelScaleCount; ++I)
        if (ModelScales[I].First < 0 || ModelScales[I].Last < ModelScales[I].First || !(ModelScales[I].Scale > 0.f) || (I > 0 && ModelScales[I - 1].Last >= ModelScales[I].First))
            return false;
    return true;
}
static_assert(ValidModelScales(), "Model scale ranges must be positive, sorted and non-overlapping");
} // namespace Detail

/// Looks up authored modular model scale; unknown or negative model IDs return 1.
inline constexpr float ModelScale(int Model)
{
    std::size_t First = 0, End = Detail::ModelScaleCount;
    while (First < End)
    {
        const std::size_t Middle = First + (End - First) / 2;
        const auto &Range = Detail::ModelScales[Middle];
        if (Model < Range.First)
            End = Middle;
        else if (Model > Range.Last)
            First = Middle + 1;
        else
            return Range.Scale;
    }
    return 1.f;
}

/// Model bits 11..12: 1 small, 2 medium, 3 large; unflagged uses medium.
/// These presentation offsets are not verified original-client height ratios.
inline constexpr float HeightScale(int Flags)
{
    const unsigned Choice = (static_cast<unsigned>(Flags) >> 11) & 3u;
    return Choice == 1 ? .95f : Choice == 3 ? 1.05f
                                            : 1.f;
}

/// NPC creation uses 50 for normal size; player appearance uses 100.
/// A missing size retains the model's authored scale.
inline constexpr float PacketSizeScale(int Size, bool IsNpc)
{
    return Size > 0 ? Size / (IsNpc ? 50.f : 100.f) : 1.f;
}

/// Combines authored model scale with the reported height selection.
inline constexpr float AppearanceScale(int Model, int Flags)
{
    return ModelScale(Model) * HeightScale(Flags);
}
} // namespace DaocCharacterSizing
