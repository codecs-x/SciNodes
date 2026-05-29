# Catálogo multifísico: PMSM + térmico + structural

A partir de esta versión el catálogo crece con bloques
analíticos de multifísica.

## PMSM analítico

- `DesignTemplate` — *bundle* de requisitos de diseño.
- `PMSMSizing` / `IPMSizing` / `BLDCSizing` — ecuaciones de
  sizing clásicas (estator, rotor, *trapezoidal factor*).
- `PMSMElectromagnetic` — Ke, Ld = Lq, V_rms, T_cog.
- `AirgapFluxDensity` — B_g(t) en un punto del estator.
- `PMSMEfficiency` — η a partir de (T, ω, Ke) + pérdidas.
- `HeatmapSink` — *heatmap* 2-D (x, y, value).

## Red térmica

Ecuaciones de pérdidas + nodos RC para construir la red
térmica del motor.

- `JouleLoss` — copper loss desde (T, Ke).
- `CoreLoss` — iron loss desde (ω, B_g).
- `MechanicalLoss` — fricción + *windage* desde ω.
- `ThermalMass` — single-node RC.
- `ThermalNode` — capacitancia pura, 4 *heat inputs*.
- `ThermalResistance` — *dual-output* H ↔ C.
- `CoolingSystem` — knobs de ambiente / agua / aire.
- `ConvectiveCooling` — h(flow) · ΔT.
- `View3DThermalSink` — colorea la malla por temperatura.

## Structural / NVH

- `MaxwellForce` — presión radial σ = B² / 2μ₀.
- `ModalFrequency` — frecuencia natural modo-m de anillo
  delgado.
- `TolerancePerturbator` — ruido uniforme ±h para
  Monte Carlo.
- `DistributionSink` — histograma acumulado.
- `View3DDeformationSink` — animación modal sobre la malla.

## Sidecar Python

`doc/fem_sidecar/` contiene un *script* Python opcional que
calcula correcciones de orden superior para el PMSM y las
expone como un archivo de calibración que `PMSMSizing` lee
si está presente.
