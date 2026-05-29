# Auto-layout (`Ctrl+L`)

Cuando el grafo se vuelve denso, `Ctrl+L` lo reorganiza
automáticamente.

## El algoritmo

1. **Topological layering** (Kahn) — cada nodo recibe una
   columna (capa) según su distancia desde las fuentes.
2. **ALAP pass para sources** — las fuentes se pegan a la
   columna del primer transformador que las consume.
3. **Y-alignment** — usa la posición del *pin* (no del
   nodo) para alinear nodos vecinos verticalmente.
4. **Equilibrio Newtoniano** — cables modelados como
   resortes; el grafo se relaja a un estado de equilibrio
   estático que minimiza el largo total de los cables.

## Reset cam

Después del layout, `E` encuadra el canvas para mostrar
todo el grafo.  `C` centra (pan-only, sin tocar zoom).
