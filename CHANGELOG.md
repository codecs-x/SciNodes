# Changelog

Historia del repo dividida en 11 *tags* `v0.0.x`.  Para cada
tag se indica el contenido nuevo respecto al anterior.

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
