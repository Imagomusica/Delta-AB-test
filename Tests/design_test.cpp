// Dumps IRs for a set of scenarios so that an independent Python implementation can check them.
#include "../Source/EqDesign.h"
#include <cstdio>
#include <cstdint>
#include <string>
using namespace eqd;

static Band B (BandType t, double f, double g, double q, Channel ch = Channel::Stereo, int slope = 12)
{ Band b; b.on = true; b.type = t; b.freq = f; b.gainDb = g; b.q = q; b.ch = ch; b.slopeDbOct = slope; return b; }

static void dump (const std::string& name, const DesignResult& r)
{
    FILE* f = std::fopen (name.c_str(), "wb");
    int32_t hdr[3] = { r.irLength, r.latencySamples, r.crossActive ? 1 : 0 };
    std::fwrite (hdr, 4, 3, f);
    for (auto* v : { &r.ll, &r.lr, &r.rl, &r.rr }) std::fwrite (v->data(), 4, v->size(), f);
    std::fclose (f);
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    const double fs = argc > 2 ? std::atof (argv[2]) : 96000.0;
    struct Sc { const char* id; std::vector<Band> bands; };
    std::vector<Sc> sc = {
        { "A_bell1k",   { B (BandType::Bell, 1000, 6, 1) } },
        { "B_shelves",  { B (BandType::LowShelf, 100, 6, 0.71), B (BandType::HighShelf, 10000, 6, 0.71), B (BandType::Bell, 18000, 6, 1) } },
        { "C_bell30",   { B (BandType::Bell, 30, 6, 1) } },
        { "D_ms",       { B (BandType::Bell, 1000, 6, 1, Channel::Mid), B (BandType::Bell, 3000, -6, 1, Channel::Side) } },
        { "E_hp30_24",  { B (BandType::HighPass, 30, 0, 1, Channel::Stereo, 24) } },
        { "F_close",    { B (BandType::Bell, 1000, 6, 1), B (BandType::Bell, 1500, 6, 1) } },
    };
    for (auto& s : sc)
        for (int mode = 0; mode < 2; ++mode)
            for (int q : { 1, 2, 3 })
            {
                auto r = design (s.bands.data(), (int) s.bands.size(), fs, q, mode == 0 ? PhaseMode::Natural : PhaseMode::Linear);
                dump (out + "/" + s.id + (mode == 0 ? "_nat" : "_lin") + "_q" + std::to_string (q) + ".bin", r);
            }
    std::printf ("done\n");
    return 0;
}
