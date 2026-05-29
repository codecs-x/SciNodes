# SciNodes — Documentación técnica

Este sitio es la documentación del editor de nodos en tiempo real
para Scilab.  Es complementario a la **tesis** (`doc/documento final/main.pdf`):

| Documento  | Audiencia              | Qué contiene                              |
|------------|------------------------|--------------------------------------------|
| Tesis PDF  | Estudiante de mecatrónica, jurado académico | El **porqué**: motivación, marco teórico, comparación con Xcos, validación. Analogías al diseño electromecánico. |
| Este sitio | Usuario / desarrollador | El **cómo**: cómo se usa, cómo está hecho, cómo extenderlo. |

## ¿Por qué dos documentos?

El proyecto cubre dos públicos con preguntas distintas:

- Un estudiante que va a defender una tesis sobre exploración
  asistida de modelos físicos quiere leer sobre control,
  motores DC, integradores, no sobre `IComputeBackend` ni
  callbacks de ImGui.
- Un desarrollador que quiere modificar el editor (o un usuario
  avanzado que quiere registrar un nodo nuevo) necesita
  exactamente esos nombres y estructuras.

Un solo documento que sirviera a los dos sería pesado para
ambos. Mantenemos los dos sincronizados a través del
[mapping tesis ↔ docs](architecture/thesis-mapping.md) en los
apéndices.

## Cómo navegar este sitio

El sumario de la izquierda divide el material en dos capas:

- **Capa media — Usar SciNodes**: para personas que quieren
  abrir el programa, armar un grafo y correr una simulación. No
  hace falta saber C++.
- **Capa baja — Arquitectura interna**: para personas que van a
  leer o modificar el código. Asume C++20 y familiaridad con
  patrones de UI inmediata.

La **capa alta** —el porqué del proyecto— vive en la tesis.

## Cómo está hecho este sitio

Las páginas son Markdown editables en `doc/manual/src/`. mdBook las
compila a HTML estático y GitHub Pages las publica.  Para editar
una página, hacé click en el icono ✏️ que aparece arriba a la
derecha — te lleva directo al archivo correspondiente en GitHub.

## Versión

La documentación corresponde a SciNodes **v0.2.0** (mayo 2026).
Si estás leyendo desde un build local más reciente, comparalo
contra el `git log` del repo.
