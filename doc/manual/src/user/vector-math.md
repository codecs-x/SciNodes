# Vectores 3-D y Vector Math

El catálogo gana tipos vectoriales para construir cinemática
3-D directamente en el grafo.

## Nodos Vec3

- `Vec3Constant` — vector 3-D literal `(x, y, z)`.
- `CombineXYZ` — toma tres escalares y emite un Vec3.
- `SeparateXYZ` — descompone un Vec3 en sus tres canales
  escalares.
- `TransformObject` — recibe un `vec(3)` de traslación y
  un `vec(3)` de rotación y los aplica a su input
  `Object3D`.

## Vector Math

Siete operaciones sobre `vec(3)`:

| Nodo            | Salida                       |
|-----------------|------------------------------|
| `VecAdd`        | `a + b`                      |
| `VecSub`        | `a - b`                      |
| `VecScale`      | `a * k` (con `k` escalar)    |
| `VecDot`        | `a · b` (escalar)            |
| `VecCross`      | `a × b` (vec3)               |
| `VecLength`     | `\|a\|` (escalar)            |
| `VecNormalize`  | `a / \|a\|` (vec3)           |

Los puertos `vec(3)` se distinguen visualmente: tres pines
agrupados con un color distintivo.  La gramática R6 valida
que los tipos de puerto sean compatibles
(no se puede cablear un escalar a un puerto `vec(3)` sin
pasar por `CombineXYZ`).
