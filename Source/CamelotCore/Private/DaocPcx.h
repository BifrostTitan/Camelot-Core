#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#if defined(DAOC_PCX_UNREAL)
#include "Containers/Array.h"
#else
#include <vector>
#endif

// PCX stores padded scanlines and planar RGB; indexed terrain uses palette red,
// not the index itself. Matches PCXDecoder.PcxImage.GetPixel(x, y).R.
namespace DaocPcx
{
/// Use Unreal's allocator in the plugin. STL storage is only for standalone
/// tests, avoiding MSVC STL runtime dependencies in Live Coding patches.
class ByteBuffer
{
  public:
#if defined(DAOC_PCX_UNREAL)
    /// Returns the number of decoded bytes currently stored.
    std::size_t size() const
    {
        return static_cast<std::size_t>(Bytes.Num());
    }
    /// Reserves byte storage without changing its logical length.
    void reserve(std::size_t Count)
    {
        Bytes.Reserve(static_cast<int32>(Count));
    }
    /// Changes byte-buffer length before decoded bytes are written.
    void resize(std::size_t Count)
    {
        Bytes.SetNumUninitialized(static_cast<int32>(Count));
    }
    /// Returns mutable byte storage; the caller must supply an index below size().
    std::uint8_t &operator[](std::size_t Index)
    {
        return Bytes[static_cast<int32>(Index)];
    }
    /// Returns one decoded byte; the caller must supply an index below size().
    const std::uint8_t &operator[](std::size_t Index) const
    {
        return Bytes[static_cast<int32>(Index)];
    }
    /// Appends Count repeated bytes while decoding a PCX run.
    void AppendRun(unsigned Count, std::uint8_t Value)
    {
        const int32 First = Bytes.AddUninitialized(static_cast<int32>(Count));
        for (unsigned I = 0; I < Count; ++I)
            Bytes[First + static_cast<int32>(I)] = Value;
    }

  private:
    TArray<uint8> Bytes;
#else
    /// Returns the number of decoded bytes currently stored.
    std::size_t size() const
    {
        return Bytes.size();
    }
    /// Reserves byte storage without changing its logical length.
    void reserve(std::size_t Count)
    {
        Bytes.reserve(Count);
    }
    /// Changes byte-buffer length before decoded bytes are written.
    void resize(std::size_t Count)
    {
        Bytes.resize(Count);
    }
    /// Returns mutable byte storage; the caller must supply an index below size().
    std::uint8_t &operator[](std::size_t Index)
    {
        return Bytes[Index];
    }
    /// Returns one decoded byte; the caller must supply an index below size().
    const std::uint8_t &operator[](std::size_t Index) const
    {
        return Bytes[Index];
    }
    /// Appends Count repeated bytes while decoding a PCX run.
    void AppendRun(unsigned Count, std::uint8_t Value)
    {
        Bytes.insert(Bytes.end(), Count, Value);
    }

  private:
    std::vector<std::uint8_t> Bytes;
#endif
};

/// Decoded PCX dimensions and red-channel samples used by terrain inputs.
struct Image
{
    int Width = 0;
    int Height = 0;
    ByteBuffer Red;
};

/// Validates supported PCX data and decodes its red channel into the output image.
inline bool Decode(const std::uint8_t *Data, std::size_t Size, Image &Out)
{
    if (!Data || Size < 128 || Data[0] != 10 || Data[2] > 1 || Data[3] != 8)
        return false;
    const auto U16 = [Data](int I) { return unsigned(Data[I]) | (unsigned(Data[I + 1]) << 8); };
    if (U16(8) < U16(4) || U16(10) < U16(6))
        return false;
    const unsigned Width = U16(8) - U16(4) + 1;
    const unsigned Height = U16(10) - U16(6) + 1;
    const unsigned Stride = U16(66);
    const unsigned Planes = Data[65];
    if (Stride < Width || (Planes != 1 && Planes != 3))
        return false;
    // Bound allocations for malformed files (well above DAoC's 256x256 maps).
    const std::uint64_t Count = std::uint64_t(Stride) * Height * Planes;
    if (Count > 64 * 1024 * 1024)
        return false;
    std::size_t End = Size;
    const std::uint8_t *Palette = nullptr;
    if (Planes == 1)
    {
        if (Size < 128 + 769 || Data[Size - 769] != 12)
            return false;
        End = Size - 769;
        Palette = Data + End + 1;
    }
    ByteBuffer Scan;
    Scan.reserve(static_cast<std::size_t>(Count));
    std::size_t Position = 128;
    while (Scan.size() < Count)
    {
        if (Position >= End)
            return false;
        auto Value = Data[Position++];
        unsigned Run = 1;
        if (Data[2] == 1 && (Value & 0xc0) == 0xc0)
        {
            Run = Value & 63;
            if (Run == 0 || Position >= End)
                return false;
            Value = Data[Position++];
        }
        if (Run > Count - Scan.size())
            return false;
        Scan.AppendRun(Run, Value);
    }
    Image Result;
    Result.Width = static_cast<int>(Width);
    Result.Height = static_cast<int>(Height);
    Result.Red.resize(std::size_t(Width) * Height);
    for (unsigned Y = 0; Y < Height; ++Y)
        for (unsigned X = 0; X < Width; ++X)
        {
            const auto Value = Scan[std::size_t(Y) * Stride * Planes + X];
            Result.Red[std::size_t(Y) * Width + X] = Palette ? Palette[Value * 3] : Value;
        }
    Out = std::move(Result);
    return true;
}
} // namespace DaocPcx
