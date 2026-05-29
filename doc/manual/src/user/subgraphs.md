# Sub-grafos y encapsulación

Un grafo de 50 nodos es legible.  Uno de 500 no.  Los sub-grafos
te dejan agrupar piezas de tu modelo en cajas reutilizables.

## Cuándo encapsular

- Tenés un controlador (PID + filtros) que querés esconder
  como `Controller`.
- Tenés el motor DC modelado a partir de primitivas
  (10 ganancias + 2 integradores) y querés mostrar sólo
  `MotorDC` con un puerto V de entrada y un puerto ω de salida.
- Querés reusar la misma estructura en otro grafo (copy-paste
  del SubGraph completo).

## Cómo encapsular

1. Box-select sobre los nodos que querés agrupar (click vacío
   + arrastrar para dibujar el rectángulo de selección).
2. Pulsá <kbd>Ctrl</kbd>+<kbd>G</kbd>.

SciNodes materializa exactamente **un** `SubGraphInput` por cada
arista que entraba al grupo y un `SubGraphOutput` por cada
arista que salía.  Los stubs preservan la unidad y el tipo
del puerto original.

## Cómo navegar adentro

- **Doble-click** sobre un `SubGraph` para entrar.
- <kbd>Esc</kbd> para salir.
- El breadcrumb arriba del canvas muestra dónde estás:
  `Raíz / Controller / FiltroDerivativo`.

## Niveles de anidamiento

SciNodes soporta sub-grafos anidados sin límite práctico.  Si
tu controlador contiene un filtro, y el filtro internamente
usa un compensador, podés tener tres niveles fácilmente sin
perder claridad.

## Buscar dentro de sub-grafos

Pulsá <kbd>Shift</kbd>+<kbd>B</kbd>.  Aparece un popup donde
escribís el nombre del nodo y SciNodes lo busca recursivamente
en todos los sub-grafos del grafo activo.

Cuando seleccionás un resultado:

- La cámara navega al sub-grafo correspondiente.
- El nodo queda seleccionado y centrado en la viewport.

Atajos en el resultado:

- <kbd>C</kbd>: centra la cámara en el nodo (sin cambiar zoom).
- <kbd>E</kbd>: encuadra el nodo (ajusta zoom hasta 1.5×).
- <kbd>Enter</kbd>: igual que click.
- <kbd>Esc</kbd>: cierra el popup sin cambiar nada.

## Desencapsular

Actualmente no hay atajo directo para desagrupar un SubGraph.
El workaround: <kbd>Ctrl</kbd>+<kbd>Z</kbd> inmediatamente
después de encapsular, o entrar al SubGraph, copiar
todo el interior al canvas padre, y borrar el SubGraph vacío.

Una operación `decoupleSelection` en `NodeGraph` está
diseñada pero todavía no expuesta como atajo.  Issue
abierto.

## Buenas prácticas

- **Un sub-grafo = un concepto**.  Si lo nombrás "Stuff" o
  "Block1", estás encapsulando por tamaño visual, no por
  significado físico.  Mejor: "Filtro_Derivativo", "MotorDC",
  "RealimentaciónVelocidad".
- **Renombrá los puertos**: SciNodes nombra los stubs por
  defecto como `in1`, `out1`.  Entrá al sub-grafo y editá el
  campo Name del `SubGraphInput`/`SubGraphOutput` a algo como
  `V`, `omega`, `theta`.
- **Cuidado con la deuda de layout**: las posiciones de los nodos
  dentro de un sub-grafo se persisten sólo en el renderer (no
  en el archivo `.scn` todavía).  Si vas a compartir el grafo,
  abrí cada sub-grafo y guardá antes para forzar el sync.
