# Changelog

Cada *tag* indica el contenido nuevo respecto al anterior.

---

## v0.0.5 — Cadena térmica + colormap 3-D

El editor cierra el ciclo termo-eléctrico del actuador. Nueve
nodos nuevos cubren las tres fuentes de pérdida (Joule, núcleo,
mecánica), la red térmica RC arbitraria (masa, junta
multi-entrada, resistencia bidireccional) y el sistema de
refrigeración (caudal de aire/agua + convección forzada). Un
nuevo sumidero `View3DThermalSink` mapea la temperatura del
motor a un colormap azul→rojo aplicado al PMSM procedural del
visor 3-D. Una corrida de balance energético de 60 s acepta
drift menor al 1 %.

### Nuevos nodos (9)

- **`JouleLoss`** (Transformer, 2 in / 1 out, 1 param) —
  pérdidas óhmicas P = R · i².
- **`CoreLoss`** (Transformer, 2 in / 1 out, 3 params) —
  Steinmetz simplificado: histéresis (∝ ω · B²) + corrientes
  de Eddy (∝ ω² · B²).
- **`MechanicalLoss`** (Transformer, 1 in / 1 out, 2 params) —
  fricción viscosa (∝ ω) + arrastre aerodinámico (∝ ω²).
- **`ThermalMass`** (Transformer stateful, 1 in / 1 out,
  3 params) — RC simplificado (capacitancia + resistencia al
  ambiente).
- **`ThermalNode`** (Transformer stateful, 4 in / 1 out,
  2 params) — junta multi-entrada con su propia capacitancia.
- **`ThermalResistance`** (Transformer, 2 in / 2 out, 1 param)
  — conductancia bidireccional entre dos nodos térmicos.
- **`CoolingSystem`** (Source, 0 in / 3 out, 3 params) —
  caudal de aire, caudal de agua, temperatura ambiente.
- **`ConvectiveCooling`** (Transformer, 3 in / 2 out, 2 params)
  — coeficiente convectivo dependiente del flujo
  (`h(flow) = h_0 + slope · flow`).
- **`View3DThermalSink`** (Sink, 1 in / 0 out, 2 params) —
  mapea su entrada (temperatura) a un colormap azul→rojo
  aplicado al PMSM procedural del visor 3-D.

### Colormap térmico en el visor 3-D

- **`View3DPanel`** ahora consulta dos sumideros: si hay
  `View3DSink` lo usa para el ángulo del eje (como antes); si
  hay `View3DThermalSink` lo usa para colorear la malla.
  Ambos coexisten; el motor se ve girando y calentándose
  simultáneamente.
- **Interpolación HSV** entre `Cold Temperature` (azul, 290 K
  por defecto) y `Hot Temperature` (rojo, 390 K).

### Catálogo

- Built-in: **40 tipos** (vs 31 del *tag* anterior).

### Tests

- `test_grammar`: **378 aserciones en runtime** (vs 339 del
  *tag* anterior).
- `test_integration`: **309 aserciones en runtime** (vs 290)
  en **28 escenarios**. Nuevos (24-28):
  24. STAGE v0.9 — `Step(100 W) → ThermalMass(C=1000, R=0.01) → Scope`
  verifica τ = R·C = 1 s.
  25. STAGE v0.9 — `JouleLoss` con I deducido de torque/Ke:
  P = R·i² = 75 W.
  26. STAGE v0.9 — `ThermalMass → View3DThermalSink` (canal del
  bridge listo para el colormap del visor).
  27. STAGE v0.9 — 2-node thermal chain (bobinado +
  carcasa) llega a estado estacionario.
  28. STAGE v0.9 — Energy balance 60 s con `CoolingSystem +
  ConvectiveCooling`; drift < 1 %.

---

## v0.0.4 — Catálogo multifísico: PMSM analítico + procedural + sweep

El editor gana un catálogo electromagnético cerrado para el
actuador del dominio mecatrónico. Ocho nodos nuevos cubren el
ciclo de diseño analítico de un PMSM, IPM o BLDC: punto de
operación, sizing, modelo lumped, densidad de flujo, eficiencia,
mapa de operating point. El visor 3-D gana una malla procedural
del rotor/estator que crece con la cuenta de slots y polos.

### Nuevos nodos (8)

- **`DesignTemplate`** (Source, 0 in / 4 out) — punto de
  diseño: torque objetivo, velocidad, voltaje de bus, clase
  térmica.
- **`PMSMSizing`**, **`IPMSizing`**, **`BLDCSizing`**
  (Transformer, 2 in / 3 out) — variantes analíticas del
  dimensionamiento; entregan diámetro de stator, longitud
  axial y dimensión de slot.
- **`PMSMElectromagnetic`** (3 in / 4 out) — modelo lumped:
  back-EMF, inductancia, voltaje RMS y *cogging-torque*.
- **`AirgapFluxDensity`** (1 in / 1 out) — serie temporal
  B(θ) con fundamental + 3er armónico + armónico de ranura.
- **`PMSMEfficiency`** (3 in / 1 out) — η = P_out / P_in con
  pérdidas Joule, hierro y mecánicas.
- **`HeatmapSink`** (Sink, 3 in / 0 out) — sumidero 2-D con
  el mapa de operating point.

### Procedural mesh y CSV per-nodo

- **`Vulkan3DRenderer`** soporta VBO que crece *on demand*
  para acomodar geometrías PMSM más grandes (slot/pole counts
  variables) sin reinicializar el pipeline.
- **`View3DPanel`** reemplaza la malla genérica del motor por
  un PMSM procedural cuando hay un nodo de sizing en el grafo.
- **`src/core/CsvParamIO.{cpp,hpp}`** — Import / Export CSV
  per-nodo en el panel de parámetros (distinto del CSV
  per-sink del v0.0.3, que serializa el ring buffer; este
  persiste los valores de los parámetros del nodo).

### Sidecar opcional

- **`doc/fem_sidecar/`** — script Python independiente
  (`pmsm_lumped_corrections.py`) con correcciones de orden
  superior al modelo lumped. El editor no lo lanza; queda como
  herramienta externa.

### Catálogo

- Built-in: **31 tipos** (vs 23 del *tag* anterior).

### Tests

- `test_grammar`: **339 aserciones en runtime** (vs 257 del
  *tag* anterior).
- `test_integration`: **290 aserciones en runtime** (vs 259)
  en **23 escenarios**. Nuevos (19-23):
  19. STAGE v0.8 — `DesignTemplate → PMSMSizing → 3× Scope`.
  20. STAGE v0.8 — `PMSMElectromagnetic` Ke / L / Vrms / Tcog.
  21. STAGE v0.8 — `Step(ω=1) → AirgapFluxDensity → Scope ⇒ -0.727`.
  22. STAGE v0.8 — `PMSMEfficiency + HeatmapSink (T, ω → η)`.
  23. STAGE v0.8 — `IPMSizing + BLDCSizing` closed-form D check.

---

## v0.0.3 — Custom nodes desde JSON + export CSV/.sod

El catálogo deja de ser un set cerrado. Un `CustomNodeRegistry`
carga descriptores JSON desde `doc/custom_nodes/` al arrancar y
los integra en la paleta, la gramática y el codegen. El usuario
puede definir transformadores nuevos —con expresión Scilab
embebida y parámetros— sin recompilar el editor. La capa de
exportación entrega dos formatos: CSV por sumidero desde el
`DataLogger`, y `.sod` (HDF5 nativo de Scilab) de toda la
corrida desde **File → Export Simulation Data**.

### Extensibilidad

- **`src/core/CustomNodeRegistry.{cpp,hpp}`** — singleton que
  parsea descriptores JSON y los expone vía
  `find(typeId)` / `typeIds()`. `CustomNodeDef` con
  `typeId`, `label`, `description`, `category`, port counts,
  `params` (con default y unidad) y `expression` (plantilla
  Scilab con placeholders `u1`, `u2`, …, `p_<name>`).
- **Descriptores de ejemplo:**
  `doc/custom_nodes/abs_value.json` (`|u1|`) y
  `doc/custom_nodes/tripler.json` (`3 * p_k * u1`).
- **Integración con gramática + codegen.** Las reglas R0–R5
  validan custom nodes igual que built-ins; `ScilabCodeGen`
  sustituye la `expression` antes de emitir el script.
- **Popup `Shift+A`** muestra los custom en una sección
  *Custom* aparte del catálogo built-in.

### Exportación

- **`src/core/CsvExport.{cpp,hpp}`** — `writeSinkCsv(path,
  buf, wIdx, latestTime, dt, nodeLabel)` escribe el *ring
  buffer* de un sumidero a CSV en orden cronológico.
- **Botón Export CSV** en cada `DataLogger` del PlotPanel.
- **`ScilabBridge::exportSod(path)`** — toda la corrida a
  archivo `.sod` (HDF5 nativo de Scilab). En modo con hilo
  dedicado del solver, la exportación se encola para evitar
  carreras; en modo síncrono es inmediata.
- **Menú `File → Export Simulation Data (SOD)…`**.

### Rendimiento

- **Validación de gramática < 1 ms** para un grafo de 256
  nodos. Cubierto por una micro-prueba de regresión en
  `test_grammar`.

### Tests

- `test_grammar`: **257 aserciones en runtime** (vs 192 del
  *tag* anterior). Las nuevas cubren custom nodes
  (registro/lookup/clash con built-ins), grammar perf y
  alcanzabilidad con custom nodes.
- `test_integration`: **259 aserciones en runtime** (vs 234)
  en **18 escenarios**. Nuevos:
  16. STAGE v0.7 — lazo cerrado PID + DC motor durante 10 s a
  tolerancia 1 %.
  17. CUSTOM NODE — `Step → Custom("Tripler", k=2) → Scope`
  verifica que el codegen sustituye la expresión y la salida
  es 3·k·u1 = 6.
  18. .sod EXPORT — `Sine → Gain → Scope`; al final llama a
  `exportSod` y verifica que el archivo existe y tiene
  tamaño > 0.

---

## v0.0.2 — Render Vulkan offscreen + motor procedural acoplado al solver

El visor 3-D deja de ser un inspector geométrico aislado y se
acopla al solver. Un segundo *pipeline* Vulkan dibuja
*offscreen* el motor procedural; el nuevo sumidero
`3D View Sink` consume cualquier rama del grafo y la usa como
ángulo del eje en pantalla.

### Visor 3-D y *renderer*

- **`src/app/Vulkan3DRenderer.{cpp,hpp}`** (~720 LOC) — segundo
  *pipeline* Vulkan independiente del principal de la UI.
  Renderiza *offscreen* a una textura ImGui que se dibuja con
  `ImGui::Image`. API: `init`, `shutdown`, `resize`, `render`,
  `imguiTextureId`, `ready`.
- **Modelo procedural** del motor (estator + rotor + eje +
  bobinas) dibujado por *shaders* propios. No requiere assets
  externos en *runtime*.
- **`src/shaders/motor.vert` y `motor.frag`** + nuevo módulo
  CMake **`cmake/EmbedSpv.cmake`** — los *shaders* se compilan
  a SPIR-V durante el *build* y se embeben en el binario como
  arreglos C++.
- **`View3DPanel` acoplado al solver**: cada *frame* consulta
  al `ScilabBridge` por la última muestra del `View3DSink` del
  grafo y la pasa como `shaftAngle` al `Vulkan3DRenderer`. Si
  no hay `View3DSink` cableado, el eje gira a 1 Hz como
  demostración estática.
- **Fallback robusto**: si `Vulkan3DRenderer::init` falla, el
  panel cae a un estado en blanco; el resto del editor sigue
  funcionando.

### Catálogo

- **Nuevo:** `View3DSink` (sumidero, 1 in, 0 out, sin
  parámetros) — drives the shaft angle of the procedural motor.
- Catálogo total: **23 tipos**.

### Tests

- `test_grammar`: **192 aserciones en runtime** (vs 186 del
  *tag* anterior).
- `test_integration`: **234 aserciones en runtime** (vs 171)
  en **15 escenarios**. Escenario 15 nuevo:
  `Sine → View3DSink` — verifica que el bridge trata al
  sumidero como uno de un canal, asigna *slot* y los buffers
  se llenan correctamente.

---

## v0.0.1 — Espectro, multi-output, panel flotante + hilo del solver

El editor gana visualización seria. El `FFT Analyzer` pasa de
mostrar la forma de onda a calcular el espectro real con una FFT
propia; `InverseKinematics` se convierte en nodo multi-output con
dos salidas reales; un panel flotante de parámetros aparece al
doble click; el codegen estrena tres nuevos bloques con estado
(`Differentiator` filtrado, `Transfer Function` de 1er orden y
`Transfer Function (2nd)` de 2do orden); y la simulación corre en
un hilo dedicado del solver, separada del *frame loop* de la UI.

### Visualización

- **FFT real** en `src/core/Fft.{cpp,hpp}` — radix-2 Cooley-Tukey
  + `magnitudeSpectrum(samples, n)`. El `FFT Analyzer` lo usa vía
  `PlotPanel::renderSpectrum` sobre la ventana más reciente del
  *ring buffer*, recortada a la mayor potencia de dos no mayor
  que el parámetro `Bin Count`.
- **Buffers multi-canal por sumidero.** `ScilabBridge` expone
  `channelCount(nodeId)` y `buffer(nodeId, channel)`. `Phase
  Portrait` los usa para dibujar (`x(t)`, `dx/dt(t)`) como
  trayectoria 2-D.
- **Panel flotante de parámetros.** Doble click sobre el cuerpo
  de un nodo abre una ventana ImGui con los mismos `DragFloat`
  que aparecen inline; coexisten editando el mismo estado.
- **Per-node NaN highlight.** Cuando una variable diverge a `NaN`
  o `Inf`, `ScilabBridge` identifica el nodo culpable y el canvas
  lo pinta en rojo.

### Catálogo

- **Nuevo:** `TransferFunction2` — segundo orden monico,
  `H(s) = (b1·s + b0) / (s² + a1·s + a0)`, aporta dos *slots* al
  vector de estado.
- **`InverseKinematics`** ahora **2 entradas, 2 salidas**: target
  `(x, y)` → ángulos `(θ1, θ2)` con la solución cerrada del IK
  planar *elbow-up*; objetivos fuera del *workspace* se recortan.
- **`Differentiator`** cambia a derivada filtrada
  `H(s) = s / (1 + s/wc)`, integrada vía `ode`.
- **`TransferFunction`** (1er orden) ahora se emite vía `ode`
  como sistema con estado.

### Solver y *bridge*

- **Hilo dedicado del solver.** `ScilabBridge::startSolverThread(dt)`
  arranca un hilo que entra en bucle de `step(dt)` con cadencia
  controlada por `std::chrono::steady_clock`. La UI no llama a
  `step()` mientras el hilo está activo; consume las muestras
  leyendo los *ring buffers* directamente.
- **Buffers SPSC** entre el hilo del solver (productor único) y
  el *frame loop* del editor (consumidor único). Sin *mutex* en
  el camino caliente.

### Tests

- `test_grammar`: **186 aserciones en runtime** (vs 117 del tag
  anterior) — 159 invocaciones textuales de `EXPECT_*`, varias
  dentro de loops sobre el catálogo.
- `test_integration`: **171 aserciones en runtime** (vs 17 del
  tag anterior) repartidas en **14 escenarios**:
  1–6. Originales (Stateless, Stateful, Coupled, Feedback, Live
  tune, CLOSED LOOP).
  7. Differentiator (`Ramp(slope=2) → Differentiator → Scope`).
  8. TransferFunction de 1er orden (`Step → 1/(s+1) → Scope`).
  9. Real 2-link IK (`(x, y) → IK → (θ1, θ2) → 2× Scope`).
  10. TF2 (`Step → 1/(s²+1) → Scope`, oscilación no amortiguada).
  11. Solver thread (`Sine → Gain → Scope` con
  `startSolverThread` corriendo en background).
  12. NaN highlight (TF con polo en `+1000` diverge; bridge
  identifica el TF culpable).
  13. FFT pipeline (`Sine(3.75 Hz) → FFTAnalyzer(Bin=64)`; pico
  en el bin correcto).
  14. Phase portrait (dos `Sine` en cuadratura llenan canal 0 y 1
  del `PhasePortrait`).

---

## v0.0.0 — Bootstrap → suite de integración

Primer estado utilizable del editor. El usuario puede construir un
grafo, validarlo con la gramática, persistirlo, lanzarlo contra
Scilab y observar la simulación en tiempo real.

### Editor

- **Ventana SDL2 + Vulkan 1.3 + Dear ImGui (rama `docking`) +
  imnodes.** Canvas zoomable con `Ctrl + rueda`; popup `Shift+A`
  para insertar nodos.
- **Barra de menús** con tres menús: `File` (New, Open…, Save,
  Save As…, Quit), `View` (Reset Canvas, Reset Layout, Reset
  Simulation) y `Help` (About SciNodes, *placeholder*).
- **Barra de estado** con cinco botones de control de simulación
  (▶ Run, ⏸ Pause, ■ Stop, ▶ Resume, ↺ Reset), badge del estado
  del solver, mensaje de la última regla rota y reloj de tiempo
  simulado.
- **Parámetros editables inline** en el cuerpo del nodo
  (`DragFloat` con *drag* lateral o doble click para teclear el
  valor exacto).

### Catálogo

- **21 tipos** divididos en tres categorías:
  - 5 fuentes: `VoltageSource`, `CurrentSource`, `StepSignal`,
    `SineSignal`, `RampSignal`.
  - 11 transformadores: `Gain`, `Summation`, `Integrator`,
    `Differentiator`, `LowPassFilter`, `PIDController`,
    `TransferFunction`, `Saturation`, `DCMotorModel`,
    `GearTransmission`, `InverseKinematics`.
  - 5 sumideros: `Oscilloscope`, `FFTAnalyzer`, `PhasePortrait`,
    `DataLogger`, `TerminalDisplay`.
- **`Summation`** acepta 2 entradas con signos configurables.
  **`PhasePortrait`** acepta 2 entradas (`x(t)`, `dx/dt(t)`).
  El resto es 1-a-1.
- **`InverseKinematics`** (FK planar de 2 enlaces) expone un
  puerto de entrada y uno de salida; la versión multi-salida
  llega en *tags* posteriores.

### Modelo + gramática

- **`NodeGraph`** con `addNode`, `removeNode`, `tryAddEdge`
  (consulta a la gramática), `removeEdge`, `setParam`.
  `NodeInstance` guarda `id`, `type` y `params` como
  `unordered_map<string, double>`; la posición visual la
  gestiona el canvas vía un `ScnPositions` separado.
- **`UndoRedoStack`** con capacidad `MAX_DEPTH = 50`. El canvas
  empuja un *snapshot* por cada transacción del usuario.
- **Gramática R0–R5.** `GrammarParser::validateEdge` rechaza
  conexiones que violen alguna regla (categoría compatible,
  puertos disponibles, no auto-conexión, no duplicados, puerto
  destino libre). `reachable()` hace BFS desde fuentes para
  avisar grafos sin sumidero accesible.

### Codegen y solver

- **`ScilabCodeGen`** emite código Scilab para grafos
  *stateless* (función `y = run(t)`) y *stateful* (función
  `dxdt(t, x)` + integración con `ode("rk", x, t_prev, t,
  dxdt)`). Los nodos con estado (`Integrator`, `Differentiator`,
  `LowPassFilter`, `PIDController`, `TransferFunction`,
  `DCMotorModel`) aportan uno o más *slots* al vector de estado
  del grafo.
- **Lazos cerrados** vía *pure-state cycle breaking*: el codegen
  identifica un nodo con estado dentro del ciclo y lo usa como
  punto de ruptura; la salida del nodo se calcula a partir del
  estado del paso anterior. Ciclos puramente combinacionales se
  rechazan en codegen.
- **`ScilabBridge`.** Subproceso `scilab-cli` con banderas
  `-nb -nwni -noatomsautoload`. API: `reset(graph)`, `step(dt)`,
  `sendParameter(...)`, `stop()`, `status()`, `time()`,
  `lastError()`. *Ring buffer* de 512 muestras por sumidero.
- **Live tuning** sin reiniciar la simulación, vía
  `sendParameter` (*fire-and-forget*).

### Persistencia y formato

- **Formato `.scn`** — JSON con `scnodes_version "0.3"`,
  `next_node_id`, `nodes` y `edges`. Cada nodo lleva `id`,
  `type`, `position` (`[x, y]`) y `params` (objeto
  `nombre → valor`). Cada arista lleva `id`, `from_node`,
  `to_node` y `to_port`.
- **`LoadReport`** acumula tipos desconocidos, aristas rechazadas
  por la gramática y mismatch de versión; el editor abre el grafo
  en modo de sólo lectura cuando hay errores.

### Visor 3-D

- Panel auxiliar con botón **Browse** para cargar `.obj` o
  `.stl`, render *wireframe* con cámara orbital. Desacoplado del
  solucionador en esta versión.

### Tests

- `test_grammar`: 117 aserciones (`EXPECT_TRUE`, `EXPECT_FALSE`,
  `EXPECT_VALID`, `EXPECT_INVALID`, `EXPECT_RULE`) sobre R0–R5,
  alcanzabilidad y ciclo undo/redo. Corre en milisegundos sin
  Scilab.
- `test_integration`: 17 aserciones (`EXPECT_TRUE` y
  `EXPECT_NEAR`) repartidas en seis escenarios *end-to-end* que
  lanzan `scilab-cli` real:
  1. Stateless (`Sine → Gain → Scope`)
  2. Stateful (`Step → Integrator → Scope`)
  3. Coupled (`Voltage → DCMotor → Scope`)
  4. Feedback (`Step → Sum(+,−) → Integrator → ↺`)
  5. Live tune (`Sine → Gain → Scope`, con `sendParameter(K)`)
  6. CLOSED LOOP (`Step → Sum → PID → DCMotor → Scope ↺`)
