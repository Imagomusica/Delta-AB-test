#pragma once
#include "EqDesign.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Polyphase up/down sampler with a user-shaped FIR: cutoff, steepness and pre-ringing
// (pre-ringing 100 % = linear phase, 0 % = minimum phase, in between = blend of the two phases).
class SrcEngine
{
public:
    int K = 1;
    int latency = 0;      // total delay of up + down, in host-rate samples (integer)

    SrcEngine (int factor, double fs, double cutHz, int steepIdx, double preRinging, int maxBlock)
        : K (factor)
    {
        const double pi = eqd::kPi;
        static const double widthFrac[4] { 0.25, 0.10, 0.04, 0.015 };      // transition width, fraction of host Nyquist
        const double dn = widthFrac[std::clamp (steepIdx, 0, 3)] * 0.5 / (double) K;   // cycles / high-rate sample
        double fc = std::min (cutHz / (fs * K), 0.5 / K - dn * 0.5);
        fc = std::max (fc, 0.02 / K);
        const double A = 120.0, beta = 0.1102 * (A - 8.7);
        int N0 = (int) std::ceil ((A - 7.95) / (2.285 * 2.0 * pi * dn)) + 1;
        N0 = std::clamp (N0, 8 * K + 1, 4095);
        N0 = ((N0 - 1) / K) * K + 1;                    // N0 - 1 multiple of K
        const double D = (double) (N0 - 1) / 2.0;

        auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 60; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        std::vector<double> h0 ((size_t) N0);
        const double I0b = i0 (beta);
        for (int n = 0; n < N0; ++n)
        {
            const double t = (double) n - D, r = t / D;
            const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / I0b;
            h0[(size_t) n] = (std::abs (t) < 1e-9 ? 2.0 * fc : std::sin (2.0 * pi * fc * t) / (pi * t)) * w;
        }

        std::vector<double> h = h0;
        const double lam = 1.0 - std::clamp (preRinging, 0.0, 1.0);     // weight of the minimum-phase part
        if (lam > 1e-4)
        {
            size_t M = 1; while (M < (size_t) N0 * 8) M <<= 1;
            eqd::Fft fft (M);
            std::vector<eqd::cd> H (M, eqd::cd (0.0, 0.0));
            for (int n = 0; n < N0; ++n) H[(size_t) n] = h0[(size_t) n];
            fft.transform (H, false);
            std::vector<double> ln (M);
            for (size_t k = 0; k < M; ++k) ln[k] = std::log (std::max (std::abs (H[k]), 1e-7));
            std::vector<eqd::cd> c (M);
            for (size_t k = 0; k < M; ++k) c[k] = eqd::cd (ln[k], 0.0);
            fft.transform (c, true);
            for (auto& v : c) v /= (double) M;
            std::vector<eqd::cd> f (M, eqd::cd (0.0, 0.0));
            const size_t half = M / 2;
            f[0] = c[0].real();
            for (size_t n = 1; n < half; ++n) f[n] = 2.0 * c[n].real();
            f[half] = c[half].real();
            fft.transform (f, false);                    // log spectrum of the minimum-phase filter: real = ln|H|, imag = phase
            std::vector<double> ph (half + 1);
            double prev = 0.0, off = 0.0;
            for (size_t k = 0; k <= half; ++k)           // unwrap the minimum phase
            {
                double p = f[k].imag();
                if (k > 0) { while (p + off - prev > pi) off -= 2.0 * pi; while (p + off - prev < -pi) off += 2.0 * pi; }
                prev = p + off; ph[k] = prev;
            }
            std::vector<eqd::cd> B (M);
            for (size_t k = 0; k <= half; ++k)
            {
                const double w = 2.0 * pi * (double) k / (double) M;
                const double phi = lam * ph[k] + (1.0 - lam) * (-w * D);
                B[k] = std::polar (std::abs (H[k]), phi);
                if (k > 0 && k < half) B[M - k] = std::conj (B[k]);
            }
            B[half] = B[half].real();
            fft.transform (B, true);
            for (int n = 0; n < N0; ++n) h[(size_t) n] = B[(size_t) n].real() / (double) M;
        }

        double sum = 0.0, cen = 0.0;
        for (int n = 0; n < N0; ++n) { sum += h[(size_t) n]; cen += (double) n * h[(size_t) n]; }
        for (auto& v : h) v /= sum;                      // unit DC gain
        const int d = (int) std::lround (cen / sum);     // group delay at DC
        pad = ((K - ((2 * d) % K)) % K);
        latency = (2 * d + pad) / K;

        // polyphase tables for the up-sampler (gain K), padded filter for the down-sampler
        M_up = (N0 + K - 1) / K;
        hUp.assign ((size_t) K * (size_t) M_up, 0.0f);
        for (int p = 0; p < K; ++p)
            for (int m = 0; m < M_up; ++m)
            {
                const int idx = m * K + p;
                hUp[(size_t) p * (size_t) M_up + (size_t) m] = idx < N0 ? (float) (h[(size_t) idx] * (double) K) : 0.0f;
            }
        Nd = N0 + pad;
        hDown.assign ((size_t) Nd, 0.0f);
        for (int i = 0; i < N0; ++i) hDown[(size_t) (i + pad)] = (float) h[(size_t) i];
        upRing[0].assign ((size_t) M_up * 2, 0.0f); upRing[1] = upRing[0];
        dnLen = Nd + K - 1;
        dnRing[0].assign ((size_t) dnLen * 2, 0.0f); dnRing[1] = dnRing[0];
        upPos[0] = upPos[1] = 0; dnPos[0] = dnPos[1] = 0;
        upBuf[0].assign ((size_t) maxBlock * (size_t) K, 0.0f); upBuf[1] = upBuf[0];
    }

    float* upChannel (int ch) { return upBuf[ch].data(); }

    void up (const float* const* in, int n)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& ring = upRing[ch]; int pos = upPos[ch];
            float* out = upBuf[ch].data();
            for (int i = 0; i < n; ++i)
            {
                pos = (pos == 0 ? M_up - 1 : pos - 1);
                ring[(size_t) pos] = ring[(size_t) pos + (size_t) M_up] = in[ch][i];
                const float* r = ring.data() + pos;
                for (int p = 0; p < K; ++p)
                {
                    const float* c = hUp.data() + (size_t) p * (size_t) M_up;
                    float acc = 0.0f;
                    for (int m = 0; m < M_up; ++m) acc += c[m] * r[m];
                    out[i * K + p] = acc;
                }
            }
            upPos[ch] = pos;
        }
    }

    void down (float* const* out, int n)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& ring = dnRing[ch]; int pos = dnPos[ch];
            const float* u = upBuf[ch].data();
            for (int i = 0; i < n; ++i)
            {
                for (int k = 0; k < K; ++k)
                {
                    pos = (pos == 0 ? dnLen - 1 : pos - 1);
                    ring[(size_t) pos] = ring[(size_t) pos + (size_t) dnLen] = u[i * K + k];
                }
                const float* r = ring.data() + pos + (K - 1);
                float acc = 0.0f;
                for (int j = 0; j < Nd; ++j) acc += hDown[(size_t) j] * r[j];
                out[ch][i] = acc;
            }
            dnPos[ch] = pos;
        }
    }

private:
    int pad = 0, M_up = 1, Nd = 1, dnLen = 1;
    std::vector<float> hUp, hDown;
    std::vector<float> upRing[2], dnRing[2], upBuf[2];
    int upPos[2] { 0, 0 }, dnPos[2] { 0, 0 };
};
