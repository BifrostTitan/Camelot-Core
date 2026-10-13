#include <niflib/ni_file.h>
#include <niflib/ni_particles.h>
#include <niflib/ni_textures.h>
#include <niflib/ni_properties.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

using namespace niflib;
namespace fs = std::filesystem;
// Encode names and parser diagnostics as JSON strings, including control bytes.
static std::string Q(const std::string &s)
{
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s)
        if (c == '"' || c == '\\')
            o << '\\' << char(c);
        else if (c < 32)
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else
            o << char(c);
    o << '"';
    return o.str();
}
static float F(float f)
{
    return std::isfinite(f) ? f : 0;
}
static std::string V(Vec3 v)
{
    return "[" + std::to_string(F(v.x)) + "," + std::to_string(F(v.y)) + "," + std::to_string(F(v.z)) + "]";
}
static std::string C(Color4 v)
{
    return "[" + std::to_string(F(v.r)) + "," + std::to_string(F(v.g)) + "," + std::to_string(F(v.b)) + "," + std::to_string(F(v.a)) + "]";
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    std::ofstream out(argv[2]);
    out << "[";
    bool comma = false;
    int files = 0, emitters = 0, errors = 0;
    for (const auto &entry : fs::directory_iterator(argv[1]))
    {
        auto extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        if (extension != ".nif")
            continue;
        if (comma)
            out << ",";
        comma = true;
        out << "{\"file\":" << Q(entry.path().filename().string());
        try
        {
            std::ifstream stream(entry.path(), std::ios::binary);
            NiFile file(stream);
            out << ",\"emitters\":[";
            bool first = true;
            for (const auto &[id, object] : file.objects())
            {
                auto controller = std::dynamic_pointer_cast<NiParticleSystemController>(object);
                if (!controller)
                    continue;
                if (!first)
                    out << ",";
                first = false;
                emitters++;
                auto target = std::dynamic_pointer_cast<NiParticles>(controller->target.object());
                std::string texture;
                int alpha = 0;
                Color4 color{1, 1, 1, 1};
                if (target)
                    for (const auto &property : target->properties)
                    {
                        if (auto p = std::dynamic_pointer_cast<NiTexturingProperty>(property.object()))
                            if (p->base_texture && p->base_texture->source.object())
                                texture = p->base_texture->source.object()->file_name;
                        if (auto p = std::dynamic_pointer_cast<NiAlphaProperty>(property.object()))
                            alpha = p->flags;
                        if (auto p = std::dynamic_pointer_cast<NiMaterialProperty>(property.object()))
                            color = {p->diffuse_color.x, p->diffuse_color.y, p->diffuse_color.z, p->alpha};
                    }
                float radius = controller->size;
                if (target)
                    if (auto data = std::dynamic_pointer_cast<NiParticlesData>(target->data.object()))
                        radius = data->particle_radius;
                auto emitter = std::dynamic_pointer_cast<NiAVObject>(controller->emitter.object());
                out << "{\"texture\":" << Q(texture) << ",\"alphaFlags\":" << alpha << ",\"color\":" << C(color)
                    << ",\"initialColor\":" << C(controller->unknown_color) << ",\"emitFlags\":" << controller->emit_flags
                    << ",\"radius\":" << F(radius) << ",\"size\":" << F(controller->size)
                    << ",\"rate\":" << F(controller->emit_rate) << ",\"capacity\":" << controller->num_particles
                    << ",\"life\":" << F(controller->lifetime) << ",\"lifeRandom\":" << F(controller->lifetime_random)
                    << ",\"speed\":" << F(controller->speed) << ",\"speedRandom\":" << F(controller->random_speed)
                    << ",\"verticalDirection\":" << F(controller->vertical_direction) << ",\"verticalAngle\":" << F(controller->vertical_angle)
                    << ",\"horizontalDirection\":" << F(controller->horizontal_direction) << ",\"horizontalAngle\":" << F(controller->horizontal_angle)
                    << ",\"start\":" << F(controller->emit_start_time) << ",\"stop\":" << F(controller->emit_stop_time)
                    << ",\"box\":" << V(controller->start_random) << ",\"emitterOffset\":" << V(emitter ? emitter->translation : Vec3{})
                    << ",\"modifiers\":[";
                auto modifier = controller->particle_extra.object();
                std::set<const NiParticleModifier *> seen;
                bool firstModifier = true;
                while (modifier && seen.insert(modifier.get()).second && seen.size() <= 64)
                {
                    if (!firstModifier)
                        out << ",";
                    firstModifier = false;
                    if (auto m = std::dynamic_pointer_cast<NiGravity>(modifier))
                        out << "{\"type\":\"gravity\",\"force\":" << F(m->force) << ",\"mode\":" << m->type << ",\"direction\":" << V(m->direction) << "}";
                    else if (auto m = std::dynamic_pointer_cast<NiParticleGrowFade>(modifier))
                        out << "{\"type\":\"growFade\",\"grow\":" << F(m->grow) << ",\"fade\":" << F(m->fade) << "}";
                    else if (auto m = std::dynamic_pointer_cast<NiParticleRotation>(modifier))
                        out << "{\"type\":\"rotation\",\"speed\":" << F(m->speed) << "}";
                    else if (auto m = std::dynamic_pointer_cast<NiParticleColorModifier>(modifier))
                    {
                        out << "{\"type\":\"color\",\"keys\":[";
                        bool keyComma = false;
                        if (m->data.object())
                            for (const auto &key : m->data.object()->data.values)
                            {
                                if (keyComma)
                                    out << ",";
                                keyComma = true;
                                out << "[" << F(key.time) << "," << C(key.value) << "]";
                            }
                        out << "]}";
                    }
                    else
                        out << "{\"type\":" << Q(typeid(*modifier).name()) << "}";
                    modifier = modifier->next.object();
                }
                out << "]}";
            }
            out << "],\"meshes\":" << std::count_if(file.objects().begin(), file.objects().end(), [](const auto &v) { return bool(std::dynamic_pointer_cast<NiTriBasedGeometry>(v.second)); }) << "}";
        }
        catch (const std::exception &e)
        {
            out << ",\"error\":" << Q(e.what()) << "}";
            errors++;
        }
        files++;
    }
    out << "]";
    std::cout << files << " NIFs, " << emitters << " emitters, " << errors << " parse errors\n";
}
