# SubGraph: encapsular partes del grafo

Cuando un grafo crece, conviene agrupar conjuntos de nodos
en cajas reusables.  `SubGraph` es esa unidad.

## Crear un SubGraph

1. Selecciona los nodos a encapsular (drag + box-select o
   `Ctrl+click`).
2. **`Ctrl+G`** — los nodos se reemplazan por un único
   nodo `SubGraph` con sus *stubs* de entrada/salida
   internamente conectados.

## Entrar / Salir

- **Doble-click** en el `SubGraph` para entrar a su grafo
  interno.
- **Esc** o el *breadcrumb* arriba del canvas para volver al
  nivel superior.

## Recursividad

Un `SubGraph` puede contener otros `SubGraph`.  La
profundidad es ilimitada.  La gramática R1–R5 se aplica a
cada nivel: un *subgraph* es un grafo.

## Hot-reload Pause → Edit → Resume

Mientras la simulación está en `Pause`, podés editar el
grafo (incluyendo dentro de subgraphs) y `Resume` preserva
\\((t, x)\\) — el solver continúa desde donde estaba con el
grafo modificado.  Útil para sintonía exploratoria sin
perder el contexto de la corrida.
