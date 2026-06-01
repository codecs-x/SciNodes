# Features de SciNodes — referencia completa

Inventario de toda la funcionalidad expuesta al usuario,
verificada por barrido del código (`src/`).  Si algo del programa
existe y no aparece acá, es un bug del doc.

Para ir directo a la sección que te interesa, usá el TOC del
sidebar de la izquierda.

---

## 1. Edición del grafo

### 1.1 Crear nodos

| Cómo                                              | Resultado                                                                |
|---------------------------------------------------|--------------------------------------------------------------------------|
| <kbd>Shift</kbd>+<kbd>A</kbd>                     | Popup de creación en la posición del cursor.  Categorizado + buscable.   |
| Drag desde un pin OUT al canvas vacío             | Popup con **auto-connect** desde ese pin.  Crea nodo cableado.           |
| Drag desde un pin IN al canvas vacío              | Popup con auto-connect inverso + sección "Nodos en el canvas" con outputs existentes.  Elegir uno crea un **Alias**.  Ver [Aliases](aliases.md). |
| Click-derecho sobre un cable                       | Popup "insertar nodo entre" — el nodo elegido queda cableado entre los dos extremos del cable original. |

El popup busca contra el label traducido del NodeType (`Shift+A`)
o contra el nombre custom + label si se invocó desde un drag
(la sección "Nodos en el canvas" muestra los nombres que cada
nodo tiene en el grafo).

### 1.2 Cablear

| Cómo                                              | Resultado                                                       |
|---------------------------------------------------|-----------------------------------------------------------------|
| Drag desde un pin OUT a un pin IN                  | Crea cable directo.                                              |
| Drag desde un pin OUT al vacío                     | Popup → si elegís un NodeType, lo crea + cablea; si elegís un nodo existente, edge directo al input elegido. |
| Drag desde un pin IN al vacío                      | Popup → si elegís un NodeType, lo crea + cablea; si elegís un nodo existente, crea **Alias**. |
| Click sobre un cable existente y arrastrar         | Detach + reconectar al pin de destino.                          |

R7 (análisis dimensional) se aplica en el momento de cablear —
si las unidades no son compatibles, `tryAddEdge` bloquea la
creación (la arista nunca aparece) y la statusbar muestra
`[R7] Edge dimensional mismatch: <from> → <to>` en rojo.

### 1.3 Selección y movimiento

| Cómo                                              | Resultado                                            |
|---------------------------------------------------|------------------------------------------------------|
| Click sobre un nodo                                | Selecciona ese nodo.                                 |
| Shift+click sobre un nodo                          | Suma a la selección.                                 |
| Click vacío + drag                                  | Rectángulo de selección.                             |
| Drag de selección                                   | Mueve todos los nodos seleccionados con offset uniforme. |
| <kbd>Delete</kbd> o <kbd>Backspace</kbd>            | Borra nodos/cables seleccionados.                    |

### 1.4 Copy / paste / undo / redo

| Atajo                                              | Acción                                            |
|----------------------------------------------------|---------------------------------------------------|
| <kbd>Ctrl</kbd>+<kbd>C</kbd>                        | Copia selección al clipboard interno.            |
| <kbd>Ctrl</kbd>+<kbd>V</kbd>                        | Pega.  Los nodos se reindexan; las aristas internas se preservan. |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>                        | Deshacer.                                          |
| <kbd>Ctrl</kbd>+<kbd>Y</kbd> o <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | Rehacer. |

El `UndoRedoStack` cubre toda mutación estructural del grafo
(add/remove nodes y edges, encapsulación, paste, edición de
parámetros, etc.).

### 1.5 Encapsulación (sub-grafos)

| Atajo / Gesto                                      | Acción                                                            |
|----------------------------------------------------|-------------------------------------------------------------------|
| <kbd>Ctrl</kbd>+<kbd>G</kbd>                        | Encapsula la selección en un `SubGraph`.  Crea stubs `SubGraphInput`/`SubGraphOutput` exactos por cada arista que cruza la frontera. |
| Doble-click en un `SubGraph`                        | Entra al sub-grafo.  Breadcrumb visible.                          |
| <kbd>Esc</kbd>                                      | Sale del sub-grafo activo.                                        |

Anidamiento ilimitado.  Los stubs heredan el `TypeExpr` (escalar,
vec3, geometry) y la unidad del puerto envuelto.

### 1.6 Auto-layout

| Atajo                                              | Acción                                            |
|----------------------------------------------------|---------------------------------------------------|
| <kbd>Ctrl</kbd>+<kbd>L</kbd>                        | Auto-layout BFS-layered del grafo activo.        |

### 1.7 Buscar y navegar

| Atajo                                              | Acción                                            |
|----------------------------------------------------|---------------------------------------------------|
| <kbd>Shift</kbd>+<kbd>B</kbd>                        | Find popup recursivo por nombre.  Atraviesa todos los sub-grafos. |
| <kbd>F</kbd> o <kbd>Home</kbd>                       | Encuadra todo el grafo.                          |
| <kbd>C</kbd> con un nodo seleccionado                | Centra cámara en el nodo (zoom preservado).      |
| <kbd>E</kbd> con un nodo seleccionado                | Encuadra el nodo (zoom adaptativo, máx 1.5×).    |
| Middle-click + drag                                  | Pan del canvas.                                   |
| Scroll                                                | Zoom centrado en cursor (rango 0.1× a 4×).       |

Search del find popup: case-insensitive substring contra
`Name` custom (si fue editado con F2) o el label traducido del
NodeType.

### 1.8 Nombres y comentarios

| Atajo                                              | Acción                                            |
|----------------------------------------------------|---------------------------------------------------|
| <kbd>F2</kbd> con un nodo seleccionado              | Diálogo `Name` + `Comment`.  Ambos persisten en el `.scn`. |

`Name` reemplaza el label del nodo al renderearlo, alimenta el
find popup, y es lo que aparece cuando otro nodo lo elige como
target de Alias.  `Comment` se muestra como punto en la esquina
del nodo + tooltip al hover.

Detalles en [Nombres y comentarios](nodes-metadata.md).

---

## 2. Catálogo de nodos

Resumen por categoría — para detalles ver [Catálogo de nodos](nodes.md).

| Categoría             | Cantidad aprox. | Ejemplos                                     |
|-----------------------|-----------------|----------------------------------------------|
| Source                | 8               | StepSignal, SineSignal, RampSignal, VoltageSource, Vec3Constant, Object3D |
| Transformer escalar   | 12              | Gain, Summation, Integrator, Differentiator, LowPassFilter, PIDController, TransferFunction, Saturation |
| Transformer vectorial | 9               | VectorAdd, VectorCross, VectorLength, CombineXYZ, SeparateXYZ |
| Electromecánico       | 3               | DCMotorModel, GearTransmission, InverseKinematics |
| Diseño de máquinas    | 7               | PMSMSizing, IPMSizing, BLDCSizing, PMSMElectromagnetic, MaxwellForce |
| Térmico               | 8               | ThermalMass, ThermalNode, ThermalResistance, JouleLoss, CoreLoss |
| NVH                   | 1               | ModalFrequency                              |
| Geometría 3D          | 1               | TransformObject                             |
| Sumideros (Sinks)     | 11              | Oscilloscope, FFTAnalyzer, PhasePortrait, DataLogger, DistributionSink, HeatmapSink, View3DSink |
| Estructurales         | 5               | SubGraph, SubGraphInput, SubGraphOutput, Alias, Custom |

Catálogo completo: [Catálogo de nodos](nodes.md).

---

## 3. Análisis dimensional

### 3.1 R7 enforcement

- Cada puerto del grafo declara una `Unit` (vector de 8
  exponentes SI + magnitud).
- `tryAddEdge` rechaza aristas con unidades incompatibles —
  la creación se bloquea y aparece el diagnóstico en statusbar.
- `scinodes::analyzeUnits` corre hasta punto fijo sobre el
  DAG (seed + edge propagation bidireccional +
  unit-transformers + alias + polimorfismo) y deduce las
  unidades implícitas de los puertos no declarados.

### 3.2 QuantityFields (entrada con prefijo)

Dos tipos de field con reglas distintas:

**Fields físicos** (unidad declarada en el registry — ej.
`DCMotorModel.Ra [Ohm]`, `SineSignal.Phase [rad]`):

- Número solo (`120`) → unidad canónica SI del field.
- Número + unidad (`120 deg`, `1.5 mm`, `9.81 m/s^2`) →
  Quantity parseada (si la dimensión calza con la declarada).
- Prefijo SI (`5 mA`, `1.2 GHz`, `2k`) → magnitud aplicada
  sobre la unidad declarada.

**Fields ideales** (coeficientes adimensionales — ej.
`StepSignal.Amplitude`, `Gain.Gain`, `Summation.Sign1`,
`PIDController.N (filter)`):

- Sólo número plano.  No aceptan sufijos textuales.
- La unidad efectiva del valor la determina la propagación
  R7 del cable downstream.
- Para semantizar el coeficiente como grados/revoluciones/etc.,
  insertá un nodo conversor (`DegToRad`, `RadToDeg`) en el
  cable, no en el field.

### 3.3 Override de unidad per-instance

En el panel de parámetros del nodo, cada puerto de
entrada/salida tiene un dropdown con unidades alternativas
compatibles (`rad` ↔ `deg`, `m/s` ↔ `km/h`).  Cambia sólo el
**display**; el cómputo subyacente sigue en SI.

Persistencia: campo `displayUnits` del `.scn`.

### 3.4 Conversores explícitos

- `DegToRad` / `RadToDeg` — para cuando hace falta un cable
  con conversión visible.

### 3.5 Phantom angle dimension

Octava dimensión "fantasma" (no parte del SI estricto) para
detectar errores típicos de control: integrar `rad/s` da `rad`,
multiplicar dos ángulos sin trigonometría queda warning.

Detalles: [Unidades y análisis dimensional](units.md).

---

## 4. Sub-grafos

| Operación                                           | Cómo                                         |
|----------------------------------------------------|----------------------------------------------|
| Crear vacío                                          | <kbd>Shift</kbd>+<kbd>A</kbd> → `SubGraph`.  |
| Encapsular selección                                 | <kbd>Ctrl</kbd>+<kbd>G</kbd>.                |
| Entrar                                                | Doble-click sobre el nodo SubGraph.          |
| Salir                                                 | <kbd>Esc</kbd>.                                |
| Renombrar                                             | <kbd>F2</kbd> sobre el nodo SubGraph.        |

Anidamiento ilimitado.  Find popup recursivo (`Shift+B`) cruza
fronteras.  Codegen aplana los stubs automáticamente — el `.sce`
final no tiene scopes.

Detalles: [Sub-grafos y encapsulación](subgraphs.md).

---

## 5. Aliases

Crear: drag desde pin **IN** → soltar al vacío → popup → sección
"Nodos en el canvas" → elegir → SciNodes crea `NodeType::Alias`
con `target_node_id` + `target_port`.

El Alias se renderea pequeño con el nombre del nodo
referenciado y actúa como atajo visual sin ocupar canvas con
cables largos.

Detalles: [Aliases](aliases.md).

---

## 6. Simulación

### 6.1 Control

Botones en la barra de estado (no hay atajos de teclado para
estos — el botón visible queda explícito en demos/videos):

| Botón     | Función                                                 |
|-----------|---------------------------------------------------------|
| ▶ Run     | Compila grafo a Scilab, arranca solver en tiempo real.  |
| ⏸ Pause   | Congela la simulación preservando `(t, x, buffers)`.   |
| ▶ Resume  | Reanuda desde el `t` actual.  Aplica cambios pendientes vía hot-reload. |
| ⏹ Stop    | Termina la sim.  Estado queda para inspección.         |
| ↺ Reset   | Vuelve a `t = 0` (descarta estado acumulado).          |

### 6.2 Hot-reload

Cambiar un parámetro durante Pause + Resume se aplica al solver
sin reset.  Cambiar la topología del grafo (agregar/quitar
nodos o cables) durante Pause también se acepta al Resume —
SciNodes regenera el plan de simulación con un seed del estado
previo (`CodegenSeedState`).

### 6.3 Backend

| Modo                             | Cómo activarlo                                              |
|----------------------------------|-------------------------------------------------------------|
| Subprocess `scilab-cli` (default) | Sin configuración.                                          |
| In-process `call_scilab`          | Compilar con `-DSCINODES_WITH_CALLAPI=ON`; runtime: `SCINODES_BACKEND=callapi`. |

Detalles arquitectónicos: [IComputeBackend](../architecture/compute-backend.md).

### 6.4 Diagnósticos en runtime

- Detección de `NaN` / `Inf` en cualquier canal: estado pasa a
  Error, nodo culpable se highlight-ea en rojo.
- Error textual en la statusbar (timer ~5 s).

---

## 7. Visualización

### 7.1 Plots embebidos

Cinco renderers en `src/ui/plots/`.  Cada sink-de-plot dibuja
inline en el canvas (no panel aparte):

| Sink              | Renderer            | Salida                                       |
|-------------------|---------------------|----------------------------------------------|
| `Oscilloscope`     | `WaveRenderer`       | Time-series con auto-escala y unidad inferida. |
| `FFTAnalyzer`      | `SpectrumRenderer`   | Magnitud + fase.                              |
| `PhasePortrait`    | `PhaseRenderer`      | Plot 2D (x, y).                              |
| `DistributionSink` | `HistogramRenderer`  | Histograma + estadísticos.                    |
| `HeatmapSink`      | `HeatmapRenderer`    | Heatmap 2D temporal.                          |

Comparten `PlotDefaults.hpp` y `ZoomState.hpp` para zoom + pan
persistente entre frames.

### 7.2 Panel View3D

| Control                              | Acción                                            |
|--------------------------------------|---------------------------------------------------|
| LMB-drag sobre el viewport           | Orbit (azimuth / elevation).                     |
| Scroll                                | Zoom de la cámara 3D (clamped).                  |
| Botones esquina superior              | Modo: **Alámbrico** / **Sólido** / **Ambos**.   |
| LOD automático                        | Si la mesh supera ~30k aristas, decimación 1/N para mantener framerate. |

El panel pinta dos rutas coexistiendo:

- **Legacy**: nodo `Device` con asset glTF cacheado + `View3DSink`.
- **Nuevo**: sub-grafo geométrico con `Object3D` + `TransformObject` + `SceneOutput`.  El `SceneCollector` lo camina cada frame.

Detalles: [Visualización 3D](3d.md) (usuario), [Pipeline 3D](../architecture/view3d.md) (arquitectura).

### 7.3 Status bar

| Zona              | Contenido                                                                 |
|-------------------|---------------------------------------------------------------------------|
| Izquierda         | Botones de simulación (Run/Pause/Resume/Stop/Reset).                      |
| Centro-izquierda  | Badge de estado con color: Editing, Valid, Invalid, Empty, Simulating, Paused, Error. |
| Centro-derecha    | Stats del grafo: node count, edge count, tiempo simulado `t`.              |
| Extrema derecha   | Frame stats (input ms, update ms, render ms, present ms) — toggleable.    |

---

## 8. Outliner — catálogo 3D

Panel dockable que muestra los nodos `Device` del grafo activo
con sus assets 3D asociados.  Cada entrada lista las partes
del asset (mesh / joint / anchor).

Acciones por entrada:

- Click → selecciona el Device en el canvas.
- 🔄 Reload → recarga el `.gltf` desde disco.
- × Unlink → desvincula el asset (el Device queda con geometría
  procedural).

El Outliner **no** es navegador del grafo (eso lo cubre
`Shift+B`).  Detalles: [Outliner](outliner.md).

---

## 9. Catálogo 3D + AssetMapping

| Feature                                  | Cómo                                            |
|------------------------------------------|-------------------------------------------------|
| Importar modelo                          | `Archivo → Importar modelo 3D` → seleccionar `.gltf` o `.glb`. |
| Catálogo del proyecto                    | El asset queda registrado bajo el nombre del archivo; cualquier `Object3D` del grafo lo puede referenciar. |
| Mapeo de partes                          | `AssetMappingPanel` (diálogo modal) — define qué mesh del glTF corresponde a qué rol (shaft, housing, etc.) según el contract del Device. |
| Sidecar `.mapping.json`                  | Persiste el binding fuera del `.scn` para que el mismo asset pueda usarse con mapeos distintos. |
| Geometry contracts                       | Carpeta `contracts/` con descriptores JSON de qué partes espera cada Device.  Ver `doc/designs/geometry-contracts-design.md`. |

---

## 10. Persistencia y export

### 10.1 Formato `.scn`

JSON UTF-8 con `nodes`, `edges`, `subgraphs`, `displayUnits`,
`importedObjects`, `metadata` (id, title, description, tags).
Diff-friendly + editable a mano.  Schema versionado con
migración hacia adelante.

Detalles: [Formato `.scn`](../architecture/file-format.md).

### 10.2 Atajos del menú Archivo

| Atajo                                              | Acción                                            |
|----------------------------------------------------|---------------------------------------------------|
| <kbd>Ctrl</kbd>+<kbd>N</kbd>                        | Nuevo grafo.                                     |
| <kbd>Ctrl</kbd>+<kbd>O</kbd>                        | Abrir `.scn`.                                     |
| <kbd>Ctrl</kbd>+<kbd>S</kbd>                        | Guardar.                                           |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>       | Guardar como.                                     |
| <kbd>Alt</kbd>+<kbd>F4</kbd>                        | Salir.                                             |

### 10.3 Export

Submenú `Archivo → Exportar` (sólo disponible si la simulación
ha producido datos):

| Formato                                  | Salida                                                            |
|------------------------------------------|-------------------------------------------------------------------|
| **CSV wide**                              | Un archivo con columnas `t, sink_1, sink_2, …`.                  |
| **CSV folder**                            | Carpeta con `time.csv` + un `.csv` por sink.                     |
| **SOD (Scilab data)**                     | Archivo binario nativo Scilab (HDF5).  Útil para post-proceso en `scilab-cli`. |

Implementación: `ScilabBridge::exportSod(path)` + `CsvExport`.

### 10.4 Examples browser

`Ayuda → Ejemplos` abre una galería con los `.scn` de
`examples/graphs/`.  Layout en dos columnas con separador
**resizable** (la lista a la izquierda con id+title, el detalle
a la derecha); hover sobre un item muestra un tooltip con el
título completo + id.  Opciones por ejemplo: **Cargar**
(reemplaza el grafo actual) o **Importar** (merge en el grafo
activo).  La descripción del ejemplo cita su referencia
externa cuando aplica (libro o demo Scilab/Xcos).

---

## 11. Extensibilidad

### 11.1 Nodos custom desde JSON

Permite agregar tipos de nodo nuevos sin recompilar.  Ver
[Personalizar nodos vía JSON](custom-nodes.md).

Flujo:

1. Escribir el JSON de definición.
2. `Archivo → Importar` → seleccionar el `.json` del custom node
   (o cargar un `.scn` que lo referencie).
3. El nodo aparece inmediatamente en el popup `Shift+A` bajo
   la categoría declarada.

### 11.2 Custom node registry

Per-sesión.  Los nodos custom cargados se asocian al
`AppWindow`; si abrís un grafo nuevo, los custom nodes del
grafo anterior siguen disponibles para drop-in en el nuevo.

### 11.3 Device contracts

JSON en `contracts/` que describen qué partes de un asset glTF
espera cada Device.  Auto-cargados al inicio + recargables sin
salir del programa.

---

## 12. Internacionalización

| Aspecto                                          | Detalle                                                |
|--------------------------------------------------|--------------------------------------------------------|
| Idioma por defecto                               | `es` (español).                                        |
| Override por env var                             | `SCINODES_LANG=en` (o cualquier otro código presente en `i18n/`). |
| Cambio en runtime                                | `Ver → Idioma` → dropdown dinámico.  No requiere reiniciar. |
| Archivos                                          | `i18n/{lang}.json`.  Hoy hay `es.json` y `en.json`, ambos paritarios (simetría de keys verificada por `test_i18n`). |
| Fallback de claves                                | Si una key no existe en el JSON activo, se muestra la key misma — visible para debugging. |

Detalles: [Internacionalización](../architecture/i18n.md).

---

## 13. Menús completos

### 13.1 Archivo

- Nuevo (<kbd>Ctrl</kbd>+<kbd>N</kbd>)
- Abrir... (<kbd>Ctrl</kbd>+<kbd>O</kbd>)
- Importar...
- Importar modelo 3D...
- Guardar (<kbd>Ctrl</kbd>+<kbd>S</kbd>)
- Guardar como... (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>)
- Guardar como ejemplo...
- Exportar
  - Todos los sinks en un solo CSV
  - Todos los sinks en una carpeta de CSV
  - Datos de simulación (SOD)
- Salir (<kbd>Alt</kbd>+<kbd>F4</kbd>)

### 13.2 Ver

- Reset canvas (vuelve zoom y pan al home).
- Reset layout (restaura el dock layout default).
- Reset simulación (descarta estado, vuelve a t=0).
- Idioma → submenú dinámico con los `.json` de `i18n/`.

### 13.3 Ayuda

- Ejemplos... → abre `ExamplesBrowser`.
- Sobre este grafo... → abre `AboutGraphPanel` con id, title,
  description, tags (editables).

---

## 14. Paneles

| Panel                  | Tipo        | Para qué sirve                                         |
|------------------------|-------------|--------------------------------------------------------|
| `NodeCanvas`           | Dockable    | Editor del grafo (canvas principal).                  |
| `View3DPanel`          | Dockable    | Render 3D del sub-grafo geométrico + Device legacy.   |
| `PlotPanel`            | Dockable    | Plots inline para Sinks visuales.                     |
| `OutlinerPanel`        | Dockable    | Catálogo 3D de Devices.                                |
| `AboutGraphPanel`      | Modal        | Editar metadata del grafo activo.                     |
| `AssetMappingPanel`    | Modal        | Mapear partes del glTF a roles del Device.            |
| `ExamplesBrowser`      | Modal        | Galería de `.scn` en `examples/graphs/`.              |

Todos los dockables tienen layout persistente vía ImGui docking.
El reset del layout vuelve al default.

---

## 15. Diagnósticos y mensajes

| Síntoma visual                              | Significado                                                       |
|---------------------------------------------|-------------------------------------------------------------------|
| Status bar `[R7] Edge dimensional mismatch …` al soltar un cable | R7 bloqueó la creación de la arista; el contador `Aristas` no incrementa. |
| Nodo con marker rojo                         | NaN/Inf detectado durante simulación en este nodo.                |
| Banner "READ-ONLY" sobre el canvas           | El grafo cargado tiene violaciones de gramática duras.            |
| Toast en statusbar (3.5–5 s)                 | Mensaje de error transitorio (encapsulación fallida, sim error, etc.). |
| Badge "Empty" en statusbar                    | Grafo sin nodos.                                                  |
| Badge "Invalid" en statusbar                  | Hay diagnósticos R1–R7 sin resolver — botón Run deshabilitado.    |

---

## 16. Tema visual

- Tema oscuro estilo Blender (24 colores definidos en
  `applyBlenderTheme`).
- Fuente extendida: DejaVu Sans con cobertura griego + flechas
  + operadores matemáticos (necesario para los símbolos de
  unidades y diagramas).
- No hay tema claro alternativo (TODO).

---

## 17. Limitaciones numéricas del solver

Restricciones que vienen del integrador real-time (RK4 a paso
fijo con 16 substeps internos, ver `ScilabCodeGen.cpp:1395`).
**No son bugs**: son las consecuencias prácticas de simular en
tiempo real.

### Frontera de estabilidad — polos < 2670 rad/s

El RK4 a 16 substeps tiene frontera de estabilidad |hλ| < 2.78
con h_sub = (1/60)/16.  Esto da un polo máximo estable de
~2670 rad/s.

Cualquier nodo con dinámica más rápida diverge en simulación —
la sim emite `Inf` durante la corrida.  Casos prácticos:

| Nodo                | Polo efectivo            | Valor seguro                         |
|---------------------|--------------------------|--------------------------------------|
| `DCMotorModel`       | `Ra / La`                | `Ra ≲ 26.7 Ω` con `La = 0.01 H`     |
| `LowPassFilter`     | `Cutoff Freq · 2π`        | `Cutoff Freq ≲ 425 Hz`              |
| `PIDController.N (filter)` | `N` rad/s               | `N ≲ 2670` (default 100 está OK)    |
| `TransferFunction`  | máx \|λ\| de los polos del denom | depende del modelo            |

> Polos más rápidos requieren subir `kDriverRk4Substeps` en
> codegen o aumentar el coeficiente conjugado (subir `La`
> proporcionalmente al `Ra` que quieras representar).  Substeps
> adaptativos al `λ_max` del grafo es trabajo futuro.

### Snap de denormales — valores < 1e-30 → 0

Después de cada step, los componentes del estado con
`|x| < 1e-30` se snapean a `0`.  Sin esto la FPU x86 entra en
modo denormal IEEE 754 cerca del steady-state y los steps
pasan de 2 ms a 200 ms.

Implicación: si tu modelo tiene un estado físico que
legítimamente vale `1e-35` (caso raro), se va a leer como `0`.

### Buffer de historia — realloc a los 4096 samples

`bridge` arranca con buffer de 4096 samples (~68 s a 60 Hz);
después se duplica dinámicamente.  Sesiones largas disparan
reallocaciones que pueden generar lag perceptible.

### Timeouts

| Operación              | Timeout       | Síntoma si lo excede                            |
|------------------------|---------------|-------------------------------------------------|
| Step del solver        | 60 s          | Sim abortada, error en statusbar               |
| Spawn de `scilab-cli`   | 15 s          | Backend reporta error al arrancar              |
| Export (SOD, CSV)      | 10 s          | El usuario ve "Export timed out"               |

### NaN/Inf reporta el primer nodo en orden topológico

Si la sim produce `NaN` o `Inf` en cualquier canal, el bridge
detecta el primer nodo (en orden topológico) cuyo output
explotó y lo marca rojo en el canvas.

**Importante**: el nodo marcado **no siempre es el culpable
real** — un upstream defectuoso puede propagar el `NaN` y el
primer nodo a explotar es el que el orden topológico encontró
primero.  Si tu motor (último Device del grafo) aparece en
rojo pero no encontrás nada mal en sus params, mirá los nodos
upstream antes.

### División por cero protegida

Varios nodos usan offsets de `1e-12` para evitar `div/0`:

- `PMSMEfficiency`: divide por `(Ke + 1e-12)`.
- `JouleLoss`, `CoreLoss`: offsets en denominadores.
- `MaxwellForce`: `B² / (2 µ₀)` con `µ₀ = 4π·1e-7` hardcoded.

Si un parámetro vale exactamente `0`, el offset produce una
amplificación de `1e12` — el resultado va a ser ruido, no `Inf`.
Para diagnosticar: si tu plot tiene magnitudes `~1e10`, hay
un param zero en algún Device.

### Escala del grafo — eficiencia hasta ~256 nodos

`GrammarParser::validateGraph` usa vectores flat-addressed por
`node.id`, optimizados para `maxId < 256`.  Grafos más grandes
funcionan pero la validación se ralentiza.

> Si tu grafo crece más allá de 256 nodos, considerá
> encapsular en SubGraphs — el codegen los aplana, así que no
> hay penalidad de runtime, sólo de validación al editar.

---

## 18. Lo que **no** está implementado todavía

Funcionalidad referida en otros docs como diseño o trabajo
futuro pero que **no** está disponible:

- Modo batch (Xcos-style) — etapa #355.
- Desencapsular SubGraph sin Ctrl+Z (operación existe en `NodeGraph::decoupleSelection` pero no expuesta).
- Geometry contracts evolucionados (post-sustentación, ver `doc/designs/geometry-contracts-design.md`).
- Tema claro.
- Multi-monitor con layouts diferentes.
- Multi-grafo simultáneo (un solo `.scn` activo por vez).

Si encontrás alguno funcionando en tu build, abrí PR para
moverlo de esta sección a la sección correspondiente.
