# Escena 3-D: `Object3D` + `TransformObject`

A partir de v0.0.9 el editor tiene un **sub-lenguaje
Geometry** dentro del mismo canvas: los cables de tipo
`geometry` viven al lado de los de tipo `signal` (escalar) y
`vec(3)`, y forman un sub-grafo que el visor 3-D consume para
componer la escena.

## El catálogo de objetos del proyecto

`Archivo → Importar modelo 3D…` agrega un asset `.gltf` al
catálogo de objetos del proyecto. Los objetos importados
aparecen en el **Outliner** con su jerarquía de partes
(`stator`, `housing`, `shaft`, ...). Cada objeto se persiste
junto al grafo en `.scn 0.5`.

## Los tres nodos del sub-lenguaje

- **`Object 3D`** (Source, 0 in / 1 out) — referencia a un
  objeto del catálogo. Su salida es un cable `geometry` que
  representa el objeto en su pose inicial.
- **`Transform Object`** (Transformer, 4 in / 1 out) — aplica
  una transformación afín a la geometría entrante (puerto 0). Sus
  otras tres entradas son **`vec(3)`**: rotación (Euler XYZ, en
  rad), traslación (m) y escala (adimensional). Cada `vec(3)` se
  arma con un `Combine XYZ` a partir de señales escalares del
  solver. Junto a cada pin verás el **valor actual** del vector en
  tiempo real.
- **`Scene Output`** (Sink, 8 in / 0 out) — sumidero del
  sub-grafo Geometry. Hasta ocho objetos transformados
  alimentan la escena que el `View3DPanel` renderiza.

## El patrón canónico

```
Object3D(stator) ───────────────────────────────────► Scene Output
                                                           ▲
Object3D(shaft) ──► TransformObject ───────────────────────┘
                         ▲ rotación (vec3)
DCMotorModel.ω → Integrator(θ) → CombineXYZ(0, θ, 0) ┘
```

Mientras la simulación corre, el ángulo del eje (un `vec(3)` de
rotación armado con `Combine XYZ` a partir del estado del motor)
entra al puerto de rotación del `TransformObject` y el eje gira en
el visor 3-D. El estator queda quieto; cualquier otra parte
(carcasa, cubierta) sigue el mismo patrón.

## Live-value en los pines

Cada uno de los tres pines `vec(3)` (rotación, traslación, escala)
muestra su valor actual junto al pin —p. ej. `(0, 1.57, 0)`—, dando
feedback inmediato de qué controla la transformación sin tener que
abrir un osciloscopio.

## Persistencia: `.scn 0.5`

El formato del archivo cambia a `scnodes_version: "0.5"` para
acomodar el catálogo de objetos del proyecto (`objects` como
clave de nivel raíz, paralelo a `nodes` y `edges`). Los
objetos llevan referencia al `.gltf` original y la
configuración del binding sidecar (`AssetMapping`).

## El walker headless: `SceneCollector`

Por debajo, el `View3DPanel` no toca el `NodeGraph`
directamente: delega en `SceneCollector`, un walker que parte
de `SceneOutput`, sigue los cables `geometry` hacia atrás y
expande cada `TransformObject` consultando el bridge por los
valores actuales de sus pines de señal. La salida es una lista
de `SceneRenderable`s que el render dibuja en el orden
recibido. Esa separación hace que la escena se pueda construir
*headless* (en tests) sin levantar Vulkan.
