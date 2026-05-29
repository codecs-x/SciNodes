# Changelog

Cada *tag* indica el contenido nuevo respecto al anterior.

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
