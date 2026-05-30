# Estructural y NVH

A partir de v0.0.6 SciNodes cierra el lazo multifísico de cuatro
etapas que aparece en los textos clásicos de diseño de máquinas
eléctricas: del campo del entrehierro a la tensión mecánica, de la
tensión a la frecuencia natural del modo dominante, y de ahí a una
animación visual del modo y a una distribución de Monte-Carlo
sobre cualquier observable del grafo.

## La cadena estructural

El patrón típico continúa la cadena electromagnética del *tag*
anterior:

```
… → AirgapFluxDensity → MaxwellForce ────────────────────────┐
                                                              │
DesignTemplate → PMSMSizing → R_stator → ModalFrequency ─┐    │
                                                          │    │
                                              3D Deformation Overlay
                                                          │
                                       (sigma_r se mide en Scope)
```

`AirgapFluxDensity` ya entregaba B(θ) en el *tag* anterior; ahora
ese valor alimenta `Maxwell Force` para obtener la tensión radial
en el entrehierro, y un cable paralelo lleva el radio medio del
stator a `Modal Frequency` para obtener la frecuencia natural del
modo dominante del anillo. Las tres salidas (frecuencia, modo,
amplitud) entran a `3D Deformation Overlay`, que anima la malla
del visor 3-D con el modo correspondiente.

## `Maxwell Force`

Un transformador 1→1 sin parámetros. Aplica la fórmula cerrada de
Maxwell para la tensión radial en el entrehierro:

```
sigma_r = B_g² / (2 · mu_0)
```

con `mu_0 = 4·π·1e-7 H/m`. Toma densidad de flujo (T) y entrega
tensión radial en Pa. Para `B_g = 1 T` el resultado es
`≈ 397 887 Pa`, valor que se usa como referencia en el test de
aceptación.

## `Modal Frequency`

Un transformador 1→1 que entrega la frecuencia natural del modo
`m` de un anillo cilíndrico delgado:

```
f_m = (t / (2·π·R²)) · √(E / (12·ρ)) · m·(m²−1) / √(m²+1)
```

Sus parámetros son `Young's Modulus` (Pa), `Density` (kg/m³),
`Thickness` (m) y `Mode Order` (entero). El radio medio entra por
el puerto.

Hay una **guarda rigid-body**: cuando `Mode Order ≤ 1` el nodo
devuelve `0` en lugar de divisiones-por-cero o raíces inválidas.
Esto permite hacer barridos del modo de 1 a N sin reventar el
solver.

## `Tolerance Perturbator` + `Distribution Sink`

Estos dos nodos son la herramienta de análisis estadístico ligero.

- **`Tolerance Perturbator`** — un transformador estocástico:
  `y = u + h·(2·rand() − 1)`. Una nueva muestra uniforme cada
  paso. Cablear esto a la entrada de cualquier observable
  convierte el grafo en un experimento Monte-Carlo en vivo: un
  paso del solver = un trial.

- **`Distribution Sink`** — recibe un canal y lo dispone como
  histograma de `Bin Count` cubetas. El `PlotPanel` muestra las
  barras junto a la media, la desviación, el mínimo y el máximo
  observados, todo recalculado en línea desde el *ring buffer*
  del *bridge*.

La cadena `Step → TolerancePerturbator(h) → DistributionSink` es
el patrón canónico, pero el perturbador puede inyectarse en medio
de la cadena electromagnética o estructural para medir la
sensibilidad de la frecuencia modal a la tolerancia de espesor,
por ejemplo.

## Visualización del modo: `3D Deformation Overlay`

Un sumidero con tres entradas: frecuencia, modo, amplitud. El
visor 3-D consume las tres y aplica un desplazamiento radial por
vértice sobre la malla procedural del PMSM:

```
Δr(θ, t) = A · cos(m · θ) · sin(2 · π · f · t)
```

con `t` el tiempo simulado, `θ` el ángulo angular del vértice,
`m` el modo y `A` la amplitud. El resultado es una deformación
*wireframe* del rotor/estator que oscila con la frecuencia
calculada por `Modal Frequency`. El motor sigue girando con el
ángulo del `View3DSink` y, si hay un `View3DThermalSink`, también
se tiñe con el color de la temperatura: tres efectos
simultáneos sobre la misma malla.

El `Vulkan3DRenderer` cachea la malla sin deformar al inicializar
(`m_baseMesh`) y, antes de cada *submit*, reescribe el VBO
*host-coherent* con los vértices desplazados. Es el mismo patrón
*hazard-free* que usa para mover el indicador del eje; no hace
falta sincronización adicional porque la GPU lee secuencialmente
en cada *frame*.
