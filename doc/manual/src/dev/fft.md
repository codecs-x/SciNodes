# `Fft` — radix-2 Cooley–Tukey

`src/core/Fft.{cpp,hpp}` implementa la FFT *radix-2*
iterativa con *bit-reversal permutation* + *butterflies*
por etapa.

## API

```cpp
namespace scinodes {
std::vector<float> magnitudeSpectrum(const float* samples, int N);
}
```

`N` debe ser potencia de 2.  Devuelve un vector de tamaño
`N/2 + 1` con `|X_k|` (magnitud).

## Costo

`O(N log N)` con `N` muestras.  Para `N = 256` (default del
`FFTAnalyzer`) son ~2 K ops de complejidad — *real-time* en
cualquier CPU moderna.
