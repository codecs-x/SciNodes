# Lanzar el editor

Al ejecutar `./build/SciNodes` se abre la ventana principal:

- **Canvas central (`Node Editor`)** — donde se construye el
  grafo.
- **Visor 3-D (`3D View`)** — carga `.obj` o `.stl` con
  cámara orbital *wireframe*.
- **Plots** — uno por cada sumidero conectado.
- **Barra de estado abajo** — botones Run / Pause / Resume /
  Stop / Reset, indicador de validez de gramática, contador
  de nodos y aristas, tiempo simulado.

## Agregar nodos

Pulsa **`Shift+A`** dentro del canvas para abrir el popup de
nuevo nodo.  El catálogo está organizado en **Fuentes**
(sources), **Transformadores** (transformers) y **Sumideros**
(sinks).

## Atajos

| Atajo            | Acción                                |
|------------------|---------------------------------------|
| `Shift+A`        | Popup "Agregar nodo"                  |
| `Del`            | Eliminar selección                    |
| `Ctrl+Z` / `Y`   | Undo / Redo                           |
| `Ctrl+S`         | Save (Save As si nunca se guardó)     |
| `Ctrl+Shift+S`   | Save As                               |
| `Ctrl+O`         | Open                                  |
| `Ctrl+N`         | New                                   |
| `Alt+F4`         | Quit                                  |
