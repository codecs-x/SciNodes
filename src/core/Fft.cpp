#include "Fft.hpp"
#include <cmath>

namespace scinodes {

static const float kTwoPi = 6.28318530717958647692f;

void fftInPlace(float* real, float* imag, int n) {
    if (!isPow2(n)) return;

    // ---- bit-reversal permutation -------------------------------------
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    // ---- butterflies --------------------------------------------------
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -kTwoPi / static_cast<float>(len);
        const float wlen_r = std::cos(ang);
        const float wlen_i = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float w_r = 1.0f, w_i = 0.0f;
            const int half = len >> 1;
            for (int k = 0; k < half; ++k) {
                const float u_r = real[i + k];
                const float u_i = imag[i + k];
                const float v_r = real[i + k + half] * w_r - imag[i + k + half] * w_i;
                const float v_i = real[i + k + half] * w_i + imag[i + k + half] * w_r;
                real[i + k]        = u_r + v_r;
                imag[i + k]        = u_i + v_i;
                real[i + k + half] = u_r - v_r;
                imag[i + k + half] = u_i - v_i;
                const float nw_r = w_r * wlen_r - w_i * wlen_i;
                const float nw_i = w_r * wlen_i + w_i * wlen_r;
                w_r = nw_r;
                w_i = nw_i;
            }
        }
    }
}

std::vector<float> magnitudeSpectrum(const float* samples, int n) {
    if (!isPow2(n) || n < 2) return {};
    std::vector<float> re(samples, samples + n);
    std::vector<float> im(n, 0.0f);
    fftInPlace(re.data(), im.data(), n);
    std::vector<float> mag(n / 2 + 1);
    for (int k = 0; k <= n / 2; ++k)
        mag[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
    return mag;
}

} // namespace scinodes
