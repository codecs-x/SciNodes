# Changelog

Historia del repo dividida en 11 *tags* `v0.0.x`.  Para cada
tag se indica el contenido nuevo respecto al anterior.

---

## v0.0.5 — `IComputeBackend`: subproceso + call_scilab in-process

- **`IComputeBackend`** (`src/core/`) — contrato puro de
  seis métodos.  `BackendPrepareSpec` separa autoría de
  ejecución.
- **`ScilabCodeGen::generateSpec(graph)`** — emite la spec
  estructurada para cualquier backend.
- **`ScilabBridge::setBackend(unique_ptr<...>)`** —
  inyección opcional.  Si no se llama, conducta histórica
  intacta (fork + pipe).
- **`ScilabCallApiBackend`** (`src/core/backends/`) — Scilab
  embebido vía `call_scilab`.  *Opt-in* con
  `-DSCINODES_WITH_CALLAPI=ON`.
- **Selector runtime** — `SCINODES_BACKEND=callapi` activa
  el backend in-process.
- **`test_callapi_bridge`** opt-in con el flag CMake.


## v0.0.4 — Consolidación de documentación interna del proyecto

Sin cambios en el código del editor.  Pasada de pulido sobre
los documentos de proyecto (estructura, metodología,
referencias, presentaciones).  Empieza el experimento del
*backend* `call_scilab` que entrará en producción en
v0.0.5.


## v0.0.3 — Catálogo multifísico: PMSM + térmico + structural

- **PMSM analítico.**  `DesignTemplate`, `PMSMSizing`,
  `IPMSizing`, `BLDCSizing`, `PMSMElectromagnetic`,
  `AirgapFluxDensity`, `PMSMEfficiency`, `HeatmapSink`.
  Malla procedural PMSM en el `View3DPanel`.
- **Red térmica.**  `JouleLoss`, `CoreLoss`,
  `MechanicalLoss`, `ThermalMass`, `ThermalNode`,
  `ThermalResistance`, `CoolingSystem`, `ConvectiveCooling`,
  `View3DThermalSink`.
- **Structural / NVH.**  `MaxwellForce`, `ModalFrequency`,
  `TolerancePerturbator`, `DistributionSink`,
  `View3DDeformationSink` con animación modal.
- **Sidecar Python FEM** (`doc/fem_sidecar/`) para
  correcciones de orden superior del PMSM.
- **Per-node Import / Export CSV** en el panel de
  parámetros.
- **Vulkan procedural mesh** con VBO que crece *on demand*.


## v0.0.2 — Custom nodes desde JSON + CSV/.sod export

- **`CustomNodeRegistry`** con descriptores `.json` en
  `doc/custom_nodes/`.  Hook `addRule()` para extender la
  gramática.  Dos ejemplos: `abs_value.json`, `tripler.json`.
- **Popup `Shift+A`** muestra los *custom nodes* en sección
  "Custom".
- **Integración en grammar + codegen** — los custom nodes
  pasan por las mismas R1–R5 que los nativos.
- **`CsvExport`** — botón "Export CSV" por sumidero.
- **Export `.sod` (HDF5)** — *File → Export Simulation Data*
  vía `ScilabBridge::saveSod`.
- **Optimización gramatical** — validación < 1 ms para 256
  nodos.


## v0.0.1 — Plots multi-canal + FFT + plano de fase + 3D Vulkan offscreen

- **`FFTAnalyzer`** — espectro de magnitud con `Fft.{cpp,hpp}`
  (Cooley–Tukey *radix-2*).
- **Sumideros multi-canal.**  `Oscilloscope` y `DataLogger`
  aceptan varias entradas y las superponen.
- **`PhasePortrait`** — trayectoria 2-D \\((x(t), y(t))\\).
- **Panel flotante de parámetros** (doble-click).
- **Realce per-nodo del culpable de NaN.**
- **`Vulkan3DRenderer`** — segundo pipeline Vulkan que
  renderiza *offscreen*, modelo procedural del motor
  (estator + rotor + eje + bobinas), shaders SPIR-V
  embebidos vía `cmake/EmbedSpv.cmake`.
- **`View3DSink`** — sumidero nuevo que aplica la salida
  escalar del solver como ángulo de rotación del eje del
  motor procedural.

---

## v0.0.0 — Bootstrap → integration tests

- **Ventana SDL2 + Vulkan 1.3 + ImGui + `imnodes`.**
- **Popup *Shift+A*** para agregar nodos al canvas.
- **Catálogo cerrado.**  Sources, transformers
  (stateless + stateful), sinks básicos.
- **Visor 3-D** con carga de `.obj`/`.stl` y orbital
  *wireframe*.
- **Gramática R1–R5.**  Validación al cablear.
  `NodeGraph` puro.  `UndoRedoStack` (128 *snapshots*).
- **Persistencia `.scn`.**  JSON determinista con
  `LoadReport`.
- **Codegen Scilab.**  Grafos *stateless* y *stateful*
  (`Integrator`, `LowPassFilter`, `PIDController`,
  `DCMotorModel`, `Differentiator`, `TransferFunction`,
  `TransferFunction2`) integrados con `ode("rk", ...)`.
- **`ScilabBridge`.**  Subproceso `scilab-cli` + hilo
  dedicado del solver + *live tuning*.
- **Lazos cerrados** con *pure-state cycle breaking*.
- **`InverseKinematics`** multi-output (planar 2-link).
- **Suite de integración** con 6 escenarios.
