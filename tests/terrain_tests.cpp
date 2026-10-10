#include "DaocPcx.h"
#include "DaocTerrain.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iterator>
#include <limits>

void Check(bool Value)
{
    if (!Value)
    {
        std::cerr << "Terrain regression failed\n";
        std::exit(1);
    }
}
void Near(float A, float B)
{
    Check(std::abs(A - B) < 0.0001f);
}

std::vector<std::uint8_t> Pcx(int Planes, bool Rle)
{
    std::vector<std::uint8_t> Bytes(128, 0);
    Bytes[0] = 10;
    Bytes[2] = Rle;
    Bytes[3] = 8;
    // Nonzero origin, 3x2 visible pixels, 4 bytes per plane (padding).
    Bytes[4] = 5;
    Bytes[8] = 7;
    Bytes[6] = 7;
    Bytes[10] = 8;
    Bytes[65] = Planes;
    Bytes[66] = 4;
    for (int Y = 0; Y < 2; ++Y)
        for (int P = 0; P < Planes; ++P)
            for (int X = 0; X < 4; ++X)
            {
                const auto V = static_cast<std::uint8_t>(X == 3 ? 240 : Y * 10 + P * 30 + X);
                if (Rle)
                    Bytes.push_back(0xc1);
                Bytes.push_back(V);
            }
    if (Planes == 1)
    {
        Bytes.push_back(12);
        for (int I = 0; I < 256; ++I)
        {
            Bytes.push_back(255 - I);
            Bytes.push_back(I);
            Bytes.push_back(0);
        }
    }
    return Bytes;
}

int main(int Argc, char **Argv)
{
    for (int Planes : {1, 3})
        for (bool Rle : {false, true})
        {
            auto Bytes = Pcx(Planes, Rle);
            DaocPcx::Image Image;
            Check(DaocPcx::Decode(Bytes.data(), Bytes.size(), Image));
            Check(Image.Width == 3 && Image.Height == 2 && Image.Red.size() == 6);
            for (int Y = 0; Y < 2; ++Y)
                for (int X = 0; X < 3; ++X)
                    Check(Image.Red[Y * 3 + X] == (Planes == 1 ? 255 - Y * 10 - X : Y * 10 + X));
            // Truncation must fail rather than producing a partial map.
            Check(!DaocPcx::Decode(Bytes.data(), 130, Image));
        }
    auto Bytes = Pcx(1, true);
    DaocPcx::Image Image;
    Bytes[128] = 0xc0;
    Check(!DaocPcx::Decode(Bytes.data(), Bytes.size(), Image));
    Bytes = Pcx(1, true);
    Bytes[128] = 0xff;
    Check(!DaocPcx::Decode(Bytes.data(), Bytes.size(), Image));
    Bytes = Pcx(1, true);
    Bytes[Bytes.size() - 769] = 0;
    Check(!DaocPcx::Decode(Bytes.data(), Bytes.size(), Image));
    Bytes = Pcx(3, false);
    Bytes[66] = 2;
    Check(!DaocPcx::Decode(Bytes.data(), Bytes.size(), Image));
    Check(!DaocPcx::Decode(nullptr, 0, Image));
    Near(DaocTerrain::Height(255, 128, 32, 255), 160);
    Near(DaocTerrain::Height(0, 255, 32, -10), -10);
    // Noncoplanar quad detects selecting the wrong triangle (or bilinear interpolation).
    const float Heights[] = {0, 10, 20, 100};
    float GX, GY;
    Check(DaocTerrain::Gradient(Heights, 2, 2, .75f, .25f, GX, GY));
    Near(GX, 10);
    Near(GY, 90);
    Check(DaocTerrain::Gradient(Heights, 2, 2, .25f, .75f, GX, GY));
    Near(GX, 80);
    Near(GY, 20);
    Check(DaocTerrain::Gradient(Heights, 2, 2, 1, 1, GX, GY));
    Near(GX, 10);
    Near(GY, 90);
    Check(!DaocTerrain::Gradient(Heights, 2, 2, -1, 0, GX, GY));
    Near(DaocTerrain::Sample(Heights, 2, 2, .75f, .25f), 30);
    Near(DaocTerrain::Sample(Heights, 2, 2, .25f, .75f), 35);
    Near(DaocTerrain::Sample(Heights, 2, 2, .5f, .5f), 50);
    Near(DaocTerrain::Sample(Heights, 2, 2, 1, 1), 100);
    Near(DaocTerrain::Sample(Heights, 2, 2, 1, .5f), 55);
    Near(DaocTerrain::Sample(Heights, 2, 2, -1, 0), 0);
    Near(DaocTerrain::Sample(Heights, 2, 2, std::numeric_limits<float>::quiet_NaN(), 0), 0);
    for (int I = 1; I < Argc; ++I)
    {
        std::ifstream File(Argv[I], std::ios::binary);
        Check(File.is_open());
        const std::vector<std::uint8_t> Data((std::istreambuf_iterator<char>(File)), {});
        Check(DaocPcx::Decode(Data.data(), Data.size(), Image));
        std::cout << "Decoded " << Argv[I] << ": " << Image.Width << "x" << Image.Height << '\n';
    }
    std::cout << "PCX and terrain regressions passed\n";
}
