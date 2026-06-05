# Catálogo de nodos

Lista del registry real (`src/core/NodeType.cpp`).  Los
nombres son los que aparecen en el popup `Shift+A` y en el
`.scn`; el "label" entre paréntesis es lo que ves dibujado
en el canvas.

## Sources

Generan señales sin necesitar entrada.

| NodeType        | Label                | Salidas         |
|-----------------|----------------------|------------------|
| `VoltageSource`  | Voltage Source        | V                |
| `CurrentSource`  | Current Source        | A                |
| `StepSignal`     | Step Signal           | escalar (Heaviside) |
| `SineSignal`     | Sine Signal           | escalar          |
| `RampSignal`     | Ramp Signal           | escalar          |
| `DesignTemplate` | Design Template       | 4 puertos (Torque, Speed, Vbus, Cooling) |
| `Vec3Constant`   | Vec3 Constant         | vec3             |
| `Object3D`       | 3D Object             | geometry         |

## Transformers — escalar

Reciben entradas y producen salidas.

| NodeType         | Operación                              |
|------------------|----------------------------------------|
| `Gain`            | y = k · u                               |
| `Summation`       | y = sign1·u1 + sign2·u2                |
| `Integrator`      | y = ∫ u dt                              |
| `Differentiator`  | y = du/dt (con filtro derivativo)      |
| `LowPassFilter`   | filtro pasa-bajos de 1er orden         |
| `PIDController`   | PID con filtro derivativo (`Kp, Ki, Kd, N`) |
| `TransferFunction`| H(s) = numerador / denominador          |
| `TransferFunction2`| variante con coeficientes por puerto  |
| `Saturation`      | y = clamp(u, Min, Max)                 |
| `DegToRad`        | grados → radianes                       |
| `RadToDeg`        | radianes → grados                       |
| `Sin`             | y = sin(u) (u en rad)                   |
| `Cos`             | y = cos(u) (u en rad)                   |
| `Tan`             | y = tan(u) (u en rad)                   |
| `Atan2`           | y = atan2(u₁ = y, u₂ = x) ∈ (−π, π]      |
| `TolerancePerturbator` | inyección de ruido controlado    |

## Transformers — vectorial

| NodeType          | Operación                            |
|-------------------|--------------------------------------|
| `VectorAdd`        | suma componente a componente         |
| `VectorSub`        | resta componente a componente        |
| `VectorScale`      | multiplica por un escalar            |
| `VectorDot`        | producto punto                       |
| `VectorCross`      | producto cruz                        |
| `VectorLength`     | norma                                 |
| `VectorNormalize`  | versor                                |
| `CombineXYZ`       | tres escalares → vec3                |
| `SeparateXYZ`      | vec3 → tres escalares                |

## Transformers — electromecánico / control

| NodeType                | Función                                          |
|-------------------------|--------------------------------------------------|
| `DCMotorModel`           | Motor DC con `Ra, La, Ke, Kt, J, B`             |
| `GearTransmission`       | Reductor con ratio + eficiencia                 |
| `InverseKinematics`      | IK 2R planar: (x, y) → (θ₁, θ₂)                  |
| `ForwardKinematics`      | FK 2R planar: (θ₁, θ₂) → codo (x, y) + punta (x, y) |

## Transformers — diseño de máquinas

| NodeType                  | Función                                         |
|---------------------------|-------------------------------------------------|
| `PMSMSizing`              | Sizing motor síncrono de imanes permanentes     |
| `IPMSizing`               | Sizing IPM (Interior PM)                        |
| `BLDCSizing`              | Sizing motor brushless DC                       |
| `PMSMElectromagnetic`     | Modelo electromagnético del PMSM                |
| `PMSMEfficiency`          | Mapa de eficiencia                              |
| `AirgapFluxDensity`       | Densidad de flujo en el entrehierro             |
| `MaxwellForce`            | Tensión de Maxwell en superficies               |

## Transformers — térmico

| NodeType            | Función                                            |
|---------------------|----------------------------------------------------|
| `ThermalMass`        | Capacidad térmica concentrada                     |
| `ThermalNode`        | Nodo en red térmica resistiva                     |
| `ThermalResistance`  | Resistencia térmica entre dos nodos                |
| `ConvectiveCooling`  | Pérdida por convección                            |
| `CoolingSystem`      | Sistema de enfriamiento compuesto                 |
| `JouleLoss`          | Pérdida resistiva I²R                              |
| `CoreLoss`           | Pérdidas en el núcleo (Steinmetz)                 |
| `MechanicalLoss`     | Pérdidas viscosas + Coulomb                       |

## Transformers — NVH (vibración/acústica)

| NodeType         | Función                                       |
|------------------|-----------------------------------------------|
| `ModalFrequency`  | Frecuencias modales de una placa/eje          |

## Transformers — geometría 3D

| NodeType            | Salida           | Propósito                                    |
|---------------------|------------------|----------------------------------------------|
| `TransformObject`    | geometry         | Aplica rotación / traslación / escala       |

## Sinks (sumideros)

Reciben señales sin producir salidas.

| NodeType                 | Función                                          |
|--------------------------|--------------------------------------------------|
| `Oscilloscope`            | Plot en tiempo real con auto-escala y unidad inferida. |
| `FFTAnalyzer`             | Espectro de magnitud y fase.                    |
| `PhasePortrait`           | Plot 2D de dos señales (x, y).                  |
| `DataLogger`              | Acumula y exporta a CSV.                        |
| `DistributionSink`        | Histograma de distribución de muestras.         |
| `HeatmapSink`             | Heatmap 2D temporal.                             |
| `TerminalDisplay`         | Sumidero "tonto" — muestra el último valor.      |
| `SceneOutput`             | Sumidero del sub-grafo de escena 3D.            |
| `View3DSink`              | Conecta una señal angular al motor 3D procedural. |
| `View3DDeformationSink`   | Mapea desplazamientos a deformaciones en mesh.  |
| `View3DThermalSink`       | Mapea temperaturas a colores en mesh.           |

## Estructurales

Construyen jerarquía sin computar.

| NodeType               | Función                                            |
|------------------------|----------------------------------------------------|
| `SubGraph`              | Contenedor de un sub-grafo.                       |
| `SubGraphInput`         | Stub que materializa una arista entrante.         |
| `SubGraphOutput`        | Stub que materializa una arista saliente.         |
| `Alias`                 | Referencia a otro nodo sin cable visible.  Ver [Alias](alias.md). |
| `Custom`                | Nodo creado en runtime desde JSON.                |

## Crear nodos personalizados

Si necesitás un nodo que no está, **no hay que recompilar**.
Escribilo como JSON y cargalo desde el menú.  Ver
[Personalizar nodos vía JSON](custom-nodes.md).
