#pragma once
#include <vector>

// -----------------------------------------------------------------------
// Minimal radix-2 Cooley-Tukey FFT.
//
// In-place, decimation-in-time. The transform size must be a power of two;
// callers usually pick 128 / 256 / 512 to match a ring-buffer window.
//
// Two flavours:
//   • fftInPlace(real, imag, n)   — complex-valued in-place transform
//   • magnitudeSpectrum(samples, n) — wrapper for the common case: real
//                                     input, returns the one-sided
//                                     magnitude spectrum of length n/2+1
//
// The implementation is intentionally short (~60 lines) and dependency-
// free; for production-grade FFT, swap in kissfft / FFTW later.
// -----------------------------------------------------------------------
namespace scinodes {

// True iff `n` is a positive power of two.
inline bool isPow2(int n) { return n > 0 && (n & (n - 1)) == 0; }

// In-place FFT. `real` and `imag` are length-n arrays; on return they
// hold the complex spectrum X[k]. Does nothing if n is not a power of 2.
void fftInPlace(float* real, float* imag, int n);

// One-sided magnitude spectrum of a real input. Returns length n/2 + 1.
// |X[k]| where k ∈ [0, n/2].
std::vector<float> magnitudeSpectrum(const float* samples, int n);

} // namespace scinodes
