#include "DaocCharacterSizing.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

int main()
{
    using namespace DaocCharacterSizing;
    // Fingerprint of the original switch across the entire 16-bit model space.
    // This catches shifted range boundaries and missing or changed model values.
    std::uint64_t Hash = 14695981039346656037ull;
    for (int Model = 0; Model <= 65535; ++Model)
    {
        const float Scale = ModelScale(Model);
        std::uint32_t Bits;
        static_assert(sizeof(Bits) == sizeof(Scale));
        std::memcpy(&Bits, &Scale, sizeof(Bits));
        for (int Byte = 0; Byte < 4; ++Byte)
        {
            Hash = (Hash ^ (Bits & 255u)) * 1099511628211ull;
            Bits >>= 8;
        }
    }
    if (Hash != 5900177926995926893ull || ModelScale(-1) != 1.f || ModelScale(std::numeric_limits<int>::min()) != 1.f || ModelScale(std::numeric_limits<int>::max()) != 1.f)
        return 1;

    for (int Flags = 0; Flags <= 65535; ++Flags)
    {
        const int Choice = (Flags >> 11) & 3;
        const float Expected = Choice == 1 ? .95f : Choice == 3 ? 1.05f
                                                                : 1.f;
        if (HeightScale(Flags) != Expected || AppearanceScale(318, Flags) != .63f * Expected)
            return 2;
    }
    if (PacketSizeScale(50, true) != 1.f || PacketSizeScale(25, true) != .5f || PacketSizeScale(100, true) != 2.f || PacketSizeScale(100, false) != 1.f || PacketSizeScale(50, false) != .5f || PacketSizeScale(0, true) != 1.f || PacketSizeScale(-1, false) != 1.f)
        return 3;

    std::cout << "Model scale fingerprint, height flags and packet-size baselines passed.\n";
}
