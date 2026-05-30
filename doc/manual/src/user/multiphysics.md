# Catálogo multifísico

A partir de esta versión SciNodes incluye una familia de ocho
nodos electromagnéticos para diseñar y barrer un actuador
síncrono de imanes permanentes (PMSM) o sus variantes (IPM,
BLDC) analíticamente desde el editor. La idea es que el flujo
clásico de diseño de un motor —fijar punto de operación,
dimensionar, calcular el modelo electromagnético, mapear la
eficiencia— se expresa como una cadena de cables en el canvas.

## La cadena de diseño

El patrón típico es:

```
DesignTemplate ──▶ PMSMSizing ──▶ PMSMElectromagnetic ──▶ PMSMEfficiency ──▶ HeatmapSink
                       │              │                          │
                       ▼              ▼                          │
                  3× Oscilloscope  AirgapFluxDensity → Scope     │
                                                                 ▼
                                                          HeatmapSink (T,ω→η)
```

Cada eslabón es un nodo del catálogo. Cableando estás
expresando el flujo de información del diseño analítico; al
pulsar Run la cadena entera se evalúa en Scilab.

## `Design Template`

Una **fuente** con cuatro salidas que fija el punto de operación
deseado. Sus parámetros:

- `Target Torque` (Nm) — par requerido.
- `Target Speed` (rad/s) — velocidad mecánica nominal.
- `Bus Voltage` (V) — voltaje del bus DC disponible.
- `Cooling Class` — escala 1–3 (natural, aire forzado,
  refrigeración líquida).

Los cuatro salen como cables independientes hacia los nodos de
sizing.

## Sizing analítico: `PMSM`, `IPM`, `BLDC`

Tres variantes del mismo nodo, todas con dos entradas (torque y
velocidad del punto de diseño) y tres salidas (diámetro de
estator, longitud axial, dimensión de slot). Difieren en
parámetros y constantes:

- **`PMSM Sizing`** — superficie de imán; carga magnética B y
  carga eléctrica A controlan la geometría base.
- **`IPM Sizing`** — interior-permanent-magnet; agrega un
  `Saliency Factor` que premia la asimetría d/q.
- **`BLDC Sizing`** — back-EMF trapezoidal; agrega un
  `Trapezoidal Factor`.

Las tres usan la fórmula clásica `D ∝ (2T / (π · k · B · A · L/D))^(1/3)`
con `k` igual a 1, a `Saliency Factor`, o a `Trapezoidal Factor`
según la variante.

## `PMSM Electromagnetic`

Modelo lumped del motor: a partir de la geometría (bore D,
stack length L) y la velocidad mecánica ω entrega cuatro
observables:

- **Ke** — constante de back-EMF (V·s/rad).
- **L_ph** — inductancia síncrona por fase (H).
- **Vrms** — voltaje RMS línea-línea de back-EMF al ω de
  entrada (V).
- **Tcog** — pico de cogging-torque (Nm).

Las fórmulas usan `μ₀ = 4π·1e-7` y un *airgap* efectivo
`g_eff = g + h_m / μ_r` con `μ_r = 1.05` para NdFeB. Los
parámetros relevantes son `Turns per Phase`, `Winding Factor`,
`Pole Pairs`, `Airgap Flux Density`, `Mechanical Airgap`,
`Magnet Thickness` y `Slot Count`.

## `Air-Gap Flux Density`

Genera la serie temporal de la densidad de flujo magnético
B(θ) en el entrehierro, descomponiendo en fundamental, 3er
armónico y armónico de ranura. Útil para alimentar un
osciloscopio y observar la forma de onda real del flujo, no
sólo la fundamental.

## `PMSM Efficiency` + `Heatmap Sink`

`PMSM Efficiency` toma tres entradas (típicamente torque,
velocidad, y corriente del modelo electromagnético) y entrega la
eficiencia η = P_out / P_in descontando tres pérdidas: Joule
(I²R), hierro (kf·ω²), mecánicas (kw·ω). Sus parámetros son
`Stator Resistance`, `Iron Loss Coeff.` y `Mech Loss Coeff.`.

El `Heatmap Sink` recibe tres canales y los renderiza como una
grilla 2-D —el caso típico es alimentarlo con (T, ω, η) para
ver el mapa de eficiencia del actuador en su espacio de
operación.

## Procedural mesh en el visor 3-D

Cuando hay un nodo de sizing en el grafo, el visor 3-D
reemplaza el motor genérico por una **malla procedural del PMSM**
construida con los slot/pole counts del nodo. Cambiar
`Slot Count` o `Pole Count` regenera la geometría; el `VBO`
crece *on demand* para acomodar geometrías más grandes sin
reinicializar el pipeline.

## Import / Export de parámetros (per-nodo)

Por encima de la exportación de datos del *tag* anterior
(CSV per-sink, .sod global), esta versión agrega Import /
Export CSV **a nivel de parámetros del nodo**: el panel
flotante de parámetros gana un botón que serializa los valores
actuales a CSV y otro que los re-aplica desde un CSV
existente. Útil para guardar un *preset* de PMSM y compartirlo
entre proyectos.

## Sidecar Python FEM (opcional)

`doc/fem_sidecar/` contiene un *script* Python independiente
(`pmsm_lumped_corrections.py`) que aplica correcciones de orden
superior a la salida del modelo lumped. Es opcional —el editor
no lo lanza— y queda como herramienta externa para usuarios
que quieran refinar el resultado analítico con un solver FEM
clásico.

## Lo que NO es esta capa

El catálogo multifísico **no** acopla el motor a una geometría
arbitraria: la malla procedural es del PMSM canónico, no de un
robot genérico. La extensión a sistemas con geometría arbitraria
(contratos JSON + assets glTF) entra en versiones posteriores.
