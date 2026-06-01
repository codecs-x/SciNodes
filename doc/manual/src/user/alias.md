# Alias: referencia virtual a un nodo

A partir de v0.0.9 el editor gana el nodo **`Alias`**: una
fuente virtual que apunta a la salida de otro nodo del grafo
sin tender un cable visible.

Es útil en dos situaciones:

- **Cerrar lazos lejanos sin cruzar el canvas.** Si la salida
  de un compensador alimenta una entrada del modelo en la
  esquina opuesta, un cable largo es ruidoso. Un `Alias`
  apuntando al compensador, colocado al lado del modelo,
  resuelve el caso sin cruces.
- **Reutilizar una salida en varios sitios.** Cuando una
  señal de referencia alimenta tres consumidores distintos,
  poner tres `Alias` cerca de cada consumidor evita la
  estrella de cables desde la fuente.

> El patrón "feedback con Alias" se ve aplicado en los
> ejemplos `walkthrough_E1_dc` (lazo PID + Motor DC) y
> `walkthrough_E6` (brazo 2R, un `Alias` por eje dentro de
> cada SubGraph).

## Cómo cablear

1. `Shift+A` en una zona vacía del canvas y elegí `Alias`.
2. El nodo aparece con un campo de target. Click sobre el
   campo y aparece una lista flotante con todos los nodos
   del grafo, agrupados por categoría.
3. Elegí el nodo destino y el puerto de salida que querés
   referenciar.
4. La salida del `Alias` se comporta exactamente como la
   salida real del nodo apuntado.

El título del nodo refleja el target (`→ NodoOrigen`), el
color es ámbar para distinguirlo del resto, y el ancho del
cuerpo se ajusta exactamente al texto del target.

## Encapsulate: `Alias` y target viajan juntos

Cuando seleccionás un fragmento y lo encapsulás con
`Ctrl+G`, el editor revisa los `Alias` involucrados:

- Si seleccionaste un `Alias` y su target, ambos entran al
  `SubGraph`.
- Si seleccionaste un `Alias` pero **no** su target, el
  editor incluye automáticamente el target en el encapsulate
  para que la referencia siga siendo válida.
- Si seleccionaste el target pero **no** algún `Alias` que
  apunta a él, el editor incluye los `Alias` automáticamente.

Esta auto-inclusión/exclusión evita romper la referencia al
mover un fragmento del grafo a un nivel diferente.

## Persistencia y validación

El `Alias` se serializa con `target_node_id` y `target_port`.
Si el target no existe al cargar el `.scn` (porque fue
borrado fuera del editor), el loader reporta el problema en
`LoadReport::unresolvedAliases` y el `Alias` queda en estado
inválido (color rojo) hasta que apuntes a otro nodo.

R7 se aplica también a la salida del `Alias`: la unidad del
puerto referenciado se propaga al cable que sale del `Alias`.
