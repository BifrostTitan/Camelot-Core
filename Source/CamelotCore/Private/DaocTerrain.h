#pragma once
#include <cmath>

namespace DaocTerrain
{
/// Exact slope of the same triangle used by Sample, in height per grid unit.
inline bool Gradient(const float *Heights, int Width, int Height, float X, float Y, float &GX, float &GY)
{
    GX = GY = 0;
    if (!Heights || Width < 2 || Height < 2 || !std::isfinite(X) || !std::isfinite(Y) || X < 0 || Y < 0 || X > Width - 1 || Y > Height - 1)
        return false;
    const int CX = X == Width - 1 ? Width - 2 : static_cast<int>(X);
    const int CY = Y == Height - 1 ? Height - 2 : static_cast<int>(Y);
    const float TL = Heights[CY * Width + CX], TR = Heights[CY * Width + CX + 1];
    const float BL = Heights[(CY + 1) * Width + CX], BR = Heights[(CY + 1) * Width + CX + 1];
    if (X - CX >= Y - CY)
    {
        GX = TR - TL;
        GY = BR - TR;
    }
    else
    {
        GX = BR - BL;
        GY = BL - TL;
    }
    return true;
}
/// UE uses clockwise front faces. Keep the same TL--BR diagonal as the sampler.
inline void CellTriangles(int TL, int TR, int BL, int BR, int *Indices)
{
    const int Ordered[] = {BR, TR, TL, BR, TL, BL};
    for (int I = 0; I < 6; ++I)
        Indices[I] = Ordered[I];
}
/// Combines terrain and offset channel samples using their source scale factors.
inline float Height(unsigned char TerrainRed, unsigned char OffsetRed, int Scale, int Offset)
{
    return TerrainRed / 255.0f * Scale + OffsetRed / 255.0f * Offset;
}

/// TL--BR diagonal, matching the procedural mesh. Samples are row-major.
inline float Sample(const float *Heights, int Width, int Height, float X, float Y)
{
    if (!Heights || Width < 2 || Height < 2 || !std::isfinite(X) || !std::isfinite(Y) || X < 0 || Y < 0 || X > Width - 1 || Y > Height - 1)
        return 0;
    const int CellX = X == Width - 1 ? Width - 2 : static_cast<int>(X);
    const int CellY = Y == Height - 1 ? Height - 2 : static_cast<int>(Y);
    const float DX = X - CellX, DY = Y - CellY;
    const auto At = [=](int I, int J) { return Heights[J * Width + I]; };
    const float TL = At(CellX, CellY), BR = At(CellX + 1, CellY + 1);
    return DX >= DY ? TL * (1 - DX) + At(CellX + 1, CellY) * (DX - DY) + BR * DY : TL * (1 - DY) + At(CellX, CellY + 1) * (DY - DX) + BR * DX;
}
} // namespace DaocTerrain
