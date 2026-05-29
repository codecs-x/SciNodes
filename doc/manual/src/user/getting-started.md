# Primer grafo en 5 minutos

Esta página te lleva de un canvas vacío a un motor DC simulado
con su respuesta animada en pantalla.  No hay que escribir
código; todo se hace con clicks y atajos.

## Conceptos básicos

- Un **nodo** es una caja con puertos.  Las salidas se conectan
  a entradas de otros nodos con **cables** (curvas Bézier).
- El grafo se ejecuta de izquierda a derecha: las fuentes
  generan señales, los transformadores las modifican, los
  sumideros las visualizan o exportan.
- El motor de cálculo es **Scilab**: SciNodes traduce el grafo
  a un script `.sce` y lo ejecuta en un subproceso `scilab-cli`.

## Paso 1 — Crear el primer nodo

1. Abrí SciNodes.  Vas a ver un canvas vacío y una barra de
   estado abajo.
2. Pulsá <kbd>Shift</kbd>+<kbd>A</kbd>.  Aparece un popup con
   un buscador de nodos.
3. Escribí `step` y pulsá <kbd>Enter</kbd> — se crea un nodo
   `StepSignal` en la posición del cursor.

## Paso 2 — Crecer el grafo arrastrando desde un pin

Acá viene el gesto central de SciNodes:

1. Posicioná el cursor sobre el círculo de **salida** (lado
   derecho) del `StepSignal`.
2. Mantené apretado el botón izquierdo y arrastrá hacia el
   **canvas vacío**.  Vas a ver un cable "fantasma" que sigue
   el cursor.
3. Soltá en cualquier punto libre.  Aparece el mismo popup,
   pero ahora **el nodo nuevo se va a cablear automáticamente
   al StepSignal**.
4. Escribí `gain` y <kbd>Enter</kbd>.  El `Gain` aparece ya
   conectado.
5. Repetí desde `Gain.out`: drag al vacío → popup → `oscilloscope`
   → <kbd>Enter</kbd>.

> **Por qué este gesto**: en Xcos hay que arrastrar el bloque
> desde una palette, posicionarlo, y luego dibujar el cable —
> tres acciones.  En SciNodes el drag-desde-pin-al-vacío
> colapsa "crear + cablear" en un solo gesto cuando el flujo
> es causal (cada nodo nace de la salida del anterior).

El popup tiene dos modos:

- **Drag desde un OUT**: lista NodeTypes para crear, **y** una
  sección "Nodos en el canvas" con los inputs disponibles —
  elegir uno crea un edge directo (sin nodo nuevo).
- **Drag desde un IN**: lista NodeTypes para crear, **y** una
  sección "Nodos en el canvas" con los outputs disponibles —
  elegir uno crea un **Alias** (atajo visual al nodo elegido).
  Ver [Aliases](aliases.md).

## Paso 3 — Ajustar valores

- En el nodo `StepSignal`, hacé doble-click sobre el campo
  `Final value` y escribí `1 rad`.  El campo acepta unidades.
- En el `Gain`, arrastrá el deslizador horizontalmente para
  cambiar `Gain` a 2.0.

> SciNodes propaga unidades por el grafo.  Como `StepSignal`
> sale en `rad` y el `Gain` no cambia la unidad, el
> `Oscilloscope` muestra el eje Y rotulado `rad` solo.

## Paso 4 — Correr

Click en el botón **▶ Run** de la barra de estado.

Después de un par de segundos verás el plot moverse en tiempo
real dentro del nodo `Oscilloscope`.  La barra de estado muestra
el tiempo simulado avanzando.

## Paso 5 — Tunear en vivo

1. Click en el botón **⏸ Pause**.  El reloj se congela.
2. Arrastrá el `Gain` a 5.0.
3. Click en **▶ Resume**.

El plot continúa donde estaba pero ahora con el nuevo `Gain`.
Esto es el **hot-reload**: SciNodes preserva el estado
`(t, x, buffers)` mientras editás.

## Próximos pasos

- [Catálogo de nodos](nodes.md) — la lista completa de qué
  podés agregar al grafo.
- [Sub-grafos](subgraphs.md) — cómo agrupar 10 nodos en un
  bloque compuesto.
- [Visualización 3D](3d.md) — cómo conectar un motor 3D a tu
  simulación.
- [Atajos de teclado](shortcuts.md) — qué pulsar para ir más rápido.
