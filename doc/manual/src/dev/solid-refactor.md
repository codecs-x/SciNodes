# Refactor SOLID por fases

El binario evoluciona de un `AppWindow` monolítico a una
arquitectura por capas donde cada responsabilidad vive en
su propia clase con dependencias inyectadas.

## Capa A — Use cases extraídos de `AppWindow`

| Clase                | Responsabilidad                              |
|----------------------|----------------------------------------------|
| `WorkspaceManager`   | Layouts pre-configurados (3 workspaces).     |
| `SimController`      | Run / Pause / Resume / Stop / Reset.         |
| `FileActions`        | New / Open / Save / SaveAs / ExportSod.      |
| `ShortcutHandler`    | Atajos globales (`Ctrl+S`, etc.).            |
| `IPanel` / `Area` / `PanelRegistry` | Sistema de paneles via Strategy. |

`AppWindow` queda con: ciclo de vida del proceso, *frame
loop*, *popups* modales, *ownership* de los objetos
compuestos.

## Capa B — Dependency Inversion en los paneles

`IPanelContext` con `graph()` y `bridge()` (read-only).
`PanelContext final` envuelve referencias a los objetos
reales que `AppWindow` posee.  Los paneles
(`PlotPanel`, `View3DPanel`) reciben `const
IPanelContext&` en `draw()`.

Plot renderers (`renderWave`, `renderSpectrum`,
`renderPhase`) se separan a `src/ui/plots/` con su propio
header común para constantes de color.

## Capa C — Service Locators con inyección opcional

| Locator                  | Quién lo consume         | Cómo se inyecta                        |
|--------------------------|--------------------------|----------------------------------------|
| `ContractRegistry`       | `NodeCanvas`, `AppWindow` | DI por referencia (Phase C.7a)         |
| `CustomNodeRegistry`     | `ScilabCodeGen`          | Parámetro opcional `const I*` (C.7b)   |
| `AssetService`           | `View3DPanel`            | Facade inyectado (Phase C.8)           |

El patrón es **Service Locator con punto de fuga
inyectable**: el binario tiene un *singleton* default; un
test o un binario alternativo inyecta su propia
implementación.
