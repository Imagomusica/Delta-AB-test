#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// ITU-R BS.1770 / EBU R128 loudness: K-weighting, momentary (400 ms), short-term (3 s), gated integrated
class LoudnessMeter
{
public:
    void prepare (double fs)
    {
        hop = std::max (1, (int) std::lround (0.1 * fs));
        const double pi = 3.14159265358979323846;
        {   // stage 1: high shelf
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {   // stage 2: RLB high-pass
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        clear();
    }

    void requestReset() { resetFlag = true; }

    void process (const float* const* x, int numCh, int n)
    {
        if (resetFlag.exchange (false)) clear();
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh && c < 2; ++c)
            {
                const double y = run (s2, z2[c], run (s1, z1[c], (double) x[c][i]));
                acc += y * y;
            }
            if (++count >= hop) { endBlock(); }
        }
    }

    std::atomic<float> momentary { -120.0f }, shortTerm { -120.0f }, integrated { -120.0f };

private:
    struct Bq { double b0, b1, b2, a1, a2; };
    static double run (const Bq& q, double* z, double x)
    {
        const double y = q.b0 * x + z[0];
        z[0] = q.b1 * x - q.a1 * y + z[1];
        z[1] = q.b2 * x - q.a2 * y;
        return y;
    }
    static float lufs (double e) { return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : -120.0f; }

    void clear()
    {
        for (auto& z : z1) z[0] = z[1] = 0.0;
        for (auto& z : z2) z[0] = z[1] = 0.0;
        ring.fill (0.0); nb = 0; acc = 0.0; count = 0;
        hist.assign (kBins, 0); histE.assign (kBins, 0.0);
        momentary = shortTerm = integrated = -120.0f;
    }

    void endBlock()
    {
        const double e = acc / (double) hop;
        acc = 0.0; count = 0;
        ring[(size_t) (nb % 30)] = e;
        ++nb;
        auto meanLast = [&] (int k)
        {
            k = std::min (k, nb);
            double s = 0.0;
            for (int j = 0; j < k; ++j) s += ring[(size_t) ((nb - 1 - j) % 30)];
            return s / (double) std::max (k, 1);
        };
        if (nb >= 4)
        {
            const double e4 = meanLast (4);
            const float L = lufs (e4);
            momentary = L;
            if (L > -70.0f)
            {
                const int b = std::min (kBins - 1, std::max (0, (int) ((L + 70.0f) * 10.0f)));
                ++hist[(size_t) b]; histE[(size_t) b] += e4;
            }
        }
        shortTerm = lufs (meanLast (30));

        double tE = 0.0; long tN = 0;
        for (int b = 0; b < kBins; ++b) { tE += histE[(size_t) b]; tN += hist[(size_t) b]; }
        if (tN == 0) { integrated = -120.0f; return; }
        const double rel = -0.691 + 10.0 * std::log10 (tE / (double) tN) - 10.0;
        const int b0 = std::max (0, (int) std::ceil ((rel + 70.0) * 10.0));
        double gE = 0.0; long gN = 0;
        for (int b = b0; b < kBins; ++b) { gE += histE[(size_t) b]; gN += hist[(size_t) b]; }
        integrated = gN > 0 ? lufs (gE / (double) gN) : -120.0f;
    }

    static constexpr int kBins = 1001;
    Bq s1 { 1, 0, 0, 0, 0 }, s2 { 1, 0, 0, 0, 0 };
    double z1[2][2] {}, z2[2][2] {};
    std::array<double, 30> ring {};
    std::vector<int> hist;
    std::vector<double> histE;
    double acc = 0.0;
    int count = 0, nb = 0, hop = 4800;
    std::atomic<bool> resetFlag { false };
};
