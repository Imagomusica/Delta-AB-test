#!/usr/bin/env python3
"""Independent check of the C++ IR designer: analytic magnitudes re-implemented with numpy."""
import sys, glob, os
import numpy as np

FS = float(sys.argv[2]) if len(sys.argv) > 2 else 96000.0
D = sys.argv[1]


def band(t, f0, g, q, slope=12):
    def H(f):
        x = f / f0
        s = 1j * x
        A = 10 ** (g / 40)
        if t == "bell":
            return (s**2 + s * A / q + 1) / (s**2 + s / (A * q) + 1)
        if t == "ls":
            r = np.sqrt(A) / q
            return A * (s**2 + r * s + A) / (A * s**2 + r * s + 1)
        if t == "hs":
            r = np.sqrt(A) / q
            return A * (A * s**2 + r * s + 1) / (s**2 + r * s + A)
        if t == "hp":
            n = slope // 6
            p = x ** (2 * n)
            return np.sqrt(p / (1 + p))
    return H


SC = {
    "A_bell1k": dict(L=[band("bell", 1000, 6, 1)], R=[band("bell", 1000, 6, 1)], M=[], S=[]),
    "B_shelves": None, "C_bell30": None, "D_ms": None, "E_hp30_24": None, "F_close": None, "G_tilt": None,
}
bell = lambda f, g, q: band("bell", f, g, q)
both = lambda lst: dict(L=lst, R=lst, M=[], S=[])
SC["A_bell1k"] = both([bell(1000, 6, 1)])
SC["B_shelves"] = both([band("ls", 100, 6, 0.71), band("hs", 10000, 6, 0.71), bell(18000, 6, 1)])
SC["C_bell30"] = both([bell(30, 6, 1)])
SC["D_ms"] = dict(L=[], R=[], M=[bell(1000, 6, 1)], S=[bell(3000, -6, 1)])
SC["E_hp30_24"] = both([band("hp", 30, 0, 1, 24)])
SC["F_close"] = both([bell(1000, 6, 1), bell(1500, 6, 1)])
def tilt3(f):
    return 10 ** (3.0 * np.tanh(np.log2(np.maximum(f, 1.0) / 650.0) / 2.2) / 20)
SC["G_tilt"] = both([bell(2000, -3, 2), lambda f: tilt3(f) + 0j])


def mag(lst, f):
    m = np.ones_like(f, dtype=complex)
    for h in lst:
        m = m * np.abs(h(f))
    return np.abs(m)


def load(p):
    raw = np.fromfile(p, dtype=np.int32, count=3)
    L, lat, cross = map(int, raw)
    data = np.fromfile(p, dtype=np.float32, offset=12)
    ir = [data[i * L:(i + 1) * L].astype(np.float64) for i in range(4)]
    return L, lat, cross, ir


NFFT = 1 << 20
f = np.fft.rfftfreq(NFFT, 1 / FS)
rows = []
for p in sorted(glob.glob(os.path.join(D, "*.bin"))):
    name = os.path.basename(p)[:-4]
    sc = name.split("_nat")[0].split("_lin")[0].split("_min")[0]
    nat = "_lin" not in name
    natural = "_nat" in name
    L, lat, cross, ir = load(p)
    spec = [np.fft.rfft(x, NFFT) for x in ir]
    ref = SC[sc]
    sel = (f >= 20) & (f <= 20000)
    if not cross:
        errs = []
        for ch, key in ((0, "L"), (3, "R")):
            target = mag(ref[key], f[sel])
            got = np.abs(spec[ch][sel])
            errs.append(np.max(np.abs(20 * np.log10(got / target))))
        err = max(errs)
        # cross terms should be exactly zero
        xt = 0.0
    else:
        M = mag(ref["M"], f[sel]); S = mag(ref["S"], f[sel])
        tLL = 0.5 * (M + S); tLR = 0.5 * (M - S)
        # compare complex-free: for natural mode compare as complex sums is not possible from magnitudes alone,
        # so check input->output behaviour: mid-only and side-only excitation
        # mid input (L=R=1): outL = LL+LR ; side input (L=1,R=-1): outL = LL-LR
        midOut = np.abs(spec[0][sel] + spec[1][sel])
        sideOut = np.abs(spec[0][sel] - spec[1][sel])
        err = max(np.max(np.abs(20 * np.log10(midOut / M))), np.max(np.abs(20 * np.log10(sideOut / S))))
        xt = float(np.max(np.abs(ir[1])))
    # time-domain properties
    h = ir[0]
    pk = int(np.argmax(np.abs(h)))
    e = h ** 2
    pre = float(10 * np.log10((np.sum(e[:max(pk - 8, 0)]) + 1e-30) / np.sum(e)))  # energy before the peak
    edge = float(20 * np.log10(np.max(np.abs(h[int(0.97 * L):])) / np.max(np.abs(h)) + 1e-30))
    # linear-phase symmetry
    sym = float(np.max(np.abs(ir[0][1:L] - ir[0][1:L][::-1]))) if not nat and not cross else float("nan")
    # gain at 1 kHz vs low freq limits etc. already in err
    phs = float("nan")
    if natural and not cross and sc in ("A_bell1k", "F_close", "B_shelves"):
        ff = f[(f > 30) & (f < 0.4 * FS / 2)]
        idx = np.searchsorted(f, ff)
        an = np.ones_like(ff, dtype=complex)
        for h in SC[sc]["L"]:
            an = an * h(ff)
        got = spec[0][idx]
        d = np.angle(got / an)
        phs = float(np.degrees(np.max(np.abs(d))))
    rows.append((name, L, lat, err, pre, edge, phs))

print(f"{'case':22s} {'L':>7s} {'lat':>7s} {'maxErr dB':>10s} {'preEnergy dB':>13s} {'tail dB':>8s} {'phase err deg (nat)':>20s}")
for n, L, lat, err, pre, edge, phs in rows:
    print(f"{n:22s} {L:7d} {lat:7d} {err:10.4f} {pre:13.1f} {edge:8.1f} {phs:20.3f}")
