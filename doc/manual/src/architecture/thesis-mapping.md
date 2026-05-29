# Mapping tesis ↔ docs

Este apéndice es el puente entre los dos documentos del
proyecto.  Para cada sección de la tesis indica qué páginas
de este sitio amplían el detalle técnico.

> La tesis vive en `doc/documento final/main.pdf` y está
> orientada al estudiante de mecatrónica.  Lo que sigue le da
> al lector que quiera más detalle el punto exacto donde
> profundizar.

## Capítulo 6 — Marco Referencial

| Sección de la tesis                          | Página de docs                                |
|----------------------------------------------|------------------------------------------------|
| 6.1 Editores de simulación (Xcos, Simulink)  | (sólo tesis — comparación cualitativa)         |
| 6.2 Grafos como representación                | [Vista de las tres capas](overview.md)         |
| 6.3 Gramáticas formales (motivación)          | [Gramáticas R1–R7](grammar.md)                |
| 6.4 Análisis dimensional                      | [Análisis dimensional](dimensional-analysis.md)|
| 6.5 ImGui (motivación, no internals)          | [NativeNodeRenderer](renderer.md)              |
| 6.6 Vulkan (motivación, no internals)         | [Pipeline 3D](view3d.md)                       |
| 6.7 Scilab + IPC (motivación)                 | [IComputeBackend](compute-backend.md)          |
| 6.8 C++ moderno (motivación)                  | [NodeKind y dispatch polimórfico](node-kinds.md)|

## Capítulo 7 — Metodología

| Sección de la tesis                          | Página de docs                                |
|----------------------------------------------|------------------------------------------------|
| 7.1 TDD por slices                            | [Testing](testing.md)                         |
| 7.2 Versionado semántico                      | (sólo tesis — proceso)                        |

## Capítulo 8 — Diseño de la arquitectura

| Sección de la tesis                          | Página de docs                                |
|----------------------------------------------|------------------------------------------------|
| 8.1 Capa modelo / capa UI / capa backend       | [Vista de las tres capas](overview.md)         |
| 8.2 Gramáticas R1–R5 (originales)             | [Gramáticas R1–R7](grammar.md)                |
| 8.3 Validación contra Xcos                    | `doc/test_manual/xcos_comparison/README.md`   |
| 8.4 Generación de código `.sce`                | [Codegen](codegen.md)                         |
| 8.5 IPC con Scilab                             | [IComputeBackend](compute-backend.md)         |
| 8.6 Persistencia (`.scn` JSON)                  | [Formato `.scn`](file-format.md)              |

## Capítulo 9 — Implementación

| Sección de la tesis                          | Página de docs                                |
|----------------------------------------------|------------------------------------------------|
| 9.1 Stack tecnológico (CMake, FetchContent)   | [Instalación](../user/install.md)              |
| 9.2 Servicios de capa app                     | [Vista de las tres capas](overview.md)         |
| 9.3 IComputeBackend vs `call_scilab`           | [IComputeBackend](compute-backend.md)         |
| 9.4 NativeNodeRenderer (motivación + decisión)| [NativeNodeRenderer](renderer.md)              |
| 9.5 SubGraph flatten algorithm                 | [Codegen](codegen.md)                         |
| 9.6 Pipeline 3D (Vulkan + SceneCollector)      | [Pipeline 3D](view3d.md)                       |
| 9.7 Live tuning + hot-reload                   | [IComputeBackend](compute-backend.md)         |
| 9.8 Librerías internas                          | [Librerías internas](libraries.md)             |
| 9.9 Internacionalización                       | [i18n](i18n.md)                                |

## Capítulo 10 — Validación

| Sección de la tesis                          | Página de docs                                |
|----------------------------------------------|------------------------------------------------|
| 10.1 Tests unitarios + integración            | [Testing](testing.md)                         |
| 10.2 Comparación con Xcos                     | `doc/test_manual/xcos_comparison/README.md`   |
| 10.3 Casos de estudio (lazo PID, energía)     | (sólo tesis)                                  |

## Capítulo 11 — Extensiones multifísicas

| Sección de la tesis                          | Página de docs                                |
|----------------------------------------------|------------------------------------------------|
| 11.* Nodos PMSM, térmica, NVH                 | [Personalizar nodos](../user/custom-nodes.md) |
|                                                | + scripts Scilab generados (sólo tesis)       |

## Capítulo 13 — Trabajo futuro

| Sección de la tesis                          | Página de docs                                |
|----------------------------------------------|------------------------------------------------|
| 13.1 Backends alternativos (SUNDIALS, FMI)    | [IComputeBackend — Backends alternativos](compute-backend.md#backends-alternativos-post-v02) |
| 13.2 Modo batch (Xcos-style)                  | Etapa 7 (task interna)                        |
| 13.3 Geometry contracts                       | `doc/designs/geometry-contracts-design.md`            |
