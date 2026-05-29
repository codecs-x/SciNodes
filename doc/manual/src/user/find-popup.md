# Buscar nodos: `Shift+B`

`Shift+B` abre un buscador de nodos por nombre.  Substring
*case-insensitive*.  Recursivo a través de todos los
*subgraphs* anidados.

## Qué busca

- El nombre custom del nodo (`stringParams["Name"]`, si lo
  seteaste con **F2**).
- Si no hay nombre custom, el *label* traducido del tipo +
  el id: `Ganancia #7`, `PID #13`.

## El resultado

Cada *hit* aparece como `Top › Lazo control › PID #7` —
un *breadcrumb* del path canónico al subgraph donde vive.
Click sobre el *hit*:

- **C**: centra (pan-only, sin tocar zoom).
- **E**: encuadra (zoom-to-fit el nodo + sus vecinos).
- **Enter**: selecciona el nodo + navega a su subgraph si
  está anidado.

Niveles ilimitados.  Sirve para volver a un nodo concreto
en un grafo grande sin recordar dónde lo metiste.
