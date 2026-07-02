#!/usr/bin/env python3
"""Numerical validation harness for the MBOIT transmittance reconstruction.

Mirrors the 4 power moment solver in Shaders/mboit.inc in float32 arithmetic so
changes to the bias strength, bias vector, storage formats or the solver itself
can be validated against analytic ground truth before touching shader code.

The moment problem solvers degenerate on a single isolated fragment: it produces
a rank one Hankel matrix whose Cholesky pivots drown in fp32 cancellation noise
unless the moment bias is strong enough. This script sweeps the cases that have
bitten in practice: isolated surfaces across the warped depth range (NaN bands
that made glass panes invisible), two layer separation accuracy, deep stacks of
coincident faint fragments and the clamped edges of the depth warp.

Storage rounding can be overridden per accumulation to evaluate reduced
precision moment targets (see `f16`); fp16 power moments require a ~50x
stronger bias and visibly blur close layers, which is why the moment render
targets are 32 bit.
"""

import numpy as np

f32 = np.float32

# matches MBOIT_OVERESTIMATION / the bias arguments in Shaders/mboit.inc
OVERESTIMATION = f32(0.25)
BIAS = f32(6e-5)
BIAS_VECTOR = np.array([0.0, 0.375, 0.0, 0.375], dtype=np.float32)


def f16(x):
    """fp16 storage rounding, as if the moment targets were R16 formats."""
    return np.float32(np.float16(x))


def fma(a, b, c):
    return f32(f32(a) * f32(b) + f32(c))


def absorbance(alpha):
    return f32(-np.log(max(1.0 - min(alpha, 0.999), 1e-4)))


def transmittance4(b_0, b_even, b_odd, depth, bias=BIAS, bias_vector=BIAS_VECTOR):
    """Port of MBOITTransmittance4 from Shaders/mboit.inc."""
    b = np.array([b_odd[0], b_even[0], b_odd[1], b_even[1]], dtype=np.float32)
    b = b * (1 - f32(bias)) + bias_vector * f32(bias)
    z0 = f32(depth)

    L21D11 = fma(-b[0], b[1], b[2])
    D11 = fma(-b[0], b[0], b[1])
    InvD11 = f32(1.0 / D11)
    L21 = f32(L21D11 * InvD11)
    squared_depth_variance = fma(-b[1], b[1], b[3])
    D22 = fma(-L21D11, L21, squared_depth_variance)

    c = np.array([1.0, z0, z0 * z0], dtype=np.float32)
    c[1] -= b[0]
    c[2] -= b[1] + L21 * c[1]
    c[1] *= InvD11
    with np.errstate(all="ignore"):
        c[2] /= D22
        c[1] -= L21 * c[2]
        c[0] -= c[1] * b[0] + c[2] * b[1]
        InvC2 = f32(1.0 / c[2])
        p = f32(c[1] * InvC2)
        q = f32(c[0] * InvC2)
        D = f32(p * p * 0.25 - q)
        r = f32(np.sqrt(D)) if D >= 0 else f32(np.nan)
        z1 = f32(-p * 0.5 - r)
        z2 = f32(-p * 0.5 + r)

        f0 = float(OVERESTIMATION)
        f1 = 1.0 if z1 < z0 else 0.0
        f2 = 1.0 if z2 < z0 else 0.0
        f01 = f32((f1 - f0) / (z1 - z0))
        f12 = f32((f2 - f1) / (z2 - z1))
        f012 = f32((f12 - f01) / (z2 - z0))
        poly = np.zeros(3, dtype=np.float32)
        poly[0] = f012
        poly[1] = poly[0]
        poly[0] = f32(f01 - poly[0] * z1)
        poly[2] = poly[1]
        poly[1] = f32(poly[0] - poly[1] * z0)
        poly[0] = f32(f0 - poly[0] * z0)
        absorb = f32(poly[0] + b[0] * poly[1] + b[1] * poly[2])
    # the shader falls back to the overestimation weight when the
    # reconstruction degenerates; mirror it but report the degeneracy
    degenerate = bool(np.isnan(absorb))
    if degenerate:
        absorb = OVERESTIMATION
    return float(np.clip(np.exp(-b_0 * absorb), 0, 1)), degenerate


def accumulate(layers, storage=f32):
    """Additively accumulate (depth, alpha) fragments with the given storage rounding."""
    b0 = f32(0)
    m = np.zeros(4, dtype=np.float32)
    for zw, alpha in layers:
        zw = f32(zw)
        a = absorbance(alpha)
        b0 = storage(b0 + a)
        powers = np.array([zw, zw**2, zw**3, zw**4], dtype=np.float32) * a
        for i in range(4):
            m[i] = storage(m[i] + powers[i])
    return b0, m


def reconstruct(b0, m, depth, bias=BIAS):
    if b0 <= 1e-3:
        return 1.0, False
    mn = m / b0
    return transmittance4(b0, [mn[1], mn[3]], [mn[0], mn[2]], depth, bias)


def main():
    failures = 0

    # single isolated surface across the full warped depth range: this is the
    # case that produced NaN bands with the paper's 32 bit bias recommendation
    a = absorbance(0.5)
    expected = np.exp(-float(OVERESTIMATION) * a)
    worst = 0.0
    degenerates = 0
    for zw in np.linspace(-1.0, 1.0, 801):
        b0, m = accumulate([(zw, 0.5)])
        t, degenerate = reconstruct(b0, m, f32(zw))
        degenerates += degenerate
        worst = max(worst, abs(t - expected))
    print(f"single surface: fallbacks={degenerates}/801 worst error={worst:.4f} (expected T={expected:.4f})")
    # isolated knife edge depths are expected; a broad band of them is a regression
    failures += (degenerates > 8) + (worst > 0.005)

    # two layers: the reconstruction should be exact for one and two Diracs
    for dz in (0.02, 0.05, 0.1, 0.2, 0.4):
        errs = []
        for z0 in np.linspace(-0.9, 0.9 - dz, 37):
            b0, m = accumulate([(z0, 0.5), (z0 + dz, 0.5)])
            front, _ = reconstruct(b0, m, f32(z0))
            back, _ = reconstruct(b0, m, f32(z0 + dz))
            errs.append(abs(front - np.exp(-0.25 * a)))
            errs.append(abs(back - np.exp(-1.25 * a)))
        print(f"two layers dz={dz:.2f}: worst error={max(errs):.4f}")
        failures += max(errs) > 0.05

    # deep stack of coincident faint fragments
    b0, m = accumulate([(0.3, 0.1)] * 16)
    t, _ = reconstruct(b0, m, f32(0.3))
    expected = np.exp(-0.25 * 16 * absorbance(0.1))
    print(f"16 faint fragments: T={t:.4f} (expected {expected:.4f})")
    failures += abs(t - expected) > 0.01

    print("FAIL" if failures else "OK")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
