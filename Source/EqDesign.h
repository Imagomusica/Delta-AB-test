// EqDesign.h - pure C++17, no JUCE dependency.
// Designs FIR impulse responses for a parametric EQ from analog-prototype magnitudes
// evaluated directly on the frequency grid (no bilinear transform => no cramping at Nyquist).
//   Linear  : zero phase, delayed by L/2
//   Natural : phase of the equivalent analog filters, blended into minimum phase near Nyquist
//   Minimal : pure minimum phase of the designed magnitude (cepstral method)
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
enum class PhaseMode { Linear = 0, Natural = 1, Minimal = 2 };

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

// Tilt: smooth see-saw around a pivot, +db at the top, -db at the bottom
struct Tilt
{
    bool on = false;
    double db = 0.0;
    double pivot = 650.0;
};

constexpr int kMaxBands = 8;

inline double tiltDb (const Tilt& t, double f)
{
    if (! t.on) return 0.0;
    return t.db * std::tanh (std::log2 (std::max (f, 1.0) / t.pivot) / 2.2);
}

// ---------------------------------------------------------------------------
// Complex analog response of one band at frequency f
inline cd bandResponse (const Band& b, double f)
{
    if (! b.on) return cd (1.0, 0.0);
    const double x = f / b.freq;
    const double x2 = x * x;
    const double q = std::max (b.q, 0.05);
    const double A = std::pow (10.0, b.gainDb / 40.0);

    switch (b.type)
    {
        case BandType::Bell:
        {
            const cd num (1.0 - x2, x * A / q), den (1.0 - x2, x / (A * q));
            return num / den;
        }
        case BandType::LowShelf:
        {
            const double r = std::sqrt (A) / q;
            const cd num (A - x2, x * r), den (1.0 - A * x2, x * r);
            return A * num / den;
        }
        case BandType::HighShelf:
        {
            const double r = std::sqrt (A) / q;
            const cd num (1.0 - A * x2, x * r), den (A - x2, x * r);
            return A * num / den;
        }
        case BandType::Notch:
        {
            const cd num (1.0 - x2, 0.0), den (1.0 - x2, x / q);
            return num / den;
        }
        case BandType::HighPass:
        case BandType::LowPass:
        {
            const int n = std::max (1, b.slopeDbOct / 6);          // Butterworth order
            const cd s (0.0, x);
            cd B (1.0, 0.0);
            for (int k = 1; k <= n; ++k)
                B *= (s - std::polar (1.0, kPi * (double) (2 * k + n - 1) / (double) (2 * n)));
            if (b.type == BandType::LowPass) return cd (1.0, 0.0) / B;
            cd sn (1.0, 0.0);
            for (int k = 0; k < n; ++k) sn *= s;
            return sn / B;
        }
    }
    return cd (1.0, 0.0);
}

inline double bandMagnitudeDb (const Band& b, double f)
{
    if (! b.on) return 0.0;
    return 20.0 * std::log10 (std::max (std::abs (bandResponse (b, f)), 1e-12));
}

inline bool bandHitsGroup (const Band& b, Channel group)
{
    return (b.ch == Channel::Stereo && (group == Channel::Left || group == Channel::Right)) || b.ch == group;
}

// Sum of all enabled bands (+ tilt on the L/R stage) acting on a channel group (dB)
inline double totalMagnitudeDb (const Band* bands, int n, Channel group, double f, const Tilt* tilt = nullptr)
{
    double db = 0.0;
    for (int i = 0; i < n; ++i)
        if (bands[i].on && bandHitsGroup (bands[i], group)) db += bandMagnitudeDb (bands[i], f);
    if (tilt != nullptr && (group == Channel::Left || group == Channel::Right)) db += tiltDb (*tilt, f);
    return db;
}

// ---------------------------------------------------------------------------
// Minimal radix-2 FFT (in place). inverse = false forward, true inverse (unscaled)
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

// Filter duration in seconds for each resolution setting
inline double durationFor (int quality)
{
    static const double d[4] = { 0.35, 0.7, 1.4, 2.8 };
    return d[std::clamp (quality, 0, 3)];
}

inline int irLengthFor (double fs, int quality)
{
    const double target = durationFor (quality) * fs;
    const int p = (int) std::lround (std::log2 (std::max (target, 256.0)));
    return 1 << p;
}

// Builds the four impulse responses.
inline DesignResult design (const Band* bands, int numBands, const Tilt& tilt, double fs, int quality, PhaseMode mode)
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

    // minimum-phase spectrum from a log-magnitude array (symmetric, length N)
    auto minPhase = [&] (const std::vector<double>& lnMag)
    {
        std::vector<cd> c (N);
        for (size_t k = 0; k < N; ++k) c[k] = cd (lnMag[k], 0.0);
        fft.transform (c, true);
        for (auto& v : c) v /= (double) N;
        std::vector<cd> folded (N, cd (0.0, 0.0));
        folded[0] = c[0].real();
        for (size_t n = 1; n < half; ++n) folded[n] = 2.0 * c[n].real();
        folded[half] = c[half].real();
        fft.transform (folded, false);
        for (auto& v : folded) v = std::exp (v);
        return folded;
    };

    const double fNyq = fs / 2.0;
    auto naturalWeight = [&] (double f)           // 1 below 0.45 Nyquist, 0 above 0.95 Nyquist
    {
        const double x = f / fNyq;
        if (x <= 0.45) return 1.0;
        if (x >= 0.95) return 0.0;
        return 0.5 * (1.0 + std::cos (kPi * (x - 0.45) / 0.5));
    };

    // minimum-phase spectrum of the tilt alone (Natural mode needs it once)
    std::vector<cd> tiltMin;
    if (mode == PhaseMode::Natural && tilt.on && std::abs (tilt.db) > 1e-9)
    {
        std::vector<double> ln (N);
        for (size_t k = 0; k <= half; ++k)
        {
            const double db = tiltDb (tilt, fs * (double) k / (double) N);
            ln[k] = db / 20.0 * std::log (10.0);
            if (k > 0 && k < half) ln[N - k] = ln[k];
        }
        tiltMin = minPhase (ln);
    }

    // frequency response (complex) of each channel group on bins 0..N-1
    auto groupSpectrum = [&] (Channel g, bool withDelay)
    {
        std::vector<double> lnMag (N);
        const bool lr = (g == Channel::Left || g == Channel::Right);
        for (size_t k = 0; k <= half; ++k)
        {
            const double f = fs * (double) k / (double) N;
            const double db = totalMagnitudeDb (bands, numBands, g, std::max (f, 1e-3), lr ? &tilt : nullptr);
            const double mag = std::max (std::pow (10.0, db / 20.0), 1e-5);  // -100 dB floor
            lnMag[k] = std::log (mag);
            if (k > 0 && k < half) lnMag[N - k] = lnMag[k];
        }

        if (mode == PhaseMode::Linear)
        {
            std::vector<cd> H (N);
            for (size_t k = 0; k < N; ++k) H[k] = cd (std::exp (lnMag[k]), 0.0);
            if (withDelay)                                  // pure delay of L/2 samples, applied once per path
                for (size_t k = 0; k < N; ++k)
                {
                    const double kk = (k <= half) ? (double) k : (double) k - (double) N;
                    H[k] *= std::polar (1.0, -2.0 * kPi * kk * (double) (L / 2) / (double) N);
                }
            return H;
        }

        auto H = minPhase (lnMag);

        if (mode == PhaseMode::Natural)
        {
            // pull the phase towards that of the equivalent analog filters (below ~0.45 Nyquist)
            for (size_t k = 1; k < half; ++k)
            {
                const double f = fs * (double) k / (double) N;
                const double w = naturalWeight (f);
                if (w <= 0.0) { H[N - k] = std::conj (H[k]); continue; }
                cd A (1.0, 0.0);
                for (int i = 0; i < numBands; ++i)
                    if (bands[i].on && bandHitsGroup (bands[i], g)) A *= bandResponse (bands[i], f);
                if (lr && ! tiltMin.empty()) A *= tiltMin[k];
                const double am = std::abs (A), hm = std::abs (H[k]);
                if (am > 1e-9 && hm > 1e-9)
                {
                    const cd ratio = (A / am) * std::conj (H[k] / hm);
                    H[k] *= std::polar (1.0, std::arg (ratio) * w);
                }
                H[N - k] = std::conj (H[k]);
            }
        }
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
        const double start = 0.85;                                          // minimum phase: fade only the tail
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
