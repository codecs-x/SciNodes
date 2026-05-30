# SubGraph: composición jerárquica

A partir de v0.0.7 el editor deja de tener un solo nivel: un
nodo del canvas puede envolver otro `NodeGraph` completo. Eso
permite tomar un fragmento del grafo, guardarlo como una caja
con puertos, reusarlo en otros lugares, y leer el modelo
top-level a una resolución más legible.

## Encapsular: `Ctrl+G`

La operación canónica es **encapsular un conjunto de nodos
seleccionados**. Selecciona los nodos con un *rubber band* o
con `Shift+click`, pulsa `Ctrl+G`, y el editor reemplaza la
selección por un solo nodo `SubGraph` con sus puertos
deducidos automáticamente:

- Cada cable que **entraba** al conjunto desde fuera se
  convierte en un puerto de entrada del SubGraph, materializado
  internamente como un `SubGraphInput` (Source).
- Cada cable que **salía** del conjunto hacia fuera se
  convierte en un puerto de salida del SubGraph, materializado
  internamente como un `SubGraphOutput` (Sink).
- El estado de la simulación se preserva: si el grafo estaba
  corriendo, encapsular no reinicia el solver. El plan
  reconstruido sigue siendo equivalente.

Entrar al SubGraph se hace con **doble click** sobre su
cuerpo. La barra superior muestra el *breadcrumb* de
contención (`root / Lazo PID / Compensador`); para volver al
nivel anterior, click en el padre del breadcrumb.

## Aplanar: `Ctrl+Shift+G`

La operación inversa expande el contenido del SubGraph en su
contexto: los `SubGraphInput`/`SubGraphOutput` se reemplazan
por los cables externos correspondientes y el `SubGraph`
desaparece. Útil para iterar entre vista compacta y vista
explícita.

## Persistencia: `.scn 0.4`

A partir de esta versión el formato del archivo cambia de
`scnodes_version: "0.3"` a `"0.4"`. La diferencia: cada
`SubGraph` lleva un campo `subgraph_contents` que persiste
recursivamente sus hijos. El esquema 0.3 sigue siendo
cargable (los grafos viejos no tenían `SubGraph`s, así que
*round-tripean* limpio); guardar uno con SubGraphs lo emite
como 0.4.

## Copy-paste profundo

`Ctrl+C` / `Ctrl+V` sobre un `SubGraph` clona su contenido
recursivamente: IDs nuevos para todos los nodos hijos, edges
internas reconectadas, y los puertos del SubGraph contenedor
se reconectan localmente. La copia es independiente del
original.

## Live-tuning de parámetros internos

`ScilabBridge::sendParameter` acepta un **path** jerárquico
para tocar parámetros de nodos dentro de un SubGraph sin
tener que aplanar el grafo. Internamente, el bridge resuelve
el path al `flatId` del nodo hijo en el plan del solver y
emite la asignación como cualquier otro tuning. Los
*sliders* del panel flotante funcionan igual desde dentro de
un SubGraph que desde fuera.

## Validación

La gramática R0–R5 se aplica **recursivamente**: cada
SubGraph es en sí un grafo válido si y solo si su contenido lo
es. Un cable inválido dentro de un SubGraph no se acepta, y
el editor reporta el error con el breadcrumb completo
(`Lazo PID / Compensador: regla R0 violada`).
