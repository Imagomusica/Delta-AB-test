// EqDesign.h - pure C++17, no JUCE dependency.
// Designs FIR impulse responses for a parametric EQ from analog-prototype magnitudes
// evaluated directly on the frequency grid (no bilinear transform => no cramping at Nyquist).
//   Natural mode : minimum phase (cepstral method), the phase an analog EQ would have
//   Linear mode  : zero phase, delayed by L/2
// Stereo handling: bands act on L, R, M or S. The result is a 2x2 filter matrix
//   outL = LL*inL + LR*inR ,  outR = RL*inL + RR*inR
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace eqd
{
using cd = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;

enum class BandType { Bell = 0, LowShelf, HighShelf, HighPass, LowPass, Notch };
enum class Channel { Stereo = 0, Left, Right, Mid, Side };
enum class PhaseMode { Natural = 0, Linear };

struct Band
{
    bool on = false;
    BandType type = BandType::Bell;
    double freq = 1000.0;   // Hz
    double gainDb = 0.0;    // bell / shelves
    double q = 1.0;         // bell / shelves / notch
    int slopeDbOct = 12;    // high-pass / low-pass (6 dB/oct steps, Butterworth)
    Channel ch = Channel::Stereo;
};

constexpr int kMaxBands = 8;

// ---------------------------------------------------------------------------
// Magnitude (dB) of one band at frequency f (analog prototype, exact at every f)
inline double bandMagnitudeDb (const Band& b, double f)
{
    if (! b.on) return 0.0;
    const double x = f / b.freq;
    const double x2 = x * x;
    const double q = std::max (b.q, 0.05);
    const double A = std::pow (10.0, b.gainDb / 40.0);
    double mag = 1.0;

    switch (b.type)
    {
        case BandType::Bell:
        {
            const cd num (1.0 - x2, x * A / q), den (1.0 - x2, x / (A * q));
            mag = std::abs (num) / std::abs (den);
            break;
        }
        case BandType::LowShelf:
        {
            const double r = std::sqrt (A) / q;
            const cd num (A - x2, x * r), den (1.0 - A * x2, x * r);
            mag = A * std::abs (num) / std::abs (den);
            break;
        }
        case BandType::HighShelf:
        {
            const double r = std::sqrt (A) / q;
            const cd num (1.0 - A * x2, x * r), den (A - x2, x * r);
            mag = A * std::abs (num) / std::abs (den);
            break;
        }
        case BandType::Notch:
        {
            const cd num (1.0 - x2, 0.0), den (1.0 - x2, x / q);
            mag = std::abs (num) / std::abs (den);
            break;
        }
        case BandType::HighPass:
        case BandType::LowPass:
        {
            const int n = std::max (1, b.slopeDbOct / 6);          // Butterworth order
            const double p = std::pow (x, 2.0 * n);
            mag = (b.type == BandType::LowPass) ? 1.0 / std::sqrt (1.0 + p)
                                                : std::sqrt (p / (1.0 + p));
            break;
        }
    }
    return 20.0 * std::log10 (std::max (mag, 1e-12));
}

// Sum of all enabled bands acting on a channel group (dB)
inline double totalMagnitudeDb (const Band* bands, int n, Channel group, double f)
{
    double db = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto& b = bands[i];
        if (! b.on) continue;
        const bool hit = (b.ch == Channel::Stereo && (group == Channel::Left || group == Channel::Right))
                         || b.ch == group;
        if (hit) db += bandMagnitudeDb (b, f);
    }
    return db;
}

// ---------------------------------------------------------------------------
// Minimal radix-2 FFT (in place). sign = -1 forward, +1 inverse (unscaled)
class Fft
{
public:
    explicit Fft (size_t n) : N (n), tw (n / 2), rev (n)
    {
        for (size_t i = 0; i < N / 2; ++i)
            tw[i] = std::polar (1.0, -2.0 * kPi * (double) i / (double) N);
        size_t bits = 0;
        while ((size_t (1) << bits) < N) ++bits;
        for (size_t i = 0; i < N; ++i)
        {
            size_t r = 0;
            for (size_t b = 0; b < bits; ++b)
                if (i & (size_t (1) << b)) r |= size_t (1) << (bits - 1 - b);
            rev[i] = r;
        }
    }

    void transform (std::vector<cd>& a, bool inverse) const
    {
        for (size_t i = 0; i < N; ++i)
            if (i < rev[i]) std::swap (a[i], a[rev[i]]);
        for (size_t len = 2; len <= N; len <<= 1)
        {
            const size_t half = len / 2, step = N / len;
            for (size_t i = 0; i < N; i += len)
                for (size_t j = 0; j < half; ++j)
                {
                    const cd w = inverse ? std::conj (tw[j * step]) : tw[j * step];
                    const cd u = a[i + j], v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
    }

private:
    size_t N;
    std::vector<cd> tw;
    std::vector<size_t> rev;
};

// ---------------------------------------------------------------------------
struct DesignResult
{
    std::vector<float> ll, lr, rl, rr;   // each of length irLength
    bool crossActive = false;            // lr / rl are non-zero
    int irLength = 0;
    int latencySamples = 0;              // inherent delay of the filter (L/2 in Linear mode, else 0)
};

inline int irLengthFor (double fs, int quality)  // quality 0..3
{
    static const int base[4] = { 4096, 8192, 16384, 32768 };
    int scale = 1;
    if (fs > 70000.0) scale = 2;
    if (fs > 140000.0) scale = 4;
    return base[std::clamp (quality, 0, 3)] * scale;
}

// Builds the four impulse responses.
inline DesignResult design (const Band* bands, int numBands, double fs, int quality, PhaseMode mode)
{
    const int L = irLengthFor (fs, quality);
    const size_t N = (size_t) L * 4;
    const size_t half = N / 2;
    Fft fft (N);

    DesignResult res;
    res.irLength = L;
    res.latencySamples = (mode == PhaseMode::Linear) ? L / 2 : 0;
    for (auto* v : { &res.ll, &res.lr, &res.rl, &res.rr }) v->assign ((size_t) L, 0.0f);

    for (int i = 0; i < numBands; ++i)
        if (bands[i].on && (bands[i].ch == Channel::Mid || bands[i].ch == Channel::Side))
            res.crossActive = true;

    // frequency response (complex) of each channel group on bins 0..N-1
    auto groupSpectrum = [&] (Channel g, bool withDelay)
    {
        std::vector<cd> H (N);
        std::vector<double> lnMag (N);
        for (size_t k = 0; k <= half; ++k)
        {
            const double f = fs * (double) k / (double) N;
            const double db = totalMagnitudeDb (bands, numBands, g, std::max (f, 1e-3));
            const double mag = std::max (std::pow (10.0, db / 20.0), 1e-5);  // -100 dB floor
            lnMag[k] = std::log (mag);
            if (k > 0 && k < half) lnMag[N - k] = lnMag[k];
        }

        if (mode == PhaseMode::Linear)
        {
            for (size_t k = 0; k < N; ++k) H[k] = cd (std::exp (lnMag[k]), 0.0);
            // pure delay of L/2 samples (applied once per signal path)
            if (withDelay)
            for (size_t k = 0; k < N; ++k)
            {
                const double kk = (k <= half) ? (double) k : (double) k - (double) N;
                H[k] *= std::polar (1.0, -2.0 * kPi * kk * (double) (L / 2) / (double) N);
            }
            return H;
        }

        // minimum phase via real cepstrum
        std::vector<cd> c (N);
        for (size_t k = 0; k < N; ++k) c[k] = cd (lnMag[k], 0.0);
        fft.transform (c, true);
        for (auto& v : c) v /= (double) N;
        std::vector<cd> folded (N, cd (0.0, 0.0));
        folded[0] = c[0].real();
        for (size_t n = 1; n < half; ++n) folded[n] = 2.0 * c[n].real();
        folded[half] = c[half].real();
        fft.transform (folded, false);
        for (size_t k = 0; k < N; ++k) H[k] = std::exp (folded[k]);
        return H;
    };

    // In Linear mode the L/2 delay is carried by the L/R stage, or by the M/S stage when it is active.
    const auto HL = groupSpectrum (Channel::Left,  ! res.crossActive);
    const auto HR = groupSpectrum (Channel::Right, ! res.crossActive);
    std::vector<cd> HM, HS;
    if (res.crossActive) { HM = groupSpectrum (Channel::Mid, true); HS = groupSpectrum (Channel::Side, true); }

    // taper window applied to the time-domain result
    auto taper = [&] (size_t n)
    {
        const double pos = (double) n / (double) (L - 1);
        if (mode == PhaseMode::Linear)
        {
            const double edge = 0.2;
            if (pos < edge)       { const double t = pos / edge;          return 0.5 - 0.5 * std::cos (kPi * t); }
            if (pos > 1.0 - edge) { const double t = (1.0 - pos) / edge;  return 0.5 - 0.5 * std::cos (kPi * t); }
            return 1.0;
        }
        const double start = 0.85;                                          // natural: fade only the tail
        if (pos > start) { const double t = (pos - start) / (1.0 - start); return 0.5 + 0.5 * std::cos (kPi * t); }
        return 1.0;
    };

    auto emit = [&] (const std::vector<cd>& Hin, std::vector<float>& out)
    {
        std::vector<cd> t = Hin;
        fft.transform (t, true);
        for (size_t n = 0; n < (size_t) L; ++n)
            out[n] = (float) (t[n].real() / (double) N * taper (n));
    };

    std::vector<cd> t (N);
    if (! res.crossActive)
    {
        emit (HL, res.ll);
        emit (HR, res.rr);
        return res;
    }

    // 2x2 matrix  T^-1 * diag(HM,HS) * T * diag(HL,HR)
    std::vector<cd> LLs (N), LRs (N), RLs (N), RRs (N);
    for (size_t k = 0; k < N; ++k)
    {
        const cd sum  = 0.5 * (HM[k] + HS[k]);
        const cd diff = 0.5 * (HM[k] - HS[k]);
        LLs[k] = sum * HL[k];   LRs[k] = diff * HR[k];
        RLs[k] = diff * HL[k];  RRs[k] = sum * HR[k];
    }
    emit (LLs, res.ll);  emit (LRs, res.lr);
    emit (RLs, res.rl);  emit (RRs, res.rr);
    return res;
}

} // namespace eqd
